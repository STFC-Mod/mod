// Owns metadata lookup and signature checks for typed bindings, and their diagnostics.

#include <il2cpp/il2cpp.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_set>

namespace il2cpp
{
namespace
{
  std::atomic<void (*)(std::string_view)> error_handler = nullptr;

  void Report(const std::string& message)
  {
    if (auto* handler = error_handler.load())
      handler(message);
    else
      std::fprintf(stderr, "%s\n", message.c_str());
  }

  std::string ClassName(Il2CppClass* klass)
  {
    if (!klass)
      return "<null>";
    if (auto* outer = il2cpp_class_get_declaring_type(klass))
      return ClassName(outer) + "/" + il2cpp_class_get_name(klass);
    std::string name = il2cpp_class_get_namespace(klass);
    return name.empty() ? il2cpp_class_get_name(klass) : name + "." + il2cpp_class_get_name(klass);
  }

  std::string TypeName(const Il2CppType* type)
  {
    auto*       raw  = type ? il2cpp_type_get_name(type) : nullptr;
    std::string name = raw ? raw : "?";
    if (raw)
      il2cpp_free(raw);
    return type && type->byref ? "ref " + name : name;
  }

  std::string Describe(const MethodInfo* method)
  {
    std::string text = TypeName(method->return_type) + " " + method->name + "(";
    for (uint8_t i = 0; i < method->parameters_count; ++i)
      text += (i ? ", " : "") + TypeName(method->parameters[i]);
    return text + ")";
  }

  std::string Describe(const detail::Slot& slot)
  {
    using Kind       = detail::Slot::Kind;
    std::string text = slot.byref ? "ref " : "";
    if (slot.kind == Kind::kVoid)
      return text + "void";
    if (slot.kind == Kind::kValue)
      return text + std::to_string(slot.size) + "-byte value";
    auto* expected = slot.klass ? slot.klass() : nullptr;
    return text + (expected ? ClassName(expected) : "reference");
  }

  std::string Describe(const detail::Signature& signature)
  {
    std::string text = Describe(signature.result) + " (";
    for (size_t i = 0; i < signature.params.size(); ++i)
      text += (i ? ", " : "") + Describe(signature.params[i]);
    return text + ")";
  }

  bool IsIntegral(Il2CppTypeEnum type)
  { return (type >= IL2CPP_TYPE_CHAR && type <= IL2CPP_TYPE_U8) || type == IL2CPP_TYPE_I || type == IL2CPP_TYPE_U; }

  // Integers match by size, so bindings may pick signedness or use enums for managed integers.
  // Floats, booleans, and structs must agree on kind as well.
  bool Matches(const Il2CppType* type, const detail::Slot& slot)
  {
    using Kind = detail::Slot::Kind;
    if (!type)
      return false;
    if (slot.kind == Kind::kVoid)
      return type->type == IL2CPP_TYPE_VOID;
    if (type->byref != slot.byref)
      return false;
    auto* klass = il2cpp_class_from_type(type);
    if (!klass)
      return false;
    if (slot.kind == Kind::kReference) {
      // Untyped pointers also stand in for IntPtr and unmanaged pointers.
      if (!slot.klass)
        return !il2cpp_class_is_valuetype(klass) || type->type == IL2CPP_TYPE_I || type->type == IL2CPP_TYPE_U
               || type->type == IL2CPP_TYPE_PTR;
      if (il2cpp_class_is_valuetype(klass))
        return false;
      // Bindings may name a base or derived class of the declared type. If the binding's own class
      // is missing, nothing matches; guessing could pass an object of the wrong type.
      auto* expected = slot.klass();
      return expected
             && (il2cpp_class_is_assignable_from(klass, expected) || il2cpp_class_is_assignable_from(expected, klass));
    }
    uint32_t alignment = 0;
    if (!il2cpp_class_is_valuetype(klass)
        || static_cast<uint32_t>(il2cpp_class_value_size(klass, &alignment)) != slot.size)
      return false;
    if (il2cpp_class_is_enum(klass) || IsIntegral(type->type))
      return IsIntegral(slot.primitive);
    if (slot.primitive == IL2CPP_TYPE_VALUETYPE)
      return type->type == IL2CPP_TYPE_VALUETYPE || type->type == IL2CPP_TYPE_GENERICINST;
    return type->type == slot.primitive;
  }

  bool Matches(const MethodInfo* method, const detail::Signature& signature)
  {
    if (method->parameters_count != signature.params.size() || !Matches(method->return_type, signature.result))
      return false;
    for (size_t i = 0; i < signature.params.size(); ++i) {
      if (!Matches(method->parameters[i], signature.params[i]))
        return false;
    }
    return true;
  }

  bool IsStatic(const MethodInfo* method)
  { return (method->flags & METHOD_ATTRIBUTE_STATIC) != 0; }

  // Generic method definitions have no native code, including those on inflated classes
  // (is_generic and is_inflated both set). Generic instance methods have is_generic clear.
  bool Callable(const MethodInfo* method)
  { return !method->is_generic; }

  // Constructors are not inherited, so they are only looked up on the class itself.
  template <typename Visit> void ForEachMethod(Il2CppClass* klass, std::string_view name, Visit visit)
  {
    const bool inherited = name != ".ctor" && name != ".cctor";
    for (; klass; klass = inherited ? il2cpp_class_get_parent(klass) : nullptr) {
      void* iter = nullptr;
      while (const auto* method = il2cpp_class_get_methods(klass, &iter)) {
        if (name == method->name && !visit(method))
          return;
      }
    }
  }

  bool TypeNamed(const Il2CppType* type, std::string_view expected)
  {
    auto* klass = type ? il2cpp_class_from_type(type) : nullptr;
    if (!klass)
      return false;
    const auto dot = expected.rfind('.');
    if (dot == std::string_view::npos)
      return expected == il2cpp_class_get_name(klass);
    return expected.substr(0, dot) == il2cpp_class_get_namespace(klass)
           && expected.substr(dot + 1) == il2cpp_class_get_name(klass);
  }
} // namespace

void SetErrorHandler(void (*handler)(std::string_view message))
{ error_handler = handler; }

namespace detail
{
  void ReportMissingICall(const char* name)
  { Report(std::string("[IL2CPP] missing internal call ") + name); }

  Il2CppClass* StringClass()
  {
    static auto* klass = il2cpp_class_from_name(il2cpp_get_corlib(), "System", "String");
    return klass;
  }

  Il2CppClass* TypeClass()
  {
    static auto* klass = il2cpp_class_from_name(il2cpp_get_corlib(), "System", "Type");
    return klass;
  }

  Il2CppClass* FindClass(std::string_view assembly, std::string_view namespaze, std::string_view name)
  {
    const std::string assembly_name(assembly), namespace_name(namespaze);
    auto*             domain = il2cpp_domain_get();
    auto*             loaded = domain ? il2cpp_domain_assembly_open(domain, assembly_name.c_str()) : nullptr;
    auto*             image  = loaded ? il2cpp_assembly_get_image(loaded) : nullptr;

    // Nested types are reached through their declaring type: "Outer/Inner".
    const auto   slash = name.find('/');
    const auto   outer = std::string(name.substr(0, slash));
    Il2CppClass* klass = image ? il2cpp_class_from_name(image, namespace_name.c_str(), outer.c_str()) : nullptr;
    for (auto rest = slash; klass && rest != std::string_view::npos;) {
      const auto next   = name.find('/', rest + 1);
      const auto nested = name.substr(rest + 1, next == std::string_view::npos ? next : next - rest - 1);
      void*      iter   = nullptr;
      auto*      parent = std::exchange(klass, nullptr);
      while (auto* candidate = il2cpp_class_get_nested_types(parent, &iter)) {
        if (nested == il2cpp_class_get_name(candidate)) {
          klass = candidate;
          break;
        }
      }
      rest = next;
    }
    if (!klass)
      Report("[IL2CPP] missing class " + assembly_name + ":" + (namespace_name.empty() ? "" : namespace_name + ".")
             + std::string(name));
    return klass;
  }

  Method FindMethod(Il2CppClass* klass, std::string_view name, int count)
  {
    if (!klass)
      return {};
    // Abstract methods and generic definitions only have shared stubs, which must never be detoured.
    const MethodInfo* found = nullptr;
    ForEachMethod(klass, name, [&](const MethodInfo* method) {
      if (!Callable(method) || (method->flags & METHOD_ATTRIBUTE_ABSTRACT)
          || (count >= 0 && method->parameters_count != count))
        return true;
      found = method;
      return false;
    });
    if (!found)
      Report("[IL2CPP] missing method " + ClassName(klass) + "." + std::string(name)
             + (count >= 0 ? " with " + std::to_string(count) + " parameters" : ""));
    return Method(found);
  }

  Method FindMethod(Il2CppClass* klass, std::string_view name, std::initializer_list<std::string_view> params)
  {
    if (!klass)
      return {};
    const MethodInfo* found = nullptr;
    ForEachMethod(klass, name, [&](const MethodInfo* method) {
      if (!Callable(method) || (method->flags & METHOD_ATTRIBUTE_ABSTRACT) || method->parameters_count != params.size())
        return true;
      size_t index = 0;
      for (auto param : params) {
        if (!TypeNamed(method->parameters[index++], param))
          return true;
      }
      found = method;
      return false;
    });
    if (!found) {
      std::string list;
      for (auto param : params)
        list += (list.empty() ? "" : ", ") + std::string(param);
      Report("[IL2CPP] missing method " + ClassName(klass) + "." + std::string(name) + "(" + list + ")");
    }
    return Method(found);
  }

  const MethodInfo* ResolveMethod(Il2CppClass* klass, std::string_view name, const Signature& signature, bool is_static)
  {
    if (!klass)
      return nullptr;
    // The most derived match wins; virtual calls still dispatch on the receiver.
    const MethodInfo* found = nullptr;
    std::string       candidates;
    ForEachMethod(klass, name, [&](const MethodInfo* method) {
      if (!Callable(method) || IsStatic(method) != is_static)
        return true;
      if (Matches(method, signature)) {
        found = method;
        return false;
      }
      candidates += "\n  " + Describe(method);
      return true;
    });
    if (found) {
      if (is_static)
        il2cpp_runtime_class_init(found->klass);
      return found;
    }
    const auto member = ClassName(klass) + "." + std::string(name);
    if (candidates.empty())
      Report("[IL2CPP] missing " + std::string(is_static ? "static " : "") + "method " + member);
    else
      Report("[IL2CPP] no overload of " + member + " matches " + Describe(signature) + "; found:" + candidates);
    return nullptr;
  }

  const MethodInfo* ResolveProperty(Il2CppClass* klass, std::string_view name, const Slot& value, bool setter,
                                    bool is_static)
  {
    if (!klass)
      return nullptr;
    const std::string property_name(name);
    const auto        member   = ClassName(klass) + "." + property_name;
    const auto*       property = il2cpp_class_get_property_from_name(klass, property_name.c_str());
    if (!property) {
      Report("[IL2CPP] missing property " + member);
      return nullptr;
    }
    // An override may redeclare only one accessor; the other is inherited from a base property.
    const MethodInfo* method = nullptr;
    for (; property; property = property->parent && il2cpp_class_get_parent(property->parent)
                                    ? il2cpp_class_get_property_from_name(il2cpp_class_get_parent(property->parent),
                                                                          property_name.c_str())
                                    : nullptr) {
      method = setter ? il2cpp_property_get_set_method(const_cast<PropertyInfo*>(property))
                      : il2cpp_property_get_get_method(const_cast<PropertyInfo*>(property));
      if (method)
        break;
    }
    if (!method) {
      Report("[IL2CPP] property " + member + " has no " + (setter ? "setter" : "getter"));
      return nullptr;
    }
    if (IsStatic(method) != is_static) {
      Report("[IL2CPP] property " + member + (is_static ? " is not static" : " is static"));
      return nullptr;
    }
    const Slot void_slot;
    const bool matches = setter ? method->parameters_count == 1 && Matches(method->parameters[0], value)
                                      && Matches(method->return_type, void_slot)
                                : method->parameters_count == 0 && Matches(method->return_type, value);
    if (!matches) {
      Report("[IL2CPP] property " + member + " is " + TypeName(setter ? method->parameters[0] : method->return_type)
             + ", binding expects " + Describe(value));
      return nullptr;
    }
    if (is_static)
      il2cpp_runtime_class_init(method->klass);
    return method;
  }

  FieldInfo* ResolveField(Il2CppClass* klass, std::string_view name, const Slot& value, bool is_static)
  {
    if (!klass)
      return nullptr;
    const std::string field_name(name);
    const auto        member = ClassName(klass) + "." + field_name;
    auto*             field  = il2cpp_class_get_field_from_name(klass, field_name.c_str());
    if (!field) {
      Report("[IL2CPP] missing field " + member);
      return nullptr;
    }
    if (((field->type->attrs & FIELD_ATTRIBUTE_STATIC) != 0) != is_static) {
      Report("[IL2CPP] field " + member + (is_static ? " is not static" : " is static"));
      return nullptr;
    }
    if (field->offset == THREAD_STATIC_FIELD_OFFSET) {
      Report("[IL2CPP] field " + member + " is thread-static");
      return nullptr;
    }
    if (!Matches(field->type, value)) {
      Report("[IL2CPP] field " + member + " is " + TypeName(field->type) + ", binding expects " + Describe(value));
      return nullptr;
    }
    if (is_static)
      il2cpp_runtime_class_init(field->parent);
    return field;
  }

  void ReportException(const MethodInfo* method, Il2CppException* exception)
  {
    // Once per method: a getter that throws every frame would otherwise flood the log.
    static std::mutex                            mutex;
    static std::unordered_set<const MethodInfo*> reported;
    {
      std::lock_guard lock(mutex);
      if (!reported.insert(method).second)
        return;
    }
    std::string message = "[IL2CPP] " + ClassName(method->klass) + "." + method->name + " threw";
    if (exception) {
      char text[512]{};
      il2cpp_format_exception(exception, text, sizeof(text));
      message += std::string(": ") + text;
    }
    Report(message + " (further exceptions from this method are not logged)");
  }

  Il2CppObject* InvokeBoxed(const MethodInfo* method, void* target, void** args)
  {
    // The runtime catches managed exceptions here and only hands them back through the slot.
    Il2CppException* exception = nullptr;
    auto*            result    = il2cpp_runtime_invoke(method, target, args, &exception);
    if (exception) {
      ReportException(method, exception);
      return nullptr;
    }
    return result;
  }
} // namespace detail

Il2CppObject* NewDelegate(Il2CppClass* delegate_class, Method target, Il2CppMethodPointer native)
{
  const auto* info = target.Info();
  if (!delegate_class || !info || !IsStatic(info) || !Callable(info))
    return nullptr;
  // Delegate(object target, IntPtr method). Runtime invocation rejects static targets for this
  // constructor; its generated invoker accepts a null target and a MethodInfo address.
  const MethodInfo* ctor = nullptr;
  ForEachMethod(delegate_class, ".ctor", [&](const MethodInfo* method) {
    if (method->parameters_count == 2 && method->parameters[0]->type == IL2CPP_TYPE_OBJECT
        && method->parameters[1]->type == IL2CPP_TYPE_I && method->invoker_method)
      ctor = method;
    return ctor == nullptr;
  });
  if (!ctor) {
    Report("[IL2CPP] " + ClassName(delegate_class) + " has no (object, IntPtr) delegate constructor");
    return nullptr;
  }
  auto* delegate = il2cpp_object_new(delegate_class);
  void* args[]   = {nullptr, &info};
  ctor->invoker_method(ctor->methodPointer, ctor, delegate, args, nullptr);
  if (native)
    reinterpret_cast<Il2CppDelegate*>(delegate)->method_ptr = native;
  return delegate;
}
} // namespace il2cpp

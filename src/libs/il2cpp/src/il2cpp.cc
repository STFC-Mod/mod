// Owns metadata lookup, signature checks and their diagnostics for typed bindings, plus managed string conversion
// and delegate construction.

#include <il2cpp/il2cpp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <format>
#include <functional>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

namespace il2cpp
{
namespace
{
  std::atomic<void (*)(std::string_view)> error_handler = nullptr;

  std::span<const Il2CppType* const> Parameters(const MethodInfo* method)
  { return {method->parameters, method->parameters_count}; }

  // "a, b, c" from each item's text.
  std::string Join(auto&& items, auto text)
  {
    std::string joined;
    for (auto&& item : items) {
      if (!joined.empty())
        joined += ", ";
      joined += text(item);
    }
    return joined;
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
  { return std::format("{} {}({})", TypeName(method->return_type), method->name, Join(Parameters(method), TypeName)); }

  std::string Describe(const detail::Slot& slot)
  {
    using Kind        = detail::Slot::Kind;
    const auto* byref = slot.byref ? "ref " : "";
    if (slot.kind == Kind::kVoid)
      return std::format("{}void", byref);
    if (slot.kind == Kind::kValue)
      return std::format("{}{}-byte value", byref, slot.size);
    auto* expected = slot.klass ? slot.klass() : nullptr;
    return std::format("{}{}", byref, expected ? ClassName(expected) : "reference");
  }

  std::string Describe(const detail::Signature& signature)
  {
    return std::format("{} ({})", Describe(signature.result),
                       Join(signature.params, [](const detail::Slot& slot) { return Describe(slot); }));
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
    return method->parameters_count == signature.params.size() && Matches(method->return_type, signature.result)
           && std::ranges::equal(Parameters(method), signature.params,
                                 [](const Il2CppType* type, const detail::Slot& slot) { return Matches(type, slot); });
  }

  bool IsStatic(const MethodInfo* method)
  { return (method->flags & METHOD_ATTRIBUTE_STATIC) != 0; }

  // Generic method definitions have no native code, including those on inflated classes
  // (is_generic and is_inflated both set). Generic instance methods have is_generic clear.
  bool Callable(const MethodInfo* method)
  { return !method->is_generic; }

  // Abstract methods and generic definitions only have shared stubs, which must never be detoured.
  bool Detourable(const MethodInfo* method)
  { return Callable(method) && !(method->flags & METHOD_ATTRIBUTE_ABSTRACT); }

  // The first method called `name` that `accept` takes, searching the class before its bases. Constructors are not
  // inherited, so they are only looked up on the class itself.
  const MethodInfo* FirstMethod(Il2CppClass* klass, std::string_view name, auto accept)
  {
    const bool inherited = name != ".ctor" && name != ".cctor";
    for (; klass; klass = inherited ? il2cpp_class_get_parent(klass) : nullptr) {
      void* iter = nullptr;
      while (const auto* method = il2cpp_class_get_methods(klass, &iter))
        if (name == method->name && accept(method))
          return method;
    }
    return nullptr;
  }

  // Parameter names are "Namespace.Type", or just "Type" for any namespace.
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

  Il2CppClass* NestedClass(Il2CppClass* outer, std::string_view name)
  {
    void* iter = nullptr;
    while (auto* candidate = il2cpp_class_get_nested_types(outer, &iter))
      if (name == il2cpp_class_get_name(candidate))
        return candidate;
    return nullptr;
  }
} // namespace

void SetErrorHandler(void (*handler)(std::string_view message))
{ error_handler = handler; }

void Report(std::string_view message)
{
  if (auto* handler = error_handler.load())
    handler(message);
  else
    std::fprintf(stderr, "%.*s\n", static_cast<int>(message.size()), message.data());
}

std::string ClassName(Il2CppClass* klass)
{
  if (!klass)
    return "<null>";
  if (auto* outer = il2cpp_class_get_declaring_type(klass))
    return std::format("{}/{}", ClassName(outer), il2cpp_class_get_name(klass));
  const std::string_view namespaze = il2cpp_class_get_namespace(klass);
  return namespaze.empty() ? il2cpp_class_get_name(klass)
                           : std::format("{}.{}", namespaze, il2cpp_class_get_name(klass));
}

std::string Method::Name() const
{ return info_ ? std::format("{}.{}", ClassName(info_->klass), info_->name) : "<missing method>"; }

Il2CppClass* ObjectClass()
{
  static auto* klass = il2cpp_class_from_name(il2cpp_get_corlib(), "System", "Object");
  return klass;
}

std::string ToUtf8(const Il2CppString* str)
{
  std::string result;
  if (!str)
    return result;
  const std::span<const Il2CppChar> chars(str->chars, static_cast<size_t>(str->length));
  result.reserve(chars.size());
  const auto in = [](uint32_t c, uint32_t low, uint32_t high) { return c >= low && c <= high; };
  for (size_t i = 0; i < chars.size(); ++i) {
    uint32_t c = chars[i];
    // A surrogate pair is one code point; a lone surrogate becomes U+FFFD.
    if (in(c, 0xD800, 0xDBFF) && i + 1 < chars.size() && in(chars[i + 1], 0xDC00, 0xDFFF))
      c = 0x10000 + ((c - 0xD800) << 10) + (chars[++i] - 0xDC00);
    else if (in(c, 0xD800, 0xDFFF))
      c = 0xFFFD;

    if (c < 0x80) {
      result += static_cast<char>(c);
    } else if (c < 0x800) {
      result += static_cast<char>(0xC0 | (c >> 6));
      result += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      result += static_cast<char>(0xE0 | (c >> 12));
      result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      result += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      result += static_cast<char>(0xF0 | (c >> 18));
      result += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      result += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return result;
}

namespace detail
{
  void ReportMissingICall(const char* name)
  { Report(std::format("[IL2CPP] missing internal call {}", name)); }

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

  Il2CppClass* PrimitiveClass(Il2CppTypeEnum type)
  {
    using Entry                     = std::pair<Il2CppTypeEnum, const char*>;
    static constexpr Entry kNames[] = {
        {IL2CPP_TYPE_BOOLEAN, "Boolean"}, {IL2CPP_TYPE_CHAR, "Char"}, {IL2CPP_TYPE_I1, "SByte"},
        {IL2CPP_TYPE_U1, "Byte"},         {IL2CPP_TYPE_I2, "Int16"},  {IL2CPP_TYPE_U2, "UInt16"},
        {IL2CPP_TYPE_I4, "Int32"},        {IL2CPP_TYPE_U4, "UInt32"}, {IL2CPP_TYPE_I8, "Int64"},
        {IL2CPP_TYPE_U8, "UInt64"},       {IL2CPP_TYPE_R4, "Single"}, {IL2CPP_TYPE_R8, "Double"},
    };
    static std::array<std::atomic<Il2CppClass*>, std::size(kNames)> classes{};
    const auto* entry = std::ranges::find(kNames, type, &Entry::first);
    if (entry == std::end(kNames))
      return nullptr;
    auto& cached = classes[entry - std::begin(kNames)];
    auto* klass  = cached.load();
    if (!klass) {
      klass = il2cpp_class_from_name(il2cpp_get_corlib(), "System", entry->second);
      cached.store(klass);
    }
    return klass;
  }

  void ReportFault(const char* what)
  { Report(std::format("[IL2CPP] access violation while reading {}", what)); }

  Il2CppClass* FindClass(std::string_view assembly, std::string_view namespaze, std::string_view name)
  {
    const std::string namespace_name(namespaze);
    auto*             domain = il2cpp_domain_get();
    auto*             loaded = domain ? il2cpp_domain_assembly_open(domain, std::string(assembly).c_str()) : nullptr;
    auto*             image  = loaded ? il2cpp_assembly_get_image(loaded) : nullptr;

    // Nested types are reached through their declaring type: "Outer/Inner".
    const auto slash = name.find('/');
    auto*      klass =
        image ? il2cpp_class_from_name(image, namespace_name.c_str(), std::string(name.substr(0, slash)).c_str())
              : nullptr;
    if (slash != std::string_view::npos)
      for (const auto nested : name.substr(slash + 1) | std::views::split('/'))
        klass = klass ? NestedClass(klass, std::string_view(nested.begin(), nested.end())) : nullptr;
    if (!klass)
      Report(std::format("[IL2CPP] missing class {}:{}{}{}", assembly, namespaze, namespaze.empty() ? "" : ".", name));
    return klass;
  }

  Method FindMethod(Il2CppClass* klass, std::string_view name, int count)
  {
    if (!klass)
      return {};
    const auto* found = FirstMethod(klass, name, [count](const MethodInfo* method) {
      return Detourable(method) && (count < 0 || method->parameters_count == count);
    });
    if (!found)
      Report(std::format("[IL2CPP] missing method {}.{}{}", ClassName(klass), name,
                         count >= 0 ? std::format(" with {} parameters", count) : ""));
    return Method(found);
  }

  Method FindMethod(Il2CppClass* klass, std::string_view name, std::initializer_list<std::string_view> params)
  {
    if (!klass)
      return {};
    const auto* found = FirstMethod(klass, name, [params](const MethodInfo* method) {
      return Detourable(method) && std::ranges::equal(Parameters(method), params, TypeNamed);
    });
    if (!found)
      Report(std::format("[IL2CPP] missing method {}.{}({})", ClassName(klass), name, Join(params, std::identity{})));
    return Method(found);
  }

  const MethodInfo* ResolveMethod(Il2CppClass* klass, std::string_view name, const Signature& signature, bool is_static,
                                  bool initialize)
  {
    if (!klass)
      return nullptr;
    // The most derived match wins; virtual calls still dispatch on the receiver.
    std::string candidates;
    const auto* found = FirstMethod(klass, name, [&](const MethodInfo* method) {
      if (!Callable(method) || IsStatic(method) != is_static)
        return false;
      if (Matches(method, signature))
        return true;
      candidates += "\n  " + Describe(method);
      return false;
    });
    if (found) {
      if (is_static && initialize)
        il2cpp_runtime_class_init(found->klass);
      return found;
    }
    if (candidates.empty())
      Report(std::format("[IL2CPP] missing {}method {}.{}", is_static ? "static " : "", ClassName(klass), name));
    else
      Report(std::format("[IL2CPP] no overload of {}.{} matches {}; found:{}", ClassName(klass), name,
                         Describe(signature), candidates));
    return nullptr;
  }

  const MethodInfo* ResolveProperty(Il2CppClass* klass, std::string_view name, const Slot& value, bool setter,
                                    bool is_static)
  {
    if (!klass)
      return nullptr;
    const std::string property_name(name);
    const auto        member   = std::format("{}.{}", ClassName(klass), name);
    const auto*       property = il2cpp_class_get_property_from_name(klass, property_name.c_str());
    if (!property) {
      Report("[IL2CPP] missing property " + member);
      return nullptr;
    }
    // An override may redeclare only one accessor; the other is inherited from a base property.
    const MethodInfo* method = nullptr;
    while (property) {
      auto* raw = const_cast<PropertyInfo*>(property);
      if ((method = setter ? il2cpp_property_get_set_method(raw) : il2cpp_property_get_get_method(raw)))
        break;
      auto* base = property->parent ? il2cpp_class_get_parent(property->parent) : nullptr;
      property   = base ? il2cpp_class_get_property_from_name(base, property_name.c_str()) : nullptr;
    }
    if (!method) {
      Report(std::format("[IL2CPP] property {} has no {}", member, setter ? "setter" : "getter"));
      return nullptr;
    }
    if (IsStatic(method) != is_static) {
      Report(std::format("[IL2CPP] property {} {}", member, is_static ? "is not static" : "is static"));
      return nullptr;
    }
    const bool matches = setter ? method->parameters_count == 1 && Matches(method->parameters[0], value)
                                      && Matches(method->return_type, Slot{})
                                : method->parameters_count == 0 && Matches(method->return_type, value);
    if (!matches) {
      Report(std::format("[IL2CPP] property {} is {}, binding expects {}", member,
                         TypeName(setter ? method->parameters[0] : method->return_type), Describe(value)));
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
    const auto member = std::format("{}.{}", ClassName(klass), name);
    auto*      field  = il2cpp_class_get_field_from_name(klass, std::string(name).c_str());
    if (!field) {
      Report("[IL2CPP] missing field " + member);
      return nullptr;
    }
    if (((field->type->attrs & FIELD_ATTRIBUTE_STATIC) != 0) != is_static) {
      Report(std::format("[IL2CPP] field {} {}", member, is_static ? "is not static" : "is static"));
      return nullptr;
    }
    if (field->offset == THREAD_STATIC_FIELD_OFFSET) {
      Report(std::format("[IL2CPP] field {} is thread-static", member));
      return nullptr;
    }
    if (!Matches(field->type, value)) {
      Report(
          std::format("[IL2CPP] field {} is {}, binding expects {}", member, TypeName(field->type), Describe(value)));
      return nullptr;
    }
    if (is_static)
      il2cpp_runtime_class_init(field->parent);
    return field;
  }

  Method Hookable(const MethodInfo* method)
  {
    if (method && (method->has_full_generic_sharing_signature || (method->flags & METHOD_ATTRIBUTE_ABSTRACT))) {
      Report("[IL2CPP] cannot hook shared or abstract method " + Method(method).Name());
      return {};
    }
    return Method(method);
  }

  void ReportException(const MethodInfo* method, Il2CppException* exception)
  {
    // Once per method: a getter that throws every frame would otherwise flood the log.
    static std::mutex                            mutex;
    static std::unordered_set<const MethodInfo*> reported;
    {
      std::scoped_lock lock(mutex);
      if (!reported.insert(method).second)
        return;
    }
    // The runtime copies with strncpy, which leaves a full buffer unterminated. Keep the last byte NUL.
    char text[512]{};
    if (exception)
      il2cpp_format_exception(exception, text, sizeof(text) - 1);
    Report(std::format("[IL2CPP] {}.{} threw{}{} (further exceptions from this method are not logged)",
                       ClassName(method->klass), method->name, exception ? ": " : "", text));
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
  const auto* ctor = FirstMethod(delegate_class, ".ctor", [](const MethodInfo* method) {
    return method->parameters_count == 2 && method->parameters[0]->type == IL2CPP_TYPE_OBJECT
           && method->parameters[1]->type == IL2CPP_TYPE_I && method->invoker_method;
  });
  if (!ctor) {
    Report(std::format("[IL2CPP] {} has no (object, IntPtr) delegate constructor", ClassName(delegate_class)));
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

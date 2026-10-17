#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "interop.h"
#include "patches/native_hook_extent.h"
#include "settings/page_catalog.h"
#include <cstring>
#include <exception>
#include <il2cpp/runtime.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_set>

namespace mod_settings::native
{
void Warn(const char* operation)
{
  std::string message(operation);
  if (auto exception = std::current_exception()) {
    try {
      std::rethrow_exception(exception);
    } catch (const std::exception& error) {
      message += ": ";
      message += error.what();
    } catch (...) {
      message += ": non-standard C++ exception";
    }
  }
  static std::mutex                      mutex;
  static std::unordered_set<std::string> reported;
  {
    std::lock_guard lock(mutex);
    if (!reported.insert(message).second)
      return;
  }
  spdlog::warn("[ModSettings] {}", message);
}

bool Type(const Il2CppType* type, int expected)
{ return type && !type->byref && type->type == expected; }

bool Reference(const Il2CppType* type)
{
  return Type(type, IL2CPP_TYPE_CLASS) || Type(type, IL2CPP_TYPE_GENERICINST) || Type(type, IL2CPP_TYPE_OBJECT)
         || Type(type, IL2CPP_TYPE_STRING);
}

bool Instance(const MethodInfo* method, int count, int result)
{
  return method && method->methodPointer && method->invoker_method && !(method->flags & METHOD_ATTRIBUTE_STATIC)
         && method->parameters_count == count && Type(method->return_type, result)
         && !method->has_full_generic_sharing_signature;
}

void ValidateHookFit(const MethodInfo* method)
{
  if (!method || !method->methodPointer)
    throw std::runtime_error("settings hook target missing");
#if __APPLE__
  // Validate the actual loaded ARM64/x86_64 method boundary before SPUD writes
  // its prologue. A successful build does not establish native hook fit.
  if (!native_hooks::MacHookFits(reinterpret_cast<const void*>(method->methodPointer)))
    throw std::runtime_error(std::string(method->klass->name) + "." + method->name
                             + ": native hook extent unavailable");
#endif
}

FieldInfo* Field(Il2CppClass* cls, const char* name)
{
  auto* field = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  if (!field || !field->type || !Reference(field->type) || (field->type->attrs & FIELD_ATTRIBUTE_STATIC)) {
    const std::string member =
        std::string(cls ? cls->namespaze : "") + "." + (cls ? cls->name : "<missing class>") + "." + name;
    throw std::runtime_error(member + ": expected instance reference field; "
                             + (!field         ? "field missing"
                                : !field->type ? "type missing"
                                               : "actual type=" + std::to_string(field->type->type) + ", static="
                                                     + std::to_string(bool(field->type->attrs & FIELD_ATTRIBUTE_STATIC))
                                                     + ", byref=" + std::to_string(field->type->byref)));
  }
  return field;
}
Il2CppObject* ReadField(Il2CppObject* object, FieldInfo* field)
{
  Il2CppObject* value = nullptr;
  if (object)
    il2cpp_field_get_value(object, field, &value);
  return value;
}
Il2CppObject* Invoke(const MethodInfo* method, Il2CppObject* object, void** args)
{
  if (!method || !object)
    throw std::runtime_error(std::string("settings invocation: ") + (method ? method->klass->name : "<missing method>")
                             + "." + (method ? method->name : "?") + (object ? "" : ": missing receiver"));
  Il2CppObject* result = nullptr;
  if (!Il2CppRuntime::TryInvoke(method, object, args, &result))
    throw std::runtime_error(std::string(method->klass->name) + "." + method->name + ": managed invocation failed");
  return result;
}
// Bounded discovery helpers used only while opening a page or binding a row.
Il2CppObject* Call(Il2CppObject* object, const char* name, int count, void** args)
{
  auto* method = object ? IL2CppClassHelper(object->klass).GetMethodInfo(name, count) : nullptr;
  if (!method)
    throw std::runtime_error(std::string(object ? object->klass->name : "<missing receiver>") + "." + name + "("
                             + std::to_string(count) + " arguments): method unavailable");
  return Invoke(method, object, args);
}
bool Equals(Il2CppObject* value, const char* ascii)
{
  if (!value || !Type(il2cpp_class_get_type(value->klass), IL2CPP_TYPE_STRING))
    return false;
  auto*      text   = reinterpret_cast<Il2CppString*>(value);
  const auto length = std::strlen(ascii);
  if (il2cpp_string_length(text) != length)
    return false;
  auto* chars = il2cpp_string_chars(text);
  for (std::size_t i = 0; i < length; ++i)
    if (chars[i] != static_cast<unsigned char>(ascii[i]))
      return false;
  return true;
}
Il2CppObject* Target(Il2CppGCHandle handle)
{ return handle ? il2cpp_gchandle_get_target(handle) : nullptr; }
void Free(Il2CppGCHandle& handle)
{
  if (handle)
    il2cpp_gchandle_free(handle);
  handle = nullptr;
}

// Cosmetic changes belong to the bound row. Restore before native refresh or
void SetActive(Il2CppObject* object, bool value)
{
  void* args[] = {&value};
  Call(object, "SetActive", 1, args);
}
Il2CppObject* MakeDelegate(Il2CppClass* cls, Il2CppObject* director, const MethodInfo* method)
{
  const auto* ctor = cls ? il2cpp_class_get_method_from_name(cls, ".ctor", 2) : nullptr;
  if (!Instance(ctor, 2, IL2CPP_TYPE_VOID) || !Reference(ctor->parameters[0])
      || !Type(ctor->parameters[1], IL2CPP_TYPE_I) || !method)
    throw std::runtime_error("settings delegate constructor");
  const auto* invoke = il2cpp_class_get_method_from_name(cls, "Invoke", method->parameters_count);
  auto*       parent = il2cpp_class_get_parent(cls);
  if (!parent || std::strcmp(il2cpp_class_get_name(parent), "MulticastDelegate") != 0
      || std::strcmp(il2cpp_class_get_namespace(parent), "System") != 0 || !invoke || invoke->return_type->byref
      || (invoke->flags & METHOD_ATTRIBUTE_STATIC)
      || il2cpp_class_from_type(invoke->return_type) != il2cpp_class_from_type(method->return_type)
      || !il2cpp_class_is_assignable_from(method->klass, director->klass))
    throw std::runtime_error("settings delegate signature");
  for (int i = 0; i < method->parameters_count; ++i)
    if (invoke->parameters[i]->byref
        || il2cpp_class_from_type(invoke->parameters[i]) != il2cpp_class_from_type(method->parameters[i]))
      throw std::runtime_error("settings delegate parameter");
  Root  object(il2cpp_object_new(cls));
  void* args[] = {director, &method};
  Invoke(ctor, object.get(), args);
  auto* delegate = reinterpret_cast<Il2CppDelegate*>(object.get());
  if (delegate->target != director || delegate->invoke_impl_this != director || delegate->method != method)
    throw std::runtime_error("settings closed delegate");
  delegate->method_ptr  = method->methodPointer;
  delegate->invoke_impl = method->methodPointer;
  return object.get();
}

int Count(Il2CppObject* list)
{
  Root value(Call(list, "get_Count"));
  if (!value.get() || !Type(il2cpp_class_get_type(value.get()->klass), IL2CPP_TYPE_I4))
    throw std::runtime_error("settings child count");
  const int count = *static_cast<int*>(il2cpp_object_unbox(value.get()));
  if (count < 0 || count > PageCatalog::NativeChildLimit)
    throw std::runtime_error("settings child bound");
  return count;
}
Il2CppObject* Item(Il2CppObject* list, int index)
{
  void* args[] = {&index};
  return Call(list, "get_Item", 1, args);
}
bool HasLabel(Il2CppObject* row, const char* id)
{
  Root label(Call(row, "get_LabelContext"));
  return label.get() && Equals(Call(label.get(), "get_Identifier"), id);
}

} // namespace mod_settings::native
#endif

#pragma once
#include "interop.h"
#include <cstring>

namespace mod_settings::native::ui
{
inline Il2CppClass* Class(const char* assembly, const char* ns, const char* name)
{
  auto* cls = il2cpp_get_class_helper(assembly, ns, name).get_cls();
  if (!cls)
    throw std::runtime_error(std::string("settings UI class unavailable: ") + assembly + ":" + ns + "." + name);
  return cls;
}
inline Il2CppClass* UnityClass(const char* name)
{ return Class("UnityEngine.CoreModule", "UnityEngine", name); }
inline Il2CppObject* Static(const MethodInfo* method, void** args)
{
  Il2CppObject* result = nullptr;
  if (!Il2CppRuntime::TryInvoke(method, nullptr, args, &result))
    throw std::runtime_error("settings UI static invocation");
  return result;
}
inline Il2CppObject* UiCall(Il2CppObject* object, const char* name, int count = 0, void** args = nullptr)
{
  try {
    return Call(object, name, count, args);
  } catch (const std::exception&) {
    throw std::runtime_error(std::string("settings UI call: ")
                             + (object ? il2cpp_class_get_name(object->klass) : "null") + "." + name);
  }
}
inline bool Alive(Il2CppObject* object)
{
  if (!object)
    return false;
  static const auto* method = IL2CppClassHelper(UnityClass("Object")).GetMethodInfo("op_Implicit", 1);
  void*              args[] = {object};
  Root               result(Static(method, args));
  return Boolean(result.get());
}
inline void Retain(Il2CppGCHandle& handle, Il2CppObject* object)
{
  Free(handle);
  handle = object ? il2cpp_gchandle_new(object, false) : nullptr;
  if (!handle)
    throw std::runtime_error("settings UI root");
}
inline void Set(Il2CppObject* object, const char* method, void* value)
{
  void* args[] = {value};
  UiCall(object, method, 1, args);
}
template <class T> void Value(Il2CppObject* object, const char* method, T value)
{ Set(object, method, &value); }
inline Il2CppObject* WithType(Il2CppObject* object, const char* method, Il2CppClass* type, int arity = 1)
{
  auto* target = IL2CppClassHelper(object->klass).GetMethodInfoSpecial(method, [arity](auto count, auto params) {
    return count == arity && Reference(params[0])
           && (arity == 1 || (arity == 2 && Type(params[1], IL2CPP_TYPE_BOOLEAN)))
           && std::strcmp(il2cpp_class_get_name(il2cpp_class_from_type(params[0])), "Type") == 0;
  });
  if (!target)
    throw std::runtime_error(std::string("settings UI typed method unavailable: ") + method);
  Root  reflection(reinterpret_cast<Il2CppObject*>(il2cpp_type_get_object(il2cpp_class_get_type(type))));
  bool  includeInactive = false;
  void* args[]          = {reflection.get(), &includeInactive};
  return Invoke(target, object, args);
}
inline void Text(Il2CppObject* text, const std::string& value)
{
  Root string(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(value.c_str())));
  Set(text, "set_text", string.get());
}
} // namespace mod_settings::native::ui

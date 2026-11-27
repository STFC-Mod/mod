#pragma once

#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>

// Checked, exception-aware bindings for the optional occupied-node viewer.
// No client offsets or managed object references are cached.
namespace navigation_binding
{
inline const MethodInfo* Method(Il2CppObject* object, const char* name, const char* result,
                                std::initializer_list<const char*> parameters = {})
{
  for (auto* cls = object ? object->klass : nullptr; cls; cls = il2cpp_class_get_parent(cls)) {
    if (auto* method = method_contract::Resolve(cls, name, false, result, parameters))
      return method;
  }
  return nullptr;
}

inline bool Invoke(const MethodInfo* method, Il2CppObject* object, void** args, Il2CppObject*& result)
{
  result = nullptr;
  if (!method || !method->invoker_method) return false;
  Il2CppException* exception = nullptr;
  result = il2cpp_runtime_invoke(method, object, args, &exception);
  return !exception;
}

inline Il2CppObject* Reference(Il2CppObject* object, const char* getter, const char* type)
{
  Il2CppObject* result = nullptr;
  auto* method = Method(object, getter, type);
  return Invoke(method, object, nullptr, result) ? result : nullptr;
}

inline Il2CppObject* ReferenceField(Il2CppObject* object, const char* name, const char* type)
{
  auto* field = object ? il2cpp_class_get_field_from_name(object->klass, name) : nullptr;
  if (!field || !field->type || (field->type->attrs & FIELD_ATTRIBUTE_STATIC)
      || !method_contract::Type(field->type, type))
    return nullptr;
  return il2cpp_field_get_value_object(field, object);
}

template <typename T>
bool Value(Il2CppObject* object, const char* getter, const char* type, T& value)
{
  Il2CppObject* boxed = nullptr;
  if (!Invoke(Method(object, getter, type), object, nullptr, boxed) || !boxed
      || !method_contract::Type(il2cpp_class_get_type(boxed->klass), type)) return false;
  auto* data = il2cpp_object_unbox(boxed);
  if (!data) return false;
  std::memcpy(&value, data, sizeof(value));
  return true;
}

inline bool Select(Il2CppObject* object, const char* name, int64_t id)
{
  auto* method = Method(object, name, "System.Boolean", {"System.Int64"});
  Il2CppObject* boxed = nullptr;
  void* args[] = {&id};
  if (!Invoke(method, object, args, boxed) || !boxed
      || !method_contract::Type(il2cpp_class_get_type(boxed->klass), "System.Boolean")) return false;
  const auto* value = static_cast<bool*>(il2cpp_object_unbox(boxed));
  return value && *value;
}
} // namespace navigation_binding

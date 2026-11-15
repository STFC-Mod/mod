#pragma once

#include <il2cpp/il2cpp_helper.h>

#include <cstdint>
#include <optional>

// Null-safe IL2CPP reads: an empty optional or nullptr instead of 0 when anything is missing.
namespace ManagedRead
{
// property getter on a class or interface
inline const MethodInfo* Getter(Il2CppClass* cls, const char* property)
{
  if (!cls) {
    return nullptr;
  }
  const PropertyInfo* prop = il2cpp_class_get_property_from_name(cls, property);
  return prop ? il2cpp_property_get_get_method(const_cast<PropertyInfo*>(prop)) : nullptr;
}

// getter on the object's runtime class, for members only some implementations have
inline const MethodInfo* RuntimeGetter(void* obj, const char* property)
{
  return obj ? Getter(((Il2CppObject*)obj)->klass, property) : nullptr;
}

// instance call, virtual and interface dispatch included
inline Il2CppObject* Invoke(const MethodInfo* method, void* obj)
{
  if (!method || !obj) {
    return nullptr;
  }
  auto* object = (Il2CppObject*)obj;
  if (method->klass && !il2cpp_class_is_assignable_from(method->klass, object->klass)) {
    return nullptr;
  }
  const MethodInfo* target = il2cpp_object_get_virtual_method(object, method);
  if (!target) {
    return nullptr;
  }
  Il2CppException* exception = nullptr;
  Il2CppObject*    result    = il2cpp_runtime_invoke(target, obj, nullptr, &exception);
  return exception ? nullptr : result;
}

// reference result
inline void* Object(const MethodInfo* getter, void* obj)
{
  return Invoke(getter, obj);
}

// boxed value result; T must match the managed size (uint64_t for DateTime)
template <typename T> std::optional<T> Value(const MethodInfo* getter, void* obj)
{
  Il2CppObject* boxed = Invoke(getter, obj);
  if (!boxed) {
    return std::nullopt;
  }
  auto* data = static_cast<T*>(il2cpp_object_unbox(boxed));
  return data ? std::optional<T>(*data) : std::nullopt;
}

// object stored in an instance field
inline void* FieldObject(void* obj, FieldInfo* field)
{
  if (!obj || !field) {
    return nullptr;
  }
  return *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + field->offset);
}

// DateTime -> unix seconds. The top two bits are the Kind; MinValue (unset) gives nullopt.
inline std::optional<int64_t> DateTimeToUnix(std::optional<uint64_t> date_data)
{
  if (!date_data) {
    return std::nullopt;
  }
  const int64_t ticks = static_cast<int64_t>(*date_data & 0x3FFFFFFFFFFFFFFFull);
  if (ticks <= 0) {
    return std::nullopt;
  }
  return (ticks - 621355968000000000LL) / 10000000LL;
}
} // namespace ManagedRead

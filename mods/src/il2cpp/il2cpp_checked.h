#pragma once

#include "runtime.h"
#include <functional>
#include <il2cpp-tabledefs.h>

#include <cstring>
#include <stdexcept>

// Optional UI features use these checked operations to stop on metadata drift
// or managed exceptions. Arguments are managed references (including null), not
// value types or byrefs. Existing unchecked helper behavior is unchanged.
namespace Il2CppChecked
{
inline Il2CppObject* Invoke(Il2CppObject* object, const char* name, int count = 0, void** args = nullptr)
{
  if (!object)
    return nullptr;
  if (!object->klass || count < 0 || (count && !args))
    throw std::runtime_error("invalid instance invocation");
  auto* method = IL2CppClassHelper(object->klass).GetMethodInfo(name, count);
  if (!method || method->parameters_count != count || !method->invoker_method
      || (method->flags & METHOD_ATTRIBUTE_STATIC) || !method->return_type || method->return_type->byref)
    throw std::runtime_error("missing instance method");
  for (int i = 0; i < count; ++i) {
    auto* parameter = method->parameters ? method->parameters[i] : nullptr;
    auto* expected  = parameter ? il2cpp_class_from_type(parameter) : nullptr;
    auto* argument  = args ? static_cast<Il2CppObject*>(args[i]) : nullptr;
    if (!expected || parameter->byref || il2cpp_class_is_valuetype(expected)
        || (argument && !il2cpp_class_is_assignable_from(expected, argument->klass)))
      throw std::runtime_error("reference argument contract changed");
  }
  Il2CppObject* result = nullptr;
  if (!Il2CppRuntime::TryInvoke(method, object, args, &result))
    throw std::runtime_error("managed invocation failed");
  return result;
}

inline bool Boolean(Il2CppObject* result)
{
  bool value = false;
  if (!Il2CppRuntime::TryBoolean(result, value))
    throw std::runtime_error("expected Boolean");
  return value;
}

inline FieldInfo* Field(Il2CppObject* object, const char* name, const char* expected)
{
  auto* field = object && object->klass ? IL2CppClassHelper(object->klass).GetField(name).get_info() : nullptr;
  auto* type  = field && field->type ? il2cpp_class_from_type(field->type) : nullptr;
  if (!field || !field->type || (field->type->attrs & FIELD_ATTRIBUTE_STATIC) || !type || !type->name
      || std::strcmp(type->name, expected) != 0)
    throw std::runtime_error("field contract changed");
  return field;
}

inline Il2CppObject* ReferenceField(Il2CppObject* object, const char* name, const char* expected)
{
  if (!object)
    return nullptr;
  auto* field = Field(object, name, expected);
  if (field->type->byref || il2cpp_class_is_valuetype(il2cpp_class_from_type(field->type)))
    throw std::runtime_error("expected reference field");
  return il2cpp_field_get_value_object(field, object);
}

inline bool BooleanField(Il2CppObject* object, const char* name)
{ return object && Boolean(il2cpp_field_get_value_object(Field(object, name, "Boolean"), object)); }

} // namespace Il2CppChecked

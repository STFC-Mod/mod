#pragma once

#include "il2cpp_helper.h"
#include <il2cpp-tabledefs.h>

// Mechanics shared by optional runtime features. Signature/overload selection,
// argument storage and feature-specific failure policy remain with the caller.
namespace Il2CppRuntime
{
// The method and argument ABI must already be established by the caller.
// IL2CPP takes references directly, but value/byref arguments as addresses.
// A null target is valid for static methods. Success is independent of whether
// the return value is null (including void methods). Outputs change on success only.
inline bool TryInvoke(const MethodInfo* method, void* target, void** args, Il2CppObject** result = nullptr)
{
  if (!method)
    return false;
  Il2CppException* exception = nullptr;
  auto*            value     = il2cpp_runtime_invoke(method, target, args, &exception);
  if (exception)
    return false;
  if (result)
    *result = value;
  return true;
}

inline bool TryBoolean(Il2CppObject* boxed, bool& value)
{
  if (!boxed || !boxed->klass)
    return false;
  const auto* type = il2cpp_class_get_type(boxed->klass);
  if (!type || type->byref || type->type != IL2CPP_TYPE_BOOLEAN)
    return false;
  auto* data = static_cast<bool*>(il2cpp_object_unbox(boxed));
  if (!data)
    return false;
  value = *data;
  return true;
}
} // namespace Il2CppRuntime

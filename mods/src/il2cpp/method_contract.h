#pragma once

#include "il2cpp-functions.h"
#include <il2cpp-tabledefs.h>
#include <cstring>
#include <initializer_list>

namespace method_contract
{
inline bool Type(const Il2CppType* type, const char* name)
{
  if (!type || type->byref) return false;
  auto* actual = il2cpp_type_get_name(type);
  const bool matches = actual && std::strcmp(actual, name) == 0;
  il2cpp_free(actual);
  return matches;
}

// Resolve the entire managed signature, including static/instance dispatch.
// A renamed, ambiguous or generic method is not a compatible callback.
inline const MethodInfo* Resolve(Il2CppClass* cls, const char* name, bool is_static,
                                 const char* result, std::initializer_list<const char*> parameters)
{
  if (!cls) return nullptr;
  const MethodInfo* found = nullptr;
  void* iterator = nullptr;
  while (auto* method = il2cpp_class_get_methods(cls, &iterator)) {
    if (std::strcmp(method->name, name) != 0 || !method->methodPointer || method->is_generic || method->is_inflated
        || bool(method->flags & METHOD_ATTRIBUTE_STATIC) != is_static
        || method->parameters_count != parameters.size() || !Type(method->return_type, result)) continue;
    bool matches = true;
    unsigned i = 0;
    for (auto* parameter : parameters)
      matches = Type(method->parameters[i++], parameter) && matches;
    if (!matches) continue;
    if (found) return nullptr;
    found = method;
  }
  return found;
}

inline void* Pointer(const MethodInfo* method)
{ return method ? reinterpret_cast<void*>(method->methodPointer) : nullptr; }
} // namespace method_contract

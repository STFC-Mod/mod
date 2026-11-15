// Production helpers with a controlled IL2CPP boundary, on Windows and macOS.
#include <il2cpp-config.h>
#if _WIN32
#undef IL2CPP_IMPORT
#define IL2CPP_IMPORT
#endif
#include "il2cpp/runtime.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
Il2CppClass  klass;
Il2CppType   type{};
Il2CppObject object{};
MethodInfo   method{};
int          calls = 0;
bool         fail = false, boxed = false, null_result = false, null_type = false, null_unbox = false;
void*        seen_target = nullptr;
void**       seen_args   = nullptr;
} // namespace
#if _WIN32
#define API(ret, name, params) extern "C" ret name params
#define END_API
#else
#define API(ret, name, params) name##_t name = +[] params->ret
#define END_API ;
#endif
API(const Il2CppType*, il2cpp_class_get_type, (Il2CppClass*))
{ return null_type ? nullptr : &type; }
END_API
API(void*, il2cpp_object_unbox, (Il2CppObject*))
{ return null_unbox ? nullptr : &boxed; }
END_API
API(Il2CppObject*, il2cpp_runtime_invoke, (const MethodInfo*, void* target, void** args, Il2CppException** error))
{
  ++calls;
  seen_target = target;
  seen_args   = args;
  if (fail)
    *error = reinterpret_cast<Il2CppException*>(&object);
  return null_result ? nullptr : &object;
}
END_API
void Require(bool condition)
{
  if (!condition)
    throw std::runtime_error("runtime helper regression");
}
int main()
{
  Il2CppObject* result = &object;
  Require(!Il2CppRuntime::TryInvoke(nullptr, nullptr, nullptr, &result) && calls == 0 && result == &object);
  bool  value  = true;
  void* args[] = {&value, &object, nullptr};
  Require(Il2CppRuntime::TryInvoke(&method, &object, args, &result));
  Require(seen_target == &object && seen_args == args && seen_args[0] == &value && seen_args[1] == &object);
  // Static calls, null reference/void returns and exceptions remain distinct.
  null_result = true;
  Require(Il2CppRuntime::TryInvoke(&method, nullptr, args, &result) && !result && !seen_target);
  result = &object;
  fail   = true;
  Require(!Il2CppRuntime::TryInvoke(&method, nullptr, args, &result) && result == &object);

  object.klass = &klass;
  type.type    = IL2CPP_TYPE_BOOLEAN;
  Require(Il2CppRuntime::TryBoolean(&object, value) && !value);
  boxed = true;
  Require(Il2CppRuntime::TryBoolean(&object, value) && value);
  type.type = IL2CPP_TYPE_I4;
  Require(!Il2CppRuntime::TryBoolean(&object, value) && value);
  Require(!Il2CppRuntime::TryBoolean(nullptr, value));
  type.type = IL2CPP_TYPE_BOOLEAN;
  type.byref = true;
  Require(!Il2CppRuntime::TryBoolean(&object, value) && value);
  type.byref = false;
  null_type = true;
  Require(!Il2CppRuntime::TryBoolean(&object, value) && value);
  null_type = false;
  null_unbox = true;
  Require(!Il2CppRuntime::TryBoolean(&object, value) && value);
  null_unbox = false;
  object.klass = nullptr;
  Require(!Il2CppRuntime::TryBoolean(&object, value) && value);
  std::cout << "IL2CPP runtime helper regressions passed\n";
}

// Production helpers with a controlled IL2CPP boundary, on Windows and macOS.
#include <il2cpp-config.h>
#if _WIN32
#undef IL2CPP_IMPORT
#define IL2CPP_IMPORT
#endif
#include "il2cpp/il2cpp_helper.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace
{
Il2CppClass  klass;
Il2CppObject object{};
MethodInfo   method{};
int          stage = 3;
} // namespace
#if _WIN32
#define API(ret, name, params) extern "C" ret name params
#define END_API
#else
#define API(ret, name, params) name##_t name = +[] params->ret
#define END_API ;
#endif
API(Il2CppDomain*, il2cpp_domain_get, ())
{ return stage >= 0 ? reinterpret_cast<Il2CppDomain*>(&object) : nullptr; }
END_API
API(const Il2CppAssembly*, il2cpp_domain_assembly_open, (Il2CppDomain * domain, const char*))
{
  if (!domain)
    std::abort();
  return stage >= 1 ? reinterpret_cast<Il2CppAssembly*>(&object) : nullptr;
}
END_API
API(const Il2CppImage*, il2cpp_assembly_get_image, (const Il2CppAssembly* assembly))
{
  if (!assembly)
    std::abort();
  return stage >= 2 ? reinterpret_cast<Il2CppImage*>(&object) : nullptr;
}
END_API
API(Il2CppClass*, il2cpp_class_from_name, (const Il2CppImage* image, const char*, const char*))
{
  if (!image)
    std::abort();
  return stage >= 3 ? &klass : nullptr;
}
END_API
API(const MethodInfo*, il2cpp_class_get_method_from_name, (Il2CppClass * cls, const char*, int))
{
  if (!cls)
    std::abort();
  return &method;
}
END_API
void Require(bool condition)
{
  if (!condition)
    throw std::runtime_error("class lookup regression");
}
int main()
{
  for (stage = -1; stage < 3; ++stage)
    Require(!il2cpp_get_class_helper("Assembly", "Namespace", "Class").get_cls());
  Require(il2cpp_get_class_helper("Assembly", "Namespace", "Class").get_cls() == &klass);
  Require(!IL2CppClassHelper(nullptr).GetMethodInfo("Method", 0));
  Require(IL2CppClassHelper(&klass).GetMethodInfo("Method", 0) == &method);

  std::cout << "IL2CPP class lookup regressions passed\n";
}

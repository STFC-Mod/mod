// Binds the IL2CPP exports from the loaded GameAssembly image on macOS. Windows links them through GameAssembly.lib.

#include <il2cpp/il2cpp.h>
#include <il2cpp/hook.h>

#include <stdexcept>

#if !_WIN32
#include <dlfcn.h>
extern "C" {
#define DO_API(r, n, p) n##_t n;
#define DO_API_NO_RETURN(r, n, p) DO_API(r, n, p)
#include "il2cpp-api-functions.h"
#undef DO_API
#undef DO_API_NO_RETURN
}
#endif

#if __APPLE__
namespace
{
void Runtime_Shutdown_Hook(auto original)
{
  il2cpp::GcHandle::StopRuntime();
  il2cpp::Report("[IL2CPP] runtime shutting down; native GC handles disabled");
  original();
}

void* RuntimeShutdownBody(void* entry)
{
  if (!entry)
    return nullptr;
  const auto* code = static_cast<const uint8_t*>(entry);
#if defined(__aarch64__) || defined(__arm64__)
  uint32_t instruction;
  std::memcpy(&instruction, code, sizeof(instruction));
  if ((instruction & 0xfc000000u) != 0x14000000u)
    return nullptr;
  int64_t displacement = instruction & 0x03ffffffu;
  if (displacement & 0x02000000)
    displacement -= 0x04000000;
  return reinterpret_cast<void*>(reinterpret_cast<intptr_t>(entry) + displacement * 4);
#elif __x86_64__
  // The Intel export has a balanced frame prologue before its tail jump, so it is not a lone-jump thunk.
  constexpr uint8_t prefix[] = {0x55, 0x48, 0x89, 0xe5, 0x5d, 0xe9};
  if (std::memcmp(code, prefix, sizeof(prefix)) != 0)
    return nullptr;
  int32_t displacement;
  std::memcpy(&displacement, code + sizeof(prefix), sizeof(displacement));
  return reinterpret_cast<void*>(reinterpret_cast<intptr_t>(entry) + 10 + displacement);
#else
  return nullptr;
#endif
}
} // namespace
#endif

void il2cpp::Initialize(void* assembly)
{
#if !_WIN32
  if (!assembly)
    throw std::runtime_error("GameAssembly is not loaded");
  // Resolve before installing any detours. Keep the image loaded for the process lifetime.
#define DO_API(r, n, p) n = reinterpret_cast<n##_t>(dlsym(assembly, #n));
#define DO_API_NO_RETURN(r, n, p) DO_API(r, n, p)
#include <il2cpp-api-functions.h>
#undef DO_API
#undef DO_API_NO_RETURN
  // The exports the libraries call. Failing startup here beats calling through null on first use.
#define REQUIRE_API(name)                                                                                              \
  if (!name)                                                                                                           \
    throw std::runtime_error("missing IL2CPP export: " #name);
  REQUIRE_API(il2cpp_init)
  REQUIRE_API(il2cpp_free)
  REQUIRE_API(il2cpp_get_corlib)
  REQUIRE_API(il2cpp_domain_get)
  REQUIRE_API(il2cpp_domain_assembly_open)
  REQUIRE_API(il2cpp_assembly_get_image)
  REQUIRE_API(il2cpp_class_from_name)
  REQUIRE_API(il2cpp_class_from_type)
  REQUIRE_API(il2cpp_class_get_type)
  REQUIRE_API(il2cpp_class_get_name)
  REQUIRE_API(il2cpp_class_get_namespace)
  REQUIRE_API(il2cpp_class_get_declaring_type)
  REQUIRE_API(il2cpp_class_get_parent)
  REQUIRE_API(il2cpp_class_get_methods)
  REQUIRE_API(il2cpp_class_get_property_from_name)
  REQUIRE_API(il2cpp_class_get_field_from_name)
  REQUIRE_API(il2cpp_class_get_nested_types)
  REQUIRE_API(il2cpp_class_is_enum)
  REQUIRE_API(il2cpp_class_is_valuetype)
  REQUIRE_API(il2cpp_class_is_assignable_from)
  REQUIRE_API(il2cpp_class_value_size)
  REQUIRE_API(il2cpp_type_get_name)
  REQUIRE_API(il2cpp_type_get_object)
  REQUIRE_API(il2cpp_property_get_get_method)
  REQUIRE_API(il2cpp_property_get_set_method)
  REQUIRE_API(il2cpp_object_get_virtual_method)
  REQUIRE_API(il2cpp_object_new)
  REQUIRE_API(il2cpp_object_unbox)
  REQUIRE_API(il2cpp_runtime_invoke)
  REQUIRE_API(il2cpp_format_exception)
  REQUIRE_API(il2cpp_runtime_class_init)
  REQUIRE_API(il2cpp_field_set_value)
  REQUIRE_API(il2cpp_field_static_get_value)
  REQUIRE_API(il2cpp_field_static_set_value)
  REQUIRE_API(il2cpp_array_new)
  REQUIRE_API(il2cpp_gc_wbarrier_set_field)
  REQUIRE_API(il2cpp_gchandle_new)
  REQUIRE_API(il2cpp_gchandle_new_weakref)
  REQUIRE_API(il2cpp_gchandle_get_target)
  REQUIRE_API(il2cpp_gchandle_free)
  REQUIRE_API(il2cpp_resolve_icall)
  REQUIRE_API(il2cpp_string_new_len)
  REQUIRE_API(il2cpp_value_box)
  // The object tracker detours it to forget objects the GC frees without OnDestroy.
  REQUIRE_API(il2cpp_unity_liveness_finalize)
#undef REQUIRE_API
#else
  (void)assembly;
#endif
#if __APPLE__
  // Hook the substantive Runtime::Shutdown body, never its undersized API wrapper. Native extents for both
  // macOS slices are recorded in docs/IL2CPP_SHUTDOWN_VALIDATION.md; SPUD also validates the resolved target.
  auto* shutdown = RuntimeShutdownBody(reinterpret_cast<void*>(il2cpp_shutdown));
  if (!shutdown)
    throw std::runtime_error("unrecognized il2cpp_shutdown wrapper; cannot protect native GC handle teardown");
  il2cpp::HookSet hooks("RuntimeShutdown");
  IL2CPP_STAGE_AT(hooks, shutdown, Runtime_Shutdown_Hook);
  if (!hooks.Commit())
    throw std::runtime_error("failed to protect native GC handles during runtime shutdown");
#endif
}

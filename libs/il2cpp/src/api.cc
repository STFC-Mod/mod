#include <il2cpp/api.h>

#include <stdexcept>

#if !_WIN32
#include <dlfcn.h>
#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus
#define DO_API(r, n, p) n##_t n;
#define DO_API_NO_RETURN(r, n, p) DO_API(r, n, p)
#include "il2cpp-api-functions.h"
#undef DO_API
#undef DO_API_NO_RETURN
#if defined(__cplusplus)
}
#endif // __cplusplus
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
  REQUIRE_API(il2cpp_class_get_method_from_name)
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
#undef REQUIRE_API
#else
  (void)assembly;
#endif
}

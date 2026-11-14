#pragma once

#ifdef _WIN32
#pragma comment(lib, "GameAssembly.lib")
#endif

#include <il2cpp-api-types.h>
#include <il2cpp-class-internals.h>
#include <il2cpp-config.h>
#include <il2cpp-object-internals.h>

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus
#if _WIN32
#define DO_API(r, n, p) IL2CPP_IMPORT r n p;
#define DO_API_NO_RETURN(r, n, p) IL2CPP_IMPORT NORETURN r n p;
#else
#define DO_API(r, n, p)                                                                                                \
  using n##_t = r(*) p;                                                                                                \
  extern n##_t n;
#define DO_API_NO_RETURN(r, n, p) DO_API(r, n, p)
#endif
#include "il2cpp-api-functions.h"
#undef DO_API
#undef DO_API_NO_RETURN
#if defined(__cplusplus)
}
#endif // __cplusplus

namespace il2cpp
{
// Bind macOS exports from the already loaded GameAssembly image. Windows uses imports.
void Initialize(void* assembly);
} // namespace il2cpp

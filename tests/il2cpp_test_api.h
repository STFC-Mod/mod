#pragma once

#include <il2cpp-config.h>
#if _WIN32
#undef IL2CPP_IMPORT
#define IL2CPP_IMPORT
#endif
#include <il2cpp/il2cpp.h>

// The library uses Windows imports. Supply both the function and its IAT slot
// so tests exercise the production archive without loading GameAssembly.dll.
#if _WIN32
#define API(ret, name, params)                                                                                         \
  extern "C" ret name params;                                                                                          \
  extern "C" {                                                                                                         \
  ret(*__imp_##name) params = name;                                                                                    \
  }                                                                                                                    \
  extern "C" ret name params
#define END_API
#else
#define API(ret, name, params) name##_t name = +[] params->ret
#define END_API ;
#endif

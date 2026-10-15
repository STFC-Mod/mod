#pragma once

// Build systems set this for the library and its consumers. Direct builds
// retain unwind support unless explicitly disabled.
#ifndef SPUD_UNWIND_SUPPORT
#define SPUD_UNWIND_SUPPORT 1
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define SPUD_OS_IOS 1
// Catalyst is the technology that allows running iOS apps on macOS. These
// builds are both OS_IOS and OS_IOS_MACCATALYST.
#if defined(TARGET_OS_MACCATALYST) && TARGET_OS_MACCATALYST
#define SPUD_OS_IOS_MACCATALYST 1
#endif // defined(TARGET_OS_MACCATALYST) && TARGET_OS_MACCATALYST
#else
#define SPUD_OS_MAC 1
#endif // defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#elif defined(__linux__)
#define SPUD_OS_LINUX 1
#elif defined(_WIN32)
#define SPUD_OS_WIN 1
#else
#error "Add support for this platform"
#endif

#if defined(SPUD_OS_MAC) || defined(SPUD_OS_IOS)
#define SPUD_OS_APPLE 1
#endif

#if defined(_M_IX86) || defined(__i386__)
#define SPUD_ARCH_X86_FAMILY 1
#define SPUD_ARCH_X86 1
#define SPUD_ARCH_32_BITS 1
#define SPUD_ARCH_LITTLE_ENDIAN 1
#elif defined(_M_X64) || defined(__x86_64__)
#define SPUD_ARCH_X86_FAMILY 1
#define SPUD_ARCH_X86_64 1
#define SPUD_ARCH_64_BITS 1
#define SPUD_ARCH_LITTLE_ENDIAN 1
#elif defined(__ARMEL__)
#define SPUD_ARCH_ARM_FAMILY 1
#define SPUD_ARCH_ARMEL 1
#define SPUD_ARCH_32_BITS 1
#define SPUD_ARCH_LITTLE_ENDIAN 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define SPUD_ARCH_ARM_FAMILY 1
#define SPUD_ARCH_ARM64 1
#if defined(__arm64e__)
// arm64e signs code pointers; spud cannot emit a validly signed stub.
#define SPUD_ARCH_ARM64E 1
#endif
#define SPUD_ARCH_64_BITS 1
#define SPUD_ARCH_LITTLE_ENDIAN 1
#else
#error "Add support for this architecture"
#endif

namespace spud {

enum class Arch {
  kX86_64 = 0,
  kX86,
  kArm64,
  kCount,
  kHost =
#if defined(SPUD_ARCH_X86_64)
      Arch::kX86_64
#elif defined(SPUD_ARCH_X86)
      Arch::kX86
#elif defined(SPUD_ARCH_ARM64)
      Arch::kArm64
#endif
};

} // namespace spud

#include <spud/arch.h>
#include <spud/diagnostics.h>
#include <spud/utils.h>

#include "memory/executable_pool.h"
#include "memory/safe_patch.h"

#include <atomic>
#include <cerrno>

#ifdef SPUD_OS_WIN
#include <windows.h>
#elif SPUD_OS_APPLE
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#elif SPUD_OS_LINUX
// No additional includes needed
#else
#error "Unsupported platform"
#endif

namespace spud {
namespace {
#if SPUD_OS_APPLE
thread_local int g_jit_write_depth = 0;
#endif

thread_local int g_last_os_error_tls = 0;
thread_local failure_detail g_failure_tls;
std::atomic<int> g_last_os_error_any{0};
#if SPUD_UNWIND_SUPPORT
std::atomic<bool> g_unwind_support{true};
#endif
} // namespace

void set_unwind_support(bool enabled) {
#if SPUD_UNWIND_SUPPORT
  g_unwind_support.store(enabled, std::memory_order_relaxed);
#else
  (void)enabled;
#endif
}

bool unwind_support_enabled() {
#if SPUD_UNWIND_SUPPORT
  return g_unwind_support.load(std::memory_order_relaxed);
#else
  return false;
#endif
}

int last_os_error() {
  return g_last_os_error_tls;
}

failure_detail last_failure_detail() {
  return g_failure_tls;
}

namespace detail {

void note_os_error(int code) {
  g_last_os_error_tls = code;
  g_last_os_error_any.store(code, std::memory_order_relaxed);
}

void note_current_os_error() {
#ifdef SPUD_OS_WIN
  note_os_error(static_cast<int>(GetLastError()));
#else
  note_os_error(errno);
#endif
}

void clear_os_error() {
  g_last_os_error_tls = 0;
  g_failure_tls = {};
}

bool note_failure(const char *reason, uintptr_t address) {
  if (g_failure_tls.reason == nullptr)
    g_failure_tls = {reason, address};
  return false;
}

int last_os_error_any() {
  return g_last_os_error_any.load(std::memory_order_relaxed);
}

} // namespace detail

void *alloc_executable_memory(size_t size) {
  return detail::executable_pool::instance().allocate(size);
}

void *alloc_executable_memory(size_t size, uintptr_t hint) {
  return detail::executable_pool::instance().allocate_near(size, hint);
}

void free_executable_memory(void *ptr, size_t size) {
  detail::executable_pool::instance().deallocate(ptr, size);
}

jit_write_scope::jit_write_scope() {
#if SPUD_OS_APPLE
  if (g_jit_write_depth++ == 0) {
    pthread_jit_write_protect_np(0);
  }
#endif
}

jit_write_scope::~jit_write_scope() {
#if SPUD_OS_APPLE
  if (g_jit_write_depth > 0 && --g_jit_write_depth == 0) {
    pthread_jit_write_protect_np(1);
  }
#endif
}

// After a write to executable memory, call this before any thread may fetch
// those bytes as instructions. Do it after the protection transition that makes
// the page executable. On Apple this is sys_icache_invalidate; on Windows,
// FlushInstructionCache; on GCC/Clang targets, __builtin___clear_cache.
bool flush_cache(void *ptr, size_t size) {
  if (ptr == nullptr || size == 0) {
    return true;
  }
#if SPUD_OS_APPLE
  sys_dcache_flush(ptr, size);
  sys_icache_invalidate(ptr, size);
  return true;
#elif SPUD_OS_WIN
  return FlushInstructionCache(GetCurrentProcess(), ptr, size) != 0;
#else
  auto *begin = static_cast<char *>(ptr);
  __builtin___clear_cache(begin, begin + size);
  return true;
#endif
}

} // namespace spud

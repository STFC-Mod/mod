#pragma once

#include <cstddef>
#include <cstdint>

namespace spud {

// errno, GetLastError, or a kern_return_t from the most recent failed OS
// call on the calling thread during a spud operation. 0 when the operation
// succeeded or failed above the OS (kDecodeFailed, kInsufficientBytes, ...).
// Cleared at the start of each install/uninstall, inspect_detour,
// query_memory, read_memory, write_memory and protect_memory call, so a
// stale code cannot attach to a later result. A lookup that finds nothing
// mapped reports what the OS gives for an unmapped address (ENOMEM on Linux),
// and a read of a page that cannot be made readable what it gives for an access
// fault (EFAULT on Linux). A failed protection_scope records its code too
// but does not clear an earlier one. Prefer this over
// patch_stats::last_os_error when more than one thread patches at a time.
int last_os_error();

// What the calling thread's most recent failed install or inspect_detour
// refused, past its detour_error: a short stable token such as
// "function_too_short" or "unknown_language_handler", and the address it
// concerns (an instruction, the end of the function, a language handler or
// a table). Both are empty when spud has nothing more specific. The reason
// has static storage. Cleared with last_os_error(), and again when an
// install, removal or inspect_detour succeeds.
struct failure_detail {
  const char *reason = nullptr;
  uintptr_t address = 0;
};

failure_detail last_failure_detail();

// Process-wide counters accumulated over every patch since the process
// started. Relaxed atomics; read them after install/uninstall has returned.
// `last_os_error` is the most recent code any thread recorded, from any
// spud operation.
struct patch_stats {
  uint64_t suspend_attempts = 0;
  uint64_t suspend_engaged = 0;
  uint64_t suspend_fallback = 0;
  uint64_t ip_redirects = 0;
  int last_os_error = 0;
};

patch_stats query_patch_stats();

struct pool_stats {
  size_t live_bytes = 0;
  size_t reserved_bytes = 0;
  size_t free_bytes = 0;
  size_t alloc_failures = 0;
  // Freed blocks the pool could not take back and leaked: the free list
  // was full, or the free came inside a patch while the pool was busy.
  size_t dropped_bytes = 0;
};

pool_stats query_pool_stats();

namespace detail {

// Drops the calling thread's code and failure detail at the start of a
// public operation; the header-only pointer hooks call it.
void clear_os_error();

// Records why an operation refused, unless a refusal is already recorded
// since the last clear: the first one is the innermost and most specific.
// Returns false so a refusal site can `return note_failure(...)`.
bool note_failure(const char *reason, uintptr_t address = 0);

} // namespace detail

} // namespace spud

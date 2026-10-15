#pragma once

#include <spud/arch.h>
#include <spud/utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace spud::detail {

// Records the OS code behind a failed syscall on this thread and updates
// the process-wide snapshot.
void note_os_error(int code);
// Records errno / GetLastError from the call that just failed.
void note_current_os_error();
// Process-wide snapshot for query_patch_stats.
int last_os_error_any();
// Warms the cached memory page size. Call once outside the suspend window
// before patching so the window itself never queries the OS for it.
void prewarm_memory_page_size();

// Maps an offset in the original code to its offset in the trampoline.
// Entries are (original_offset, delta_before_this_instruction), sorted by
// original offset; a following entry at the instruction's end carries the
// post-expansion delta so lookups after the last rewritten instruction still
// work. The last entry at or before `address` applies.
inline uintptr_t relocated_location_at(
    std::span<const std::pair<uintptr_t, uintptr_t>> relocation_offsets,
    uintptr_t address) {
  const auto next = std::upper_bound(
      relocation_offsets.begin(), relocation_offsets.end(), address,
      [](uintptr_t value, const auto &entry) { return value < entry.first; });
  return (next == relocation_offsets.begin() ? 0 : std::prev(next)->second) +
         address;
}

// Counts how many suspend windows the calling thread is inside. While nonzero,
// allocation and locking are forbidden: a suspended thread may hold the very
// lock or heap arena we would need, which would deadlock. Debug builds assert
// this in the executable pool and context factories.
inline thread_local int g_suspend_window_depth = 0;

#ifndef NDEBUG
#define SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW()                                   \
  assert(::spud::detail::g_suspend_window_depth == 0 &&                        \
         "allocation or locking inside the suspend window")
#else
#define SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW() ((void)0)
#endif

// Test-only override (see tests): forces every session to take the unguarded
// patch path so the fallback stays covered even where suspension works.
extern std::atomic<bool> g_force_unguarded_patch_for_tests;
// Test-only override (Linux, see tests): skips the ptrace stop and exercises
// the signal-based fallback directly, even where ptrace works.
extern std::atomic<bool> g_force_signal_stop_for_tests;
// Reserved real-time signal the Linux signal stop uses. Tests install their
// own handler on it to check chaining; 0 on other platforms.
int stop_signal_for_tests();

// When false (the default), a failed suspend aborts the patch instead of
// writing bytes while other threads run. Set true only when the caller
// accepts a torn write.
extern std::atomic<bool> g_allow_unguarded_patch;

inline bool unguarded_patch_permitted() {
  return g_allow_unguarded_patch.load(std::memory_order_relaxed) ||
         g_force_unguarded_patch_for_tests.load(std::memory_order_relaxed);
}

// Test-only: force suspension, redirection, or remapping to fail so rollback is
// covered on hosts where the real OS path cannot be injected.
enum class patch_fault : uint8_t {
  none = 0,
  suspend = 1,
  redirect = 2,
  remap = 3,
};
extern std::atomic<uint8_t> g_inject_patch_fault_for_tests;

// Test-only instrumentation (see tests): cumulative suspend outcomes.
// Relaxed atomics; tests read before/after deltas once patching is quiesced.
struct suspend_stats {
  std::atomic<uint64_t> attempts{0};
  std::atomic<uint64_t> engaged{0};
  std::atomic<uint64_t> fallback{0};
  std::atomic<uint64_t> redirects{0};
};
extern suspend_stats g_suspend_stats_for_tests;

struct ip_range {
  uintptr_t base = 0;
  size_t size = 0;
};

// Upper bound for waiting on in-flight hook executions after uninstall
// unlinks a hook. What has not drained by then is retired, not freed.
inline constexpr uint64_t kQuiesceTimeoutNs = 5000000000ULL;

// Test override: nonzero replaces kQuiesceTimeoutNs.
extern std::atomic<uint64_t> g_quiesce_timeout_ns_for_tests;

inline uint64_t quiesce_timeout_ns() {
  const auto override_ns =
      g_quiesce_timeout_ns_for_tests.load(std::memory_order_relaxed);
  return override_ns != 0 ? override_ns : kQuiesceTimeoutNs;
}

// Window-safe monotonic clock (vDSO / QPC / mach_absolute_time): no locks,
// no allocation. Used to bound quiescence waits.
uint64_t monotonic_ns() noexcept;

// Warms any one-time clock state. Called by the session constructor (outside
// the window) so the window itself never triggers lazy initialization.
void prewarm_monotonic_clock() noexcept;

// Suspends all other threads of the process so executable bytes can be
// patched without tearing, and redirects instruction pointers caught inside
// overwritten regions to their relocated counterparts.
//
// Threads are enumerated before any stop: in the constructor on Windows and
// macOS, at the start of suspend() on Linux. After the stop, Linux re-reads
// /proc/self/task with getdents64 (no malloc) and macOS calls task_threads,
// stopping anyone new until a pass finds nobody; stopped threads cannot
// start more. Windows does not re-enumerate yet, so a thread created between
// its snapshot and its creator's suspension is a residual race. The thread
// lists are reserved before the stop so they never grow inside the window;
// a burst beyond the reserve fails the stop. A live enumerated thread that
// cannot be opened, suspended, or have its instruction pointer rewritten
// fails the operation instead of proceeding with a partial stop.
//
// Phase rules (see the suspend-window contract above):
// - The constructor, and suspend() up to the first stop, may allocate.
// - Once threads are stopped, suspend(), redirect_ips(), any_ip_in_ranges()
//   and resume() are syscall-only: no allocation, no locks, no I/O.
// - The destructor resumes (if suspended) and releases enumeration handles,
//   so cleanup happens outside the window.
class safe_patch_session {
public:
  safe_patch_session();
  ~safe_patch_session();

  safe_patch_session(const safe_patch_session &) = delete;
  safe_patch_session &operator=(const safe_patch_session &) = delete;

  // Suspends all other threads. Returns false when suspension is unavailable.
  // Linux cannot ptrace a sibling in the same thread group (EPERM).
  // A cloned tracer shares the address space but not the thread group, and
  // that tracer is the one that attaches and rewrites IPs. When the attach
  // fails (a debugger, strace, rr, Yama, or a container), Linux falls back
  // to stopping the siblings with a reserved real-time signal instead.
  bool suspend();
  // Resumes threads suspended by suspend(). No-op unless suspended.
  void resume();
  bool suspended() const {
    return suspended_;
  }
  bool nested() const {
    return owner_ != nullptr;
  }

  // Rewrites every suspended IP inside [base, base + size) to
  // tramp_base + relocated_location_at(relocation_offsets, ip - base).
  // `size` covers the entire copied span: a thread past the landing bytes
  // can still take a back edge into them. An empty map is the identity
  // mapping (byte-copied stubs).
  // to_entry sends every IP in range to tramp_base (used when the
  // overwritten bytes are a relative landing pad that cannot be copied).
  //
  // Returns false when a live thread's instruction pointer could not be
  // read or rewritten. The caller must not overwrite those bytes.
  bool redirect_ips(
      uintptr_t base, size_t size, uintptr_t tramp_base,
      const std::vector<std::pair<uintptr_t, uintptr_t>> &relocation_offsets,
      bool to_entry = false);

  // True when any suspended thread has its IP inside one of the ranges.
  // Always false unless suspended. Used to quiesce ranges that cannot be
  // remapped (bytes being restored or unmapped).
  bool any_ip_in_ranges(const ip_range *ranges, size_t count);
  // Includes stack return addresses, ARM64 LR, and ARM64 x16, which holds
  // the destination of a chain's indirect jump stopped before its branch.
  // Stack scanning is conservative: ambiguous pointers or an unreadable
  // stack keep it busy.
  bool any_return_in_ranges(const ip_range *ranges, size_t count);
  bool any_own_return_in_ranges(const ip_range *ranges, size_t count) const;

private:
  struct PlatformState;
  PlatformState *state_ = nullptr;
  safe_patch_session *owner_ = nullptr;
  bool prepared_ = false;
  bool suspended_ = false;
  // Captured before the stop. Walking unwind tables while sibling threads
  // are frozen could wait on a lock held by one of them.
  std::array<uintptr_t, 256> own_returns_{};
  size_t own_return_count_ = 0;
  bool own_trace_complete_ = false;
};

// Serializes whole install/uninstall operations. This one atomic is the
// only synchronization in the patch path: no mutexes, no OS locks, so there
// is no lock a suspended thread could hold that would deadlock us. Suspension
// is process-wide anyway, so one global flag loses nothing over striping.
//
// A holder keeps it for a whole stop of every other thread, 0.2 to several
// ms, so waiters spin briefly and then sleep in atomic wait (a futex,
// WaitOnAddress or ulock) instead of burning a core in a yield loop. The
// flag is never taken inside a suspend window, and the release and its
// notify happen after the window has closed.
class patch_flag_guard {
public:
  patch_flag_guard() {
    if (held_depth_ > 0) {
      ++held_depth_;
      return;
    }
    auto &held = flag();
    int spins = 0;
    while (held.exchange(1, std::memory_order_acquire) != 0) {
      if (++spins < 100) {
#if defined(_MSC_VER) && defined(_M_X64)
        _mm_pause();
#elif defined(SPUD_ARCH_X86_FAMILY)
        __builtin_ia32_pause();
#endif
      } else {
        held.wait(1, std::memory_order_relaxed);
      }
    }
    ++held_depth_;
  }

  ~patch_flag_guard() {
    --held_depth_;
    if (held_depth_ == 0) {
      flag().store(0, std::memory_order_release);
      flag().notify_one();
    }
  }

  patch_flag_guard(const patch_flag_guard &) = delete;
  patch_flag_guard &operator=(const patch_flag_guard &) = delete;

private:
  static std::atomic<int> &flag() {
    static std::atomic<int> flag{0};
    return flag;
  }

  static inline thread_local int held_depth_ = 0;
};

inline bool patch_can_commit(const safe_patch_session &session) {
  return session.suspended() || unguarded_patch_permitted();
}

} // namespace spud::detail

// Opens a suspend window for the following patch. The session resumes all
// threads when it goes out of scope, including on early return.
#define SPUD_SCOPED_SAFE_PATCH(name)                                           \
  ::spud::detail::safe_patch_session name{};                                   \
  static_cast<void>(name.suspend())

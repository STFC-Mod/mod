#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "memory/safe_patch.h"
#include <spud/arch.h>
#include <spud/diagnostics.h>
#include <spud/memory.h>

#include "readable.h"

#ifdef SPUD_OS_WIN
#include <windows.h>

#include <tlhelp32.h>
#elif defined(SPUD_OS_APPLE)
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_act.h>
#include <mach/vm_map.h>
#include <sched.h>
#include <sys/sysctl.h>
#elif defined(SPUD_OS_LINUX)
#include <cstddef>
#include <cstdint>
#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <new>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif
#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif
#endif

#include <array>
#include <cerrno>
#include <cstring>
#include <optional>
#if !SPUD_OS_WIN
#include <unwind.h>
#endif

namespace spud::detail {

std::atomic<bool> g_force_unguarded_patch_for_tests{false};
std::atomic<bool> g_force_signal_stop_for_tests{false};
std::atomic<bool> g_allow_unguarded_patch{false};
std::atomic<uint64_t> g_quiesce_timeout_ns_for_tests{0};
std::atomic<uint8_t> g_inject_patch_fault_for_tests{0};
suspend_stats g_suspend_stats_for_tests;
thread_local safe_patch_session *g_active_session = nullptr;

namespace {

bool inject_fault(patch_fault kind) {
  return g_inject_patch_fault_for_tests.load(std::memory_order_relaxed) ==
         static_cast<uint8_t>(kind);
}

// Each platform's suspend path records its own OS error where the call
// failed; errno or GetLastError is stale by the time this runs.
void note_fallback() {
  g_suspend_stats_for_tests.fallback.fetch_add(1, std::memory_order_relaxed);
}

// Where a thread stopped at `ip` resumes so it never runs the bytes being
// overwritten, or nullopt to leave it where it is. Counts the redirect.
// See safe_patch_session::redirect_ips for to_entry.
std::optional<uintptr_t>
remap_ip(uintptr_t ip, uintptr_t base, size_t size, uintptr_t tramp_base,
         const std::vector<std::pair<uintptr_t, uintptr_t>> &map,
         bool to_entry) {
  if (ip < base || ip >= base + size) {
    return std::nullopt;
  }
  g_suspend_stats_for_tests.redirects.fetch_add(1, std::memory_order_relaxed);
  if (to_entry) {
    return tramp_base;
  }
  // An empty map is the identity mapping (byte-copied stubs).
  return tramp_base + relocated_location_at(map, ip - base);
}

// The registers a stopped thread is checked by. `lr` and `ip0` (x16) are
// zero on x86-64.
struct stopped_regs {
  uintptr_t ip = 0;
  uintptr_t sp = 0;
  uintptr_t lr = 0;
  uintptr_t ip0 = 0;
};

bool ip_in_ranges(uintptr_t ip, const ip_range *ranges, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (ip >= ranges[i].base && ip < ranges[i].base + ranges[i].size) {
      return true;
    }
  }
  return false;
}

// `ip0` is x16 on ARM64: a hook chain's indirect jump loads its destination
// there one instruction before branching, so a thread stopped in between is
// about to enter that code. Zero on x86-64, whose jump is one instruction.
bool pending_return_in(const stopped_regs &regs, const ip_range *ranges,
                       size_t count) {
  auto in = [&](uintptr_t value) {
#if defined(SPUD_ARCH_ARM64)
    // Saved return addresses can carry a PAC. Matching the low address
    // bits conservatively can delay removal, but cannot miss a signed LR.
    constexpr uintptr_t kAddressMask = 0x0000FFFFFFFFFFFFull;
    for (size_t i = 0; i < count; ++i) {
      // Mask both ends: 52-bit VAs can have real address bits where another
      // process configuration places the PAC. Collisions only keep it busy.
      if (((value - ranges[i].base) & kAddressMask) < ranges[i].size) {
        return true;
      }
    }
    return false;
#else
    return ip_in_ranges(value, ranges, count);
#endif
  };
  if ((regs.lr != 0 && in(regs.lr)) || (regs.ip0 != 0 && in(regs.ip0))) {
    return true;
  }
  uintptr_t sp = regs.sp;
#if defined(SPUD_ARCH_ARM64)
  sp &= 0x00FFFFFFFFFFFFFFull;
#endif
  // A stack whose readable extent exceeds the budget is unproven. Refuse
  // rather than scan only part of it and miss an older active frame.
  // Adjacent default 8MB pthread stacks and anonymous mappings can share
  // one VMA. Keep the complete readable extent, up to a 16MB budget.
  constexpr size_t kStackLimit = 16 * 1024 * 1024;
  const auto region = find_region(sp);
  const size_t size =
      region && has_protection(region->protection, mem_protection::kRead)
          ? region->size - (sp - region->start)
          : 0;
  if (size == 0) {
    return true;
  }
  std::array<uintptr_t, 8192> words{};
  for (size_t off = 0; off < size;) {
    const size_t length = std::min(sizeof(words), size - off);
    const size_t copied = read_code_prefix(
        sp + off, {reinterpret_cast<uint8_t *>(words.data()), length});
    if (copied == 0) {
      return true;
    }
    for (size_t i = 0; i < copied / sizeof(uintptr_t); ++i) {
      if (in(words[i])) {
        return true;
      }
    }
    // Linux lightweight guards can split stacks without splitting the VMA.
    // The first unreadable page above SP bounds this active stack too.
    if (copied < length) {
      return false;
    }
    off += length;
    if (off >= kStackLimit && off < size) {
      return true;
    }
  }
  return false;
}

// Whether a stopped thread is inside `ranges`, or with returns_only, will
// return or jump into them.
bool stopped_in(const stopped_regs &regs, const ip_range *ranges, size_t count,
                bool returns_only) {
  return returns_only ? pending_return_in(regs, ranges, count)
                      : ip_in_ranges(regs.ip, ranges, count);
}

} // namespace

uint64_t monotonic_ns() noexcept {
#ifdef SPUD_OS_WIN
  static LARGE_INTEGER frequency = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f;
  }();
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<uint64_t>(now.QuadPart * 1000000000ULL /
                               static_cast<uint64_t>(frequency.QuadPart));
#elif defined(SPUD_OS_APPLE)
  static mach_timebase_info_data_t timebase = [] {
    mach_timebase_info_data_t t;
    mach_timebase_info(&t);
    return t;
  }();
  return mach_absolute_time() * timebase.numer / timebase.denom;
#elif defined(SPUD_OS_LINUX)
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
#endif
}

void prewarm_monotonic_clock() noexcept {
  (void)monotonic_ns();
}

#if !defined(SPUD_OS_LINUX)
int stop_signal_for_tests() {
  return 0;
}
#endif

#ifdef SPUD_OS_LINUX
namespace {

bool xfer_all(int fd, void *buf, size_t n, bool sending) {
  auto *p = static_cast<uint8_t *>(buf);
  size_t off = 0;
  while (off < n) {
    const ssize_t r =
        sending ? write(fd, p + off, n - off) : read(fd, p + off, n - off);
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (r == 0) {
      return false;
    }
    off += static_cast<size_t>(r);
  }
  return true;
}

bool send_byte(int fd, uint8_t b) {
  return xfer_all(fd, &b, 1, true);
}

bool recv_byte(int fd, uint8_t *b) {
  return xfer_all(fd, b, 1, false);
}

// getdents64 so a freeze-window re-scan does not call opendir/malloc.
struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

// The tracer needs little stack (a 64KB scan buffer is its largest frame),
// but a fresh mapping costs a page fault, and a 2MB-aligned one a whole
// transparent huge page zeroed on every session (about 100us). One stack is
// kept for the process; patching is serialized, so a second concurrent
// session is rare and maps its own.
//
// The stack keeps stale copies of sibling stacks from earlier scans. It is
// fenced with PROT_NONE so it never joins a neighbouring thread stack's
// mapping (glibc's stacks share our flags and can merge with it), where
// the conservative scan of that thread would read those stale words.
constexpr size_t kTracerStackSize = size_t{1} << 20;
constexpr size_t kTracerStackGuard = size_t{64} << 10;
std::atomic<bool> g_tracer_stack_busy{false};
void *g_tracer_stack = nullptr;

void *map_tracer_stack() {
  void *mapping = mmap(nullptr, kTracerStackSize + 2 * kTracerStackGuard,
                       PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapping == MAP_FAILED)
    return nullptr;
  auto *stack = static_cast<uint8_t *>(mapping) + kTracerStackGuard;
  if (mprotect(stack, kTracerStackSize, PROT_READ | PROT_WRITE) != 0) {
    munmap(mapping, kTracerStackSize + 2 * kTracerStackGuard);
    return nullptr;
  }
  madvise(stack, kTracerStackSize, MADV_NOHUGEPAGE);
  return stack;
}

void unmap_tracer_stack(void *stack) {
  munmap(static_cast<uint8_t *>(stack) - kTracerStackGuard,
         kTracerStackSize + 2 * kTracerStackGuard);
}

// Lists /proc/self/task without the two skipped ids into out[0, *count).
// Fails with errno set when the directory cannot be read, or with
// EOVERFLOW when it holds more than `cap` threads: a partial list would
// leave threads running through the patch.
bool list_task_tids(pid_t *out, size_t cap, size_t *count, pid_t skip_a,
                    pid_t skip_b) {
  const int fd = open("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }
  size_t n = 0;
  bool ok = true;
  alignas(linux_dirent64) char buf[2048];
  for (;;) {
    const long got = syscall(SYS_getdents64, fd, buf, sizeof(buf));
    if (got <= 0) {
      ok = got == 0;
      break;
    }
    for (long off = 0; ok && off < got;) {
      auto *ent = reinterpret_cast<linux_dirent64 *>(buf + off);
      off += ent->d_reclen;
      if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
        continue;
      }
      const pid_t tid = static_cast<pid_t>(atoi(ent->d_name));
      if (tid <= 0 || tid == skip_a || tid == skip_b) {
        continue;
      }
      if (n == cap) {
        errno = EOVERFLOW;
        ok = false;
        break;
      }
      out[n++] = tid;
    }
    if (!ok)
      break;
  }
  const int error = errno;
  close(fd);
  errno = error;
  *count = n;
  return ok;
}

// Read once. Mode 0 allows the attach anyway, and modes 2 and 3 refuse it
// whatever this process declares. Without the file (no Yama, or a sandbox
// hiding /proc/sys) the caller still tries and tolerates EINVAL.
bool yama_wants_ptracer() {
  static const bool wanted = [] {
    const int fd =
        open("/proc/sys/kernel/yama/ptrace_scope", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
      return true;
    char mode = 0;
    if (read(fd, &mode, 1) != 1)
      mode = '1';
    close(fd);
    return mode == '1';
  }();
  return wanted;
}

// TracerPid in /proc/self/status: a debugger or strace holds this process.
bool process_is_traced() {
  const int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return false;
  char buf[4096];
  const ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return false;
  buf[n] = '\0';
  const char *field = std::strstr(buf, "\nTracerPid:");
  return field != nullptr && atoi(field + 11) != 0;
}

// pthread_join returns when the kernel clears the thread id, before the
// thread finishes exiting. Until it is unlisted, /proc still names it and
// ptrace refuses it with EPERM, and a stop signal sent to it never runs
// its handler; its stat state is then Z or X. Called while siblings are
// stopped, from the tracer too, so the path is built by hand. `pid` is the
// patching process: /proc/self would name the tracer there.
bool task_exiting(pid_t pid, pid_t tid) {
  char path[64] = "/proc/";
  size_t len = 6;
  auto append_number = [&](long value) {
    char digits[20];
    size_t n = 0;
    do {
      digits[n++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value != 0);
    while (n != 0)
      path[len++] = digits[--n];
  };
  append_number(pid);
  std::memcpy(path + len, "/task/", 6);
  len += 6;
  append_number(tid);
  std::memcpy(path + len, "/stat", 6);
  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return errno == ENOENT;
  char buf[512];
  const ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return false;
  buf[n] = '\0';
  // The command name can contain ')', so take the last one.
  const char *end = std::strrchr(buf, ')');
  return end != nullptr && end[1] == ' ' && (end[2] == 'Z' || end[2] == 'X');
}

// Upper bound for one signal stop, initial wait and rescans together. Acks
// arrive in about a millisecond; the rest bounds failure when a sibling
// blocks the signal or a non-cooperating tracer wedges it.
constexpr uint64_t kSignalAckTimeoutNs = 200000000ULL;

// The reserved stop signal. Apps allocate real-time signals bottom-up from
// SIGRTMIN, so the top of the range stays free. A previous handler for the
// same number is chained while no patch is in flight.
int signal_stop_number() {
  static const int sig = [] {
    int s = SIGRTMAX - 4;
    if (s < SIGRTMIN) {
      s = SIGRTMIN;
    }
    return s;
  }();
  return sig;
}

// Marks stop signals as ours. Thread-directed delivery with a payload needs
// rt_tgsigqueueinfo (Linux 2.6.31+); without it the fallback refuses instead
// of sending anonymous signals it cannot later recognize.
constexpr int kSignalStopMagic = 0x53505544;

bool send_stop_signal(pid_t pid, pid_t tid) {
  siginfo_t info{};
  info.si_code = SI_QUEUE;
  info.si_value.sival_int = kSignalStopMagic;
  return syscall(SYS_rt_tgsigqueueinfo, pid, tid, signal_stop_number(),
                 &info) == 0;
}

// In-process stop state. Process-wide and grow-only: a thread running a
// stale stop signal after its window closed still reads valid memory, and a
// stop from an earlier generation is ignored instead of observed.
struct signal_slot {
  std::atomic<pid_t> tid{0};
  std::atomic<uint64_t> epoch{0};
  std::atomic<bool> acked{false};
  std::atomic<uintptr_t> ip{0};
  std::atomic<uintptr_t> sp{0};
  std::atomic<uintptr_t> lr{0};
  std::atomic<uintptr_t> ip0{0};
  std::atomic<uintptr_t> resume_ip{0};
};

std::atomic<signal_slot *> g_signal_slots{nullptr};
std::atomic<size_t> g_signal_cap{0};
std::atomic<uint64_t> g_signal_epoch{0};
std::atomic<bool> g_signal_active{false};
std::atomic<bool> g_signal_release{false};
std::atomic<pid_t> g_signal_owner{-1};
// Siblings inside stop_handler for one of our stop signals. A released
// sibling can still be on its way out, its redirect unread, when the next
// stop starts; resetting the slots under it would send it back into the
// bytes the last patch overwrote. The next stop waits for zero first.
std::atomic<int> g_signal_in_handler{0};
// Futex words: bumped by every ack, which the patcher sleeps on, and by
// every release, which stopped siblings sleep on. Raw futex calls are safe
// in a signal handler.
std::atomic<uint32_t> g_signal_acks{0};
std::atomic<uint32_t> g_signal_releases{0};

void futex_wake(std::atomic<uint32_t> &word, int count) {
  syscall(SYS_futex, reinterpret_cast<uint32_t *>(&word), FUTEX_WAKE_PRIVATE,
          count, nullptr, nullptr, 0);
}

// Sleeps until `word` moves on from `seen`, or `ns` passes.
void futex_wait(std::atomic<uint32_t> &word, uint32_t seen, uint64_t ns) {
  timespec timeout{static_cast<time_t>(ns / 1000000000ULL),
                   static_cast<long>(ns % 1000000000ULL)};
  syscall(SYS_futex, reinterpret_cast<uint32_t *>(&word), FUTEX_WAIT_PRIVATE,
          seen, &timeout, nullptr, 0);
}

void release_stopped_siblings() {
  g_signal_release.store(true, std::memory_order_release);
  g_signal_releases.fetch_add(1, std::memory_order_release);
  futex_wake(g_signal_releases, INT32_MAX);
}

stopped_regs regs_of(const user_regs_struct &regs) {
#if defined(SPUD_ARCH_X86_64)
  return {static_cast<uintptr_t>(regs.rip), static_cast<uintptr_t>(regs.rsp)};
#elif defined(SPUD_ARCH_ARM64)
  return {static_cast<uintptr_t>(regs.pc), static_cast<uintptr_t>(regs.sp),
          static_cast<uintptr_t>(regs.regs[30]),
          static_cast<uintptr_t>(regs.regs[16])};
#endif
}

void set_regs_ip(user_regs_struct &regs, uintptr_t ip) {
#if defined(SPUD_ARCH_X86_64)
  regs.rip = ip;
#elif defined(SPUD_ARCH_ARM64)
  regs.pc = ip;
#endif
}

bool peek_regs(pid_t tid, user_regs_struct *regs) {
#if defined(SPUD_ARCH_X86_64)
  return ptrace(PTRACE_GETREGS, tid, nullptr, regs) == 0;
#else
  iovec iov{regs, sizeof(*regs)};
  return ptrace(PTRACE_GETREGSET, tid, reinterpret_cast<void *>(NT_PRSTATUS),
                &iov) == 0;
#endif
}

bool poke_regs(pid_t tid, user_regs_struct *regs) {
#if defined(SPUD_ARCH_X86_64)
  return ptrace(PTRACE_SETREGS, tid, nullptr, regs) == 0;
#else
  iovec iov{regs, sizeof(*regs)};
  return ptrace(PTRACE_SETREGSET, tid, reinterpret_cast<void *>(NT_PRSTATUS),
                &iov) == 0;
#endif
}

} // namespace

struct safe_patch_session::PlatformState {
  struct target {
    pid_t tid = 0;
    bool seized = false;
    bool gone = false;
  };
  std::vector<target> threads;
  // Room for the post-freeze rescan, sized before anything stops.
  std::vector<pid_t> rescan;
  pid_t tracer_pid = -1;
  // errno behind the tracer's last failed attach. The tracer shares this
  // memory, and the socket reply orders the write before our read.
  std::atomic<int> tracer_error{0};
  void *stack = nullptr;
  bool shared_stack = false;
  int sock[2] = {-1, -1};

  // Signal fallback bookkeeping. The stop state itself (slots, generation,
  // release flag) is process-wide in the file below, so a thread running a
  // stale stop signal after its window closed still reads valid memory.
  bool signal_engaged = false;
  uint64_t signal_epoch_ = 0;
  int signal_error = 0;
  static struct sigaction g_previous_stop_action;

  static int install_stop_handler();
  static void forward_stop_signal(int sig, siginfo_t *info, void *ctx);
  static void stop_handler(int sig, siginfo_t *info, void *ctx);
  static void park_in_handler(int sig, siginfo_t *info, void *ctx);
  static bool wait_for_handlers_to_leave(uint64_t deadline_ns);
  static stopped_regs capture_context(void *ctx);
  static void retarget_context(void *ctx, uintptr_t ip);

  // Tries the tracer, then the signal fallback. Only the final failure is
  // reported, so a recovered ptrace EPERM never surfaces on success.
  bool suspend_all();
  bool suspend_via_signal(uint64_t *epoch_out);
  void resume_via_signal();
  bool redirect_via_signal(
      uint64_t epoch, uintptr_t base, size_t size, uintptr_t tramp_base,
      const std::vector<std::pair<uintptr_t, uintptr_t>> &map, bool to_entry);
  bool any_ip_via_signal(uint64_t epoch, const ip_range *list, size_t count,
                         bool only_returns);
  static bool claim_signal_slot(pid_t tid, size_t *next);
  static bool wait_for_signal_acks(uint64_t epoch, uint64_t deadline_ns);

  uintptr_t redir_base = 0;
  size_t redir_size = 0;
  uintptr_t redir_tramp = 0;
  const std::vector<std::pair<uintptr_t, uintptr_t>> *redir_map = nullptr;
  bool redir_to_entry = false;
  const ip_range *ranges = nullptr;
  size_t range_count = 0;
  bool returns_only = false;

  void close_socks() {
    if (sock[0] >= 0) {
      close(sock[0]);
      sock[0] = -1;
    }
    if (sock[1] >= 0) {
      close(sock[1]);
      sock[1] = -1;
    }
  }

  bool acquire_stack() {
    if (!g_tracer_stack_busy.exchange(true, std::memory_order_acquire)) {
      if (g_tracer_stack == nullptr)
        g_tracer_stack = map_tracer_stack();
      if (g_tracer_stack != nullptr) {
        stack = g_tracer_stack;
        shared_stack = true;
        return true;
      }
      g_tracer_stack_busy.store(false, std::memory_order_release);
    }
    stack = map_tracer_stack();
    return stack != nullptr;
  }

  // Only after the tracer has been reaped.
  void release_stack() {
    if (shared_stack)
      g_tracer_stack_busy.store(false, std::memory_order_release);
    else if (stack != nullptr)
      unmap_tracer_stack(stack);
    stack = nullptr;
    shared_stack = false;
  }

  // Waits for the tracer to exit and frees what suspend() set up for it.
  void reap_tracer() {
    int status = 0;
    // __WALL: the tracer exits without a signal, so a plain wait skips it.
    while (waitpid(tracer_pid, &status, __WALL) < 0 && errno == EINTR) {
    }
    tracer_pid = -1;
    for (auto &t : threads) {
      t.seized = false;
    }
    release_stack();
    close_socks();
  }

  // `threads` stays sorted by tid, so the rescans inside the window look
  // each listed thread up in log time rather than walk the whole list.
  bool knows(pid_t tid) const {
    const auto it = std::lower_bound(
        threads.begin(), threads.end(), tid,
        [](const target &t, pid_t value) { return t.tid < value; });
    return it != threads.end() && it->tid == tid;
  }

  // Inside the window: fails with EAGAIN rather than grow past the reserve.
  bool track(const target &t) {
    if (threads.size() >= threads.capacity()) {
      errno = EAGAIN;
      return false;
    }
    const auto it = std::lower_bound(
        threads.begin(), threads.end(), t.tid,
        [](const target &other, pid_t value) { return other.tid < value; });
    threads.insert(it, t);
    return true;
  }

  bool collect_siblings(pid_t self) {
    std::vector<target> next;
    DIR *dir = opendir("/proc/self/task");
    if (dir == nullptr) {
      return false;
    }
    while (dirent *entry = readdir(dir)) {
      if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
        continue;
      }
      const pid_t tid = static_cast<pid_t>(atoi(entry->d_name));
      if (tid <= 0 || tid == self) {
        continue;
      }
      next.push_back(target{.tid = tid});
    }
    closedir(dir);
    std::sort(next.begin(), next.end(),
              [](const target &a, const target &b) { return a.tid < b.tid; });
    threads.swap(next);
    return true;
  }

  // SEIZE and INTERRUPT stop the thread without queueing a SIGSTOP. An
  // ATTACH stop could be confused with a signal the thread had pending,
  // which then was swallowed while the SIGSTOP stopped the whole process
  // after detach. Pending signals now stay queued and arrive after detach.
  static bool interrupt_one(target &t) {
    if (ptrace(PTRACE_SEIZE, t.tid, nullptr, nullptr) != 0) {
      if (errno == ESRCH ||
          (errno == EPERM && task_exiting(getppid(), t.tid))) {
        t.gone = true;
        return true;
      }
      return false;
    }
    t.seized = true;
    return ptrace(PTRACE_INTERRUPT, t.tid, nullptr, nullptr) == 0 ||
           errno == ESRCH;
  }

  static bool attach_one(target &t) {
    return interrupt_one(t) && (t.gone || wait_for_stop(t));
  }

  bool attach_new(pid_t tid) {
    if (knows(tid)) {
      return true;
    }
    if (threads.size() >= threads.capacity()) {
      errno = EAGAIN;
      return false;
    }
    target t{.tid = tid};
    if (!attach_one(t)) {
      const int error = errno;
      if (t.seized && !t.gone)
        ptrace(PTRACE_DETACH, t.tid, nullptr, nullptr);
      errno = error;
      return false;
    }
    return track(t);
  }

  void detach_all() {
    for (auto &t : threads) {
      if (t.seized && !t.gone) {
        ptrace(PTRACE_DETACH, t.tid, nullptr, nullptr);
        t.seized = false;
      }
    }
  }

  static bool wait_for_stop(target &t) {
    for (;;) {
      int status = 0;
      const pid_t r = waitpid(t.tid, &status, __WALL);
      if (r < 0) {
        if (errno == EINTR) {
          continue;
        }
        // An attached thread can exit before its stop is reported. Accept
        // a missing wait child only after the kernel confirms it is gone.
        if ((errno == ECHILD || errno == ESRCH) &&
            syscall(SYS_tgkill, getppid(), t.tid, 0) < 0 && errno == ESRCH) {
          t.gone = true;
          t.seized = false;
          return true;
        }
        return false;
      }
      if (WIFEXITED(status) || WIFSIGNALED(status)) {
        t.gone = true;
        t.seized = false;
        return true;
      }
      if (!WIFSTOPPED(status))
        return false;
      // The interrupt (or a group stop already in progress) reports
      // PTRACE_EVENT_STOP. A signal dequeued just before it shows up as a
      // signal-delivery stop: deliver it and wait for the interrupt.
      if ((status >> 16) == PTRACE_EVENT_STOP)
        return true;
      ptrace(PTRACE_CONT, t.tid, nullptr,
             reinterpret_cast<void *>(static_cast<intptr_t>(WSTOPSIG(status))));
    }
  }

  bool do_redirect() {
    if (redir_map == nullptr) {
      return false;
    }
    for (auto &t : threads) {
      if (!t.seized || t.gone) {
        continue;
      }
      user_regs_struct regs{};
      if (!peek_regs(t.tid, &regs)) {
        if (errno == ESRCH) {
          t.gone = true;
          t.seized = false;
          continue;
        }
        return false;
      }
      const auto to = remap_ip(regs_of(regs).ip, redir_base, redir_size,
                               redir_tramp, *redir_map, redir_to_entry);
      if (!to) {
        continue;
      }
      set_regs_ip(regs, *to);
      if (!poke_regs(t.tid, &regs)) {
        if (errno == ESRCH) {
          t.gone = true;
          t.seized = false;
          continue;
        }
        return false;
      }
    }
    return true;
  }

  bool do_any_ip() {
    if (ranges == nullptr || range_count == 0) {
      return false;
    }
    for (auto &t : threads) {
      if (!t.seized || t.gone) {
        continue;
      }
      user_regs_struct regs{};
      if (!peek_regs(t.tid, &regs)) {
        if (errno == ESRCH) {
          t.gone = true;
          t.seized = false;
          continue;
        }
        return true;
      }
      if (stopped_in(regs_of(regs), ranges, range_count, returns_only)) {
        return true;
      }
    }
    return false;
  }

  // Runs in a cloned task that shares the address space but not the thread
  // group, so ptrace of the caller's siblings is allowed.
  //
  // Without CLONE_SETTLS the task also shares the patcher's thread pointer:
  // its thread_local variables and errno are the patcher's, which blocks on
  // the socket meanwhile. Its memory lookups (the 'A' stack scan) come only
  // after the patcher opened the window, so they see a nonzero window depth
  // and never fill process-wide caches with the tracer's own pid or
  // /proc/self files, which die with it. Only raw syscalls and window-safe
  // lookups belong here.
  static int run(void *arg) {
    auto *st = static_cast<PlatformState *>(arg);
    uint8_t go = 0;
    if (!recv_byte(st->sock[1], &go) || go != 'G') {
      return 1;
    }
    // Allowed from this cloned task; the same call from a sibling in the
    // caller's thread group is EPERM. Interrupt everyone first, then
    // collect the stops, so the threads wake and stop in parallel instead
    // of one context switch after another. Registers are touched only once
    // a stop is reported. On failure the tracer's exit releases any thread
    // still seized.
    bool failed = false;
    for (auto &t : st->threads) {
      if (!t.gone && !interrupt_one(t)) {
        st->tracer_error.store(errno, std::memory_order_relaxed);
        failed = true;
        break;
      }
    }
    for (auto &t : st->threads) {
      if (failed)
        break;
      if (t.seized && !t.gone && !wait_for_stop(t)) {
        st->tracer_error.store(errno, std::memory_order_relaxed);
        failed = true;
      }
    }
    if (failed) {
      st->detach_all();
      send_byte(st->sock[1], 'F');
      return 1;
    }
    if (!send_byte(st->sock[1], 'R')) {
      st->detach_all();
      return 1;
    }
    for (;;) {
      uint8_t cmd = 0;
      if (!recv_byte(st->sock[1], &cmd)) {
        break;
      }
      if (cmd == 'Q') {
        break;
      }
      uint8_t ack = 0;
      if (cmd == 'I') {
        ack = st->do_redirect() ? 1 : 0;
      } else if (cmd == 'A') {
        ack = st->do_any_ip() ? 1 : 0;
      } else if (cmd == 'T') {
        pid_t tid = 0;
        if (!xfer_all(st->sock[1], &tid, sizeof(tid), false)) {
          break;
        }
        ack = st->attach_new(tid) ? 1 : 0;
        if (ack == 0)
          st->tracer_error.store(errno, std::memory_order_relaxed);
      }
      if (!send_byte(st->sock[1], ack)) {
        break;
      }
    }
    st->detach_all();
    return 0;
  }

  // Starts the tracer, which attaches every sibling, then asks it to attach
  // threads that appeared while the first ones were stopping. Returns 0, or
  // the errno behind the failure; suspend_all() reports it only once the
  // signal fallback has failed too, so a recovered EPERM never surfaces.
  int suspend_with_tracer() {
    // A tracer that died closes its socket without setting errno.
    auto failed = [](int error) { return error != 0 ? error : EIO; };
    const pid_t self = gettid();
    // Nothing is stopped yet, so the scan may still allocate.
    if (!collect_siblings(self)) {
      return failed(errno);
    }
    if (threads.empty()) {
      return 0;
    }
    // Threads that appear while the first ones stop are attached inside the
    // window, where the vector cannot grow. A burst beyond this headroom
    // fails the patch with EAGAIN rather than allocate.
    threads.reserve(threads.size() * 2 + 64);
    rescan.resize(threads.capacity() + 1);

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sock) != 0) {
      return failed(errno);
    }
    if (!acquire_stack()) {
      const int error = errno;
      close_socks();
      return failed(error);
    }
    auto stack_top = reinterpret_cast<uintptr_t>(stack) + kTracerStackSize;
    stack_top &= ~static_cast<uintptr_t>(15);

    // No exit signal: the application's SIGCHLD handler and its
    // waitpid(-1) never see a child it did not spawn. The tracer starts
    // with every signal blocked. It has a copy of the application's
    // handlers, and each tracee stop sends it SIGCHLD, which would run the
    // application's handler on the tracer's stack.
    sigset_t all;
    sigset_t previous;
    sigfillset(&all);
    pthread_sigmask(SIG_SETMASK, &all, &previous);
    const pid_t child =
        clone(run, reinterpret_cast<void *>(stack_top),
              CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_UNTRACED, this);
    const int clone_error = errno;
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    if (child < 0) {
      release_stack();
      close_socks();
      return failed(clone_error);
    }
    tracer_pid = child;
    // Yama's mode 1 lets the tracer attach only to its own descendants
    // unless this process names it. That replaces any ptracer the
    // application registered (crash handlers do this), and Yama drops our
    // entry when the tracer exits, so such an application has to register
    // its handler again after patching. Yama has no call to read the old
    // one back. EINVAL means Yama is not built in, and nothing is needed.
    if (yama_wants_ptracer() &&
        prctl(PR_SET_PTRACER, static_cast<unsigned long>(child), 0, 0, 0) !=
            0 &&
        errno != EINVAL) {
      const int error = errno;
      close(sock[0]);
      sock[0] = -1;
      reap_tracer();
      return failed(error);
    }
    uint8_t ready = 0;
    if (!send_byte(sock[0], 'G') || !recv_byte(sock[0], &ready) ||
        ready != 'R') {
      const int error = ready == 'F' ? tracer_error.load() : errno;
      reap_tracer();
      return failed(error);
    }

    // Other threads are stopped. Re-scan /proc without allocating and attach
    // anyone who appeared during the freeze, until a pass finds nobody new.
    // A list that cannot be read, or never settles, fails the stop.
    auto abort = [&](int error) {
      send_byte(sock[0], 'Q');
      reap_tracer();
      return failed(error);
    };
    for (int pass = 0; pass < 16; ++pass) {
      size_t n = 0;
      if (!list_task_tids(rescan.data(), rescan.size(), &n, self, child)) {
        return abort(errno);
      }
      bool added = false;
      for (size_t i = 0; i < n; ++i) {
        if (knows(rescan[i])) {
          continue;
        }
        uint8_t ack = 0;
        if (!send_byte(sock[0], 'T') ||
            !xfer_all(sock[0], &rescan[i], sizeof(rescan[i]), true) ||
            !recv_byte(sock[0], &ack) || ack != 1) {
          return abort(ack == 0 ? tracer_error.load() : errno);
        }
        added = true;
      }
      if (!added) {
        return 0;
      }
    }
    return abort(EAGAIN);
  }

  void resume_all() {
    if (tracer_pid > 0) {
      send_byte(sock[0], 'Q');
      reap_tracer();
    }
    resume_via_signal();
  }

  bool redirect(uintptr_t base, size_t size, uintptr_t tramp_base,
                const std::vector<std::pair<uintptr_t, uintptr_t>> &map,
                bool to_entry) {
    if (signal_engaged) {
      return redirect_via_signal(signal_epoch_, base, size, tramp_base, map,
                                 to_entry);
    }
    if (tracer_pid <= 0) {
      return true;
    }
    redir_base = base;
    redir_size = size;
    redir_tramp = tramp_base;
    redir_map = &map;
    redir_to_entry = to_entry;
    uint8_t ack = 0;
    return send_byte(sock[0], 'I') && recv_byte(sock[0], &ack) && ack == 1;
  }

  bool any_ip_in(const ip_range *list, size_t count,
                 bool only_returns = false) {
    if (signal_engaged) {
      return any_ip_via_signal(signal_epoch_, list, count, only_returns);
    }
    if (tracer_pid <= 0) {
      return false;
    }
    ranges = list;
    range_count = count;
    returns_only = only_returns;
    uint8_t ack = 0;
    if (!send_byte(sock[0], 'A') || !recv_byte(sock[0], &ack)) {
      return true;
    }
    return ack == 1;
  }
};

struct sigaction safe_patch_session::PlatformState::g_previous_stop_action{};

int safe_patch_session::PlatformState::install_stop_handler() {
  // Re-asserted on every stop: the app may have installed its own handler
  // for the reserved signal since the last patch, which then becomes the
  // chained previous handler. Runs before anything stops, so the sigaction
  // calls are safe here.
  struct sigaction current{};
  if (sigaction(signal_stop_number(), nullptr, &current) != 0) {
    return errno;
  }
  if ((current.sa_flags & SA_SIGINFO) != 0 &&
      current.sa_sigaction == &PlatformState::stop_handler) {
    return 0;
  }
  struct sigaction act{};
  act.sa_sigaction = &PlatformState::stop_handler;
  sigemptyset(&act.sa_mask);
  act.sa_flags = SA_SIGINFO | SA_RESTART;
  if (sigaction(signal_stop_number(), &act, &g_previous_stop_action) != 0) {
    return errno;
  }
  return 0;
}

void safe_patch_session::PlatformState::forward_stop_signal(int sig,
                                                            siginfo_t *info,
                                                            void *ctx) {
  const struct sigaction prev = g_previous_stop_action;
  if ((prev.sa_flags & SA_SIGINFO) != 0 && prev.sa_sigaction != nullptr) {
    prev.sa_sigaction(sig, info, ctx);
    return;
  }
  if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN &&
      prev.sa_handler != nullptr) {
    prev.sa_handler(sig);
    return;
  }
  if (prev.sa_handler == SIG_DFL) {
    // A real-time signal's default disposition terminates. Restore it and
    // re-raise with raw syscalls, which are safe to call from a handler.
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(sig, &dfl, nullptr);
    syscall(SYS_tgkill, getpid(), syscall(SYS_gettid), sig);
  }
  // SIG_IGN: drop it.
}

stopped_regs safe_patch_session::PlatformState::capture_context(void *ctx) {
  const auto *uc = static_cast<ucontext_t *>(ctx);
#if defined(SPUD_ARCH_X86_64)
  return {static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]),
          static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RSP])};
#elif defined(SPUD_ARCH_ARM64)
  return {static_cast<uintptr_t>(uc->uc_mcontext.pc),
          static_cast<uintptr_t>(uc->uc_mcontext.sp),
          static_cast<uintptr_t>(uc->uc_mcontext.regs[30]),
          static_cast<uintptr_t>(uc->uc_mcontext.regs[16])};
#endif
}

void safe_patch_session::PlatformState::retarget_context(void *ctx,
                                                         uintptr_t ip) {
  auto *uc = static_cast<ucontext_t *>(ctx);
#if defined(SPUD_ARCH_X86_64)
  uc->uc_mcontext.gregs[REG_RIP] = static_cast<greg_t>(ip);
#elif defined(SPUD_ARCH_ARM64)
  uc->uc_mcontext.pc = static_cast<uint64_t>(ip);
#endif
}

// Runs on a sibling in its signal handler: publishes the interrupted context
// and spins until the patcher releases it, then resumes at a redirected
// address when one was set. Only lock-free atomics and raw syscalls here: a
// suspended sibling may hold the heap arena or any other lock.
void safe_patch_session::PlatformState::stop_handler(int sig, siginfo_t *info,
                                                     void *ctx) {
  if (sig != signal_stop_number() || ctx == nullptr || info == nullptr ||
      info->si_code != SI_QUEUE ||
      info->si_value.sival_int != kSignalStopMagic) {
    forward_stop_signal(sig, info, ctx);
    return;
  }
  // Sequentially consistent with the g_signal_active accesses: a sibling
  // that saw the window active is counted before the patcher, having closed
  // the window, reads the count.
  g_signal_in_handler.fetch_add(1);
  park_in_handler(sig, info, ctx);
  g_signal_in_handler.fetch_sub(1, std::memory_order_release);
}

void safe_patch_session::PlatformState::park_in_handler(int sig,
                                                        siginfo_t *info,
                                                        void *ctx) {
  if (!g_signal_active.load()) {
    // A stop from an earlier window, delivered late. Swallow it: the thread
    // was already released, and forwarding an ours signal would run alien
    // code or hit the default disposition.
    return;
  }
  const pid_t self = static_cast<pid_t>(syscall(SYS_gettid));
  if (self == g_signal_owner.load(std::memory_order_relaxed)) {
    // Never stop the patching thread itself; it is waiting for acks below.
    forward_stop_signal(sig, info, ctx);
    return;
  }
  const uint64_t epoch = g_signal_epoch.load(std::memory_order_relaxed);
  signal_slot *slots = g_signal_slots.load(std::memory_order_relaxed);
  const size_t cap = g_signal_cap.load(std::memory_order_relaxed);
  signal_slot *slot = nullptr;
  for (size_t i = 0; slots != nullptr && i < cap; ++i) {
    signal_slot &s = slots[i];
    const pid_t t = s.tid.load(std::memory_order_relaxed);
    if (t == self) {
      slot = &s;
      break;
    }
    if (t == 0 && slot == nullptr) {
      pid_t expect = 0;
      if (s.tid.compare_exchange_strong(expect, self,
                                        std::memory_order_relaxed)) {
        slot = &s;
        break;
      }
    }
  }
  if (slot == nullptr) {
    // No room left: the patcher times out and refuses. The signal was ours,
    // so swallowing it is correct; chaining would run alien code mid-window.
    return;
  }
  slot->epoch.store(epoch, std::memory_order_relaxed);
  const stopped_regs regs = capture_context(ctx);
  slot->ip.store(regs.ip, std::memory_order_relaxed);
  slot->sp.store(regs.sp, std::memory_order_relaxed);
  slot->lr.store(regs.lr, std::memory_order_relaxed);
  slot->ip0.store(regs.ip0, std::memory_order_relaxed);
  slot->acked.store(true, std::memory_order_release);
  g_signal_acks.fetch_add(1, std::memory_order_release);
  futex_wake(g_signal_acks, 1);
  for (uint32_t spins = 0;; ++spins) {
    const uint32_t seen = g_signal_releases.load(std::memory_order_acquire);
    if (g_signal_release.load(std::memory_order_acquire) ||
        g_signal_epoch.load(std::memory_order_relaxed) != epoch) {
      break;
    }
    if (spins < 2048) {
#if defined(SPUD_ARCH_X86_64)
      __builtin_ia32_pause();
#elif defined(SPUD_ARCH_ARM64)
      asm volatile("yield" ::: "memory");
#endif
      continue;
    }
    // A patch holds the stop for hundreds of microseconds or more. Sleep
    // rather than take a CPU the patcher may need; the timeout only guards
    // against a missed wake.
    futex_wait(g_signal_releases, seen, 10000000);
  }
  // A newer window may have reused this slot while this thread was
  // descheduled: only an address stored for this generation applies.
  const uintptr_t resume = slot->resume_ip.load(std::memory_order_acquire);
  if (resume != 0 && slot->epoch.load(std::memory_order_acquire) == epoch &&
      g_signal_epoch.load(std::memory_order_acquire) == epoch) {
    retarget_context(ctx, resume);
  }
}

// Gives a thread found by a rescan its slot before it is signalled, from
// *next upwards. A thread that claimed a slot itself in the handler is one
// spud never signalled this window.
bool safe_patch_session::PlatformState::claim_signal_slot(pid_t tid,
                                                          size_t *next) {
  signal_slot *slots = g_signal_slots.load(std::memory_order_relaxed);
  const size_t cap = g_signal_cap.load(std::memory_order_relaxed);
  for (; slots != nullptr && *next < cap; ++*next) {
    pid_t expect = 0;
    if (slots[*next].tid.compare_exchange_strong(expect, tid,
                                                 std::memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

// Acks normally arrive within tens of microseconds, so the wait yields
// first and then sleeps on the ack counter. A sibling that exited after its
// signal never acks; telling that apart costs a tgkill and a /proc read per
// sibling, so it is checked only for siblings still silent after a grace
// period, and then once per sleep.
bool safe_patch_session::PlatformState::wait_for_signal_acks(
    uint64_t epoch, uint64_t deadline_ns) {
  constexpr uint64_t kExitCheckAfterNs = 2000000;
  constexpr uint64_t kSleepNs = 1000000;
  const pid_t pid = getpid();
  const uint64_t start = monotonic_ns();
  for (uint32_t round = 0;; ++round) {
    const uint32_t seen = g_signal_acks.load(std::memory_order_acquire);
    const uint64_t now = monotonic_ns();
    const bool check_exits = now - start >= kExitCheckAfterNs;
    bool pending = false;
    signal_slot *slots = g_signal_slots.load(std::memory_order_relaxed);
    const size_t cap = g_signal_cap.load(std::memory_order_relaxed);
    for (size_t i = 0; slots != nullptr && i < cap; ++i) {
      const pid_t tid = slots[i].tid.load(std::memory_order_relaxed);
      if (tid == 0) {
        continue;
      }
      // An ack from an earlier generation is a descheduled thread's late
      // store, not a stopped thread.
      if (slots[i].epoch.load(std::memory_order_relaxed) == epoch &&
          slots[i].acked.load(std::memory_order_acquire)) {
        continue;
      }
      if (check_exits &&
          ((syscall(SYS_tgkill, pid, tid, 0) != 0 && errno == ESRCH) ||
           task_exiting(pid, tid))) {
        slots[i].tid.store(0, std::memory_order_relaxed);
        continue;
      }
      pending = true;
    }
    if (!pending) {
      return true;
    }
    if (now >= deadline_ns) {
      return false;
    }
    if (round < 32) {
      sched_yield();
      continue;
    }
    futex_wait(g_signal_acks, seen, std::min(kSleepNs, deadline_ns - now));
  }
}

bool safe_patch_session::PlatformState::suspend_all() {
  uint64_t epoch = 0;
  if (g_force_signal_stop_for_tests.load(std::memory_order_relaxed)) {
    if (suspend_via_signal(&epoch)) {
      signal_epoch_ = epoch;
      return true;
    }
    note_os_error(signal_error);
    return false;
  }
  const int tracer_err = suspend_with_tracer();
  if (tracer_err == 0) {
    return true;
  }
  if (suspend_via_signal(&epoch)) {
    signal_epoch_ = epoch;
    return true;
  }
  // Keep the historical failure report: EPERM under a tracer, whatever the
  // signal wait saw.
  note_os_error(process_is_traced() ? EPERM : tracer_err);
  return false;
}

bool safe_patch_session::PlatformState::suspend_via_signal(
    uint64_t *epoch_out) {
  const pid_t self = gettid();
  // Nothing is stopped yet, so this scan may still allocate.
  if (!collect_siblings(self)) {
    signal_error = errno;
    return false;
  }
  if (threads.empty()) {
    return true;
  }
  // Threads that appear while the first ones stop claim a slot in the
  // handler, where nothing can grow. A burst beyond this headroom fails the
  // patch rather than allocate.
  threads.reserve(threads.size() * 2 + 64);
  rescan.resize(threads.capacity() + 1);
  signal_error = install_stop_handler();
  if (signal_error != 0) {
    return false;
  }
  // Nothing is stopped yet. A sibling still leaving the last window's
  // handler reads its slot until it has taken its redirect.
  if (!wait_for_handlers_to_leave(monotonic_ns() + kSignalAckTimeoutNs)) {
    signal_error = ETIMEDOUT;
    return false;
  }
  // Grow-only slots: a thread descheduled in an earlier window may still
  // read the old array, so it is never freed.
  const size_t need = threads.capacity();
  if (g_signal_cap.load(std::memory_order_relaxed) < need) {
    auto *fresh = new (std::nothrow) signal_slot[need];
    if (fresh == nullptr) {
      signal_error = ENOMEM;
      return false;
    }
    (void)g_signal_slots.exchange(fresh, std::memory_order_acq_rel);
    g_signal_cap.store(need, std::memory_order_release);
  }
  signal_slot *slots = g_signal_slots.load(std::memory_order_relaxed);
  const size_t cap = g_signal_cap.load(std::memory_order_relaxed);
  for (size_t i = 0; i < threads.size(); ++i) {
    slots[i].tid.store(threads[i].tid, std::memory_order_relaxed);
    slots[i].epoch.store(0, std::memory_order_relaxed);
    slots[i].acked.store(false, std::memory_order_relaxed);
    slots[i].resume_ip.store(0, std::memory_order_relaxed);
  }
  for (size_t i = threads.size(); i < cap; ++i) {
    slots[i].tid.store(0, std::memory_order_relaxed);
    slots[i].epoch.store(0, std::memory_order_relaxed);
    slots[i].acked.store(false, std::memory_order_relaxed);
    slots[i].resume_ip.store(0, std::memory_order_relaxed);
  }
  g_signal_owner.store(self, std::memory_order_relaxed);
  g_signal_release.store(false, std::memory_order_relaxed);
  const uint64_t epoch = g_signal_epoch.load(std::memory_order_relaxed) + 1;
  g_signal_epoch.store(epoch, std::memory_order_relaxed);
  g_signal_active.store(true);
  *epoch_out = epoch;

  const pid_t pid = getpid();
  for (size_t i = 0; i < threads.size(); ++i) {
    if (!send_stop_signal(pid, threads[i].tid)) {
      if (errno == ESRCH) {
        threads[i].gone = true;
        slots[i].tid.store(0, std::memory_order_relaxed);
        continue;
      }
      // Earlier siblings may already spin; release them before refusing.
      signal_error = errno;
      release_stopped_siblings();
      g_signal_active.store(false);
      return false;
    }
  }
  auto abort = [&] {
    release_stopped_siblings();
    g_signal_active.store(false);
    signal_engaged = false;
    return false;
  };
  const uint64_t deadline = monotonic_ns() + kSignalAckTimeoutNs;
  if (!wait_for_signal_acks(epoch, deadline)) {
    signal_error = ETIMEDOUT;
    return abort();
  }
  // Other threads are stopped. Signal anyone who appeared during the freeze
  // until a pass finds nobody new; a stopped thread cannot start more.
  // Each newcomer is tracked and given a slot before its signal goes out,
  // so a later pass never signals it twice, ack or not.
  size_t next_slot = threads.size();
  for (int pass = 0; pass < 16; ++pass) {
    size_t n = 0;
    if (!list_task_tids(rescan.data(), rescan.size(), &n, self, self)) {
      signal_error = errno;
      return abort();
    }
    bool added = false;
    for (size_t i = 0; i < n; ++i) {
      const pid_t tid = rescan[i];
      if (knows(tid)) {
        continue;
      }
      if (!track(target{.tid = tid}) || !claim_signal_slot(tid, &next_slot)) {
        signal_error = EAGAIN;
        return abort();
      }
      if (!send_stop_signal(pid, tid)) {
        if (errno == ESRCH) {
          slots[next_slot].tid.store(0, std::memory_order_relaxed);
          continue;
        }
        signal_error = errno;
        return abort();
      }
      added = true;
    }
    if (!wait_for_signal_acks(epoch, deadline)) {
      signal_error = ETIMEDOUT;
      return abort();
    }
    if (!added) {
      signal_engaged = true;
      return true;
    }
  }
  signal_error = EAGAIN;
  return abort();
}

void safe_patch_session::PlatformState::resume_via_signal() {
  if (!signal_engaged) {
    return;
  }
  // Release first, then deactivate: a thread descheduled in the handler
  // still reads valid process-wide state either way.
  release_stopped_siblings();
  g_signal_active.store(false);
  signal_engaged = false;
}

bool safe_patch_session::PlatformState::wait_for_handlers_to_leave(
    uint64_t deadline_ns) {
  for (uint32_t spins = 0; g_signal_in_handler.load() != 0; ++spins) {
    if (spins < 64) {
      sched_yield();
      continue;
    }
    if (monotonic_ns() >= deadline_ns)
      return false;
    timespec req{0, 50000};
    nanosleep(&req, nullptr);
  }
  return true;
}

bool safe_patch_session::PlatformState::redirect_via_signal(
    uint64_t epoch, uintptr_t base, size_t size, uintptr_t tramp_base,
    const std::vector<std::pair<uintptr_t, uintptr_t>> &map, bool to_entry) {
  signal_slot *slots = g_signal_slots.load(std::memory_order_relaxed);
  const size_t cap = g_signal_cap.load(std::memory_order_relaxed);
  for (size_t i = 0; slots != nullptr && i < cap; ++i) {
    if (slots[i].tid.load(std::memory_order_relaxed) == 0) {
      continue;
    }
    // Without this generation's ack the thread is running unobserved:
    // refuse rather than patch under it.
    if (slots[i].epoch.load(std::memory_order_relaxed) != epoch ||
        !slots[i].acked.load(std::memory_order_acquire)) {
      return false;
    }
    const auto to = remap_ip(slots[i].ip.load(std::memory_order_relaxed), base,
                             size, tramp_base, map, to_entry);
    if (to) {
      slots[i].resume_ip.store(*to, std::memory_order_release);
    }
  }
  return true;
}

bool safe_patch_session::PlatformState::any_ip_via_signal(uint64_t epoch,
                                                          const ip_range *list,
                                                          size_t count,
                                                          bool only_returns) {
  if (list == nullptr || count == 0) {
    return false;
  }
  signal_slot *slots = g_signal_slots.load(std::memory_order_relaxed);
  const size_t cap = g_signal_cap.load(std::memory_order_relaxed);
  for (size_t i = 0; slots != nullptr && i < cap; ++i) {
    if (slots[i].tid.load(std::memory_order_relaxed) == 0) {
      continue;
    }
    if (slots[i].epoch.load(std::memory_order_relaxed) != epoch ||
        !slots[i].acked.load(std::memory_order_acquire)) {
      return true;
    }
    const stopped_regs regs{slots[i].ip.load(std::memory_order_relaxed),
                            slots[i].sp.load(std::memory_order_relaxed),
                            slots[i].lr.load(std::memory_order_relaxed),
                            slots[i].ip0.load(std::memory_order_relaxed)};
    if (stopped_in(regs, list, count, only_returns)) {
      return true;
    }
  }
  return false;
}

int stop_signal_for_tests() {
  return signal_stop_number();
}
#endif // SPUD_OS_LINUX

#ifdef SPUD_OS_WIN
namespace {
#if defined(SPUD_ARCH_X86_64)
stopped_regs regs_of(const CONTEXT &ctx) {
  return {ctx.Rip, ctx.Rsp};
}
void set_context_ip(CONTEXT &ctx, uintptr_t ip) {
  ctx.Rip = ip;
}
#elif defined(SPUD_ARCH_ARM64)
stopped_regs regs_of(const CONTEXT &ctx) {
  return {ctx.Pc, ctx.Sp, ctx.Lr, ctx.X16};
}
void set_context_ip(CONTEXT &ctx, uintptr_t ip) {
  ctx.Pc = ip;
}
#endif

// A thread call that fails because the thread exited is not an error.
bool thread_exited(HANDLE handle) {
  DWORD code = 0;
  return GetExitCodeThread(handle, &code) != 0 && code != STILL_ACTIVE;
}
} // namespace

struct safe_patch_session::PlatformState {
  struct target {
    HANDLE handle = nullptr;
    DWORD tid = 0;
    bool suspended = false;
    bool gone = false;
  };
  std::vector<target> threads;

  ~PlatformState() {
    for (auto &t : threads) {
      if (t.handle != nullptr) {
        CloseHandle(t.handle);
      }
    }
  }

  bool prepare() {
    const DWORD self_tid = GetCurrentThreadId();
    const DWORD self_pid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
      note_current_os_error();
      return false;
    }
    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    bool control_failed = false;
    DWORD control_error = 0;
    if (Thread32First(snapshot, &entry)) {
      do {
        if (entry.th32OwnerProcessID != self_pid ||
            entry.th32ThreadID == self_tid) {
          continue;
        }
        HANDLE handle =
            OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                           THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                       FALSE, entry.th32ThreadID);
        if (handle != nullptr) {
          threads.push_back(
              target{.handle = handle, .tid = entry.th32ThreadID});
          continue;
        }
        const DWORD open_error = GetLastError();
        HANDLE query = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE,
                                  entry.th32ThreadID);
        if (query != nullptr) {
          CloseHandle(query);
          control_failed = true;
          control_error = open_error;
        }
      } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (control_failed)
      note_os_error(static_cast<int>(control_error));
    return !control_failed;
  }

  // TODO(alex): a thread created between the Toolhelp snapshot in prepare()
  // and its creator's SuspendThread keeps running through the patch. Linux
  // and macOS enumerate again after the stop; CreateToolhelp32Snapshot and
  // OpenThread are not known to be safe while other threads are frozen
  // (heap or loader locks), so Windows does not yet.
  bool suspend_all() {
    for (auto &t : threads) {
      if (t.gone) {
        continue;
      }
      if (SuspendThread(t.handle) != static_cast<DWORD>(-1)) {
        t.suspended = true;
        continue;
      }
      const DWORD error = GetLastError();
      if (thread_exited(t.handle)) {
        t.gone = true;
        continue;
      }
      note_os_error(static_cast<int>(error));
      resume_all();
      return false;
    }
    return true;
  }

  void resume_all() {
    for (auto &t : threads) {
      if (t.suspended) {
        ResumeThread(t.handle);
        t.suspended = false;
      }
    }
  }

  bool redirect(uintptr_t base, size_t size, uintptr_t tramp_base,
                const std::vector<std::pair<uintptr_t, uintptr_t>> &map,
                bool to_entry) {
    for (auto &t : threads) {
      if (!t.suspended || t.gone) {
        continue;
      }
      CONTEXT ctx;
      ctx.ContextFlags = CONTEXT_CONTROL;
      if (GetThreadContext(t.handle, &ctx) == 0) {
        if (thread_exited(t.handle)) {
          t.gone = true;
          continue;
        }
        return false;
      }
      const auto to =
          remap_ip(regs_of(ctx).ip, base, size, tramp_base, map, to_entry);
      if (!to) {
        continue;
      }
      set_context_ip(ctx, *to);
      if (SetThreadContext(t.handle, &ctx) == 0) {
        if (thread_exited(t.handle)) {
          t.gone = true;
          continue;
        }
        return false;
      }
    }
    return true;
  }

  bool any_ip_in(const ip_range *ranges, size_t count,
                 bool returns_only = false) {
    for (auto &t : threads) {
      if (!t.suspended || t.gone) {
        continue;
      }
      CONTEXT ctx;
      ctx.ContextFlags = CONTEXT_CONTROL;
      if (GetThreadContext(t.handle, &ctx) == 0) {
        if (thread_exited(t.handle)) {
          t.gone = true;
          continue;
        }
        return true;
      }
      if (stopped_in(regs_of(ctx), ranges, count, returns_only)) {
        return true;
      }
    }
    return false;
  }
};
#endif // SPUD_OS_WIN

#ifdef SPUD_OS_APPLE
namespace {
#if defined(SPUD_ARCH_X86_64)
using mach_thread_state = x86_thread_state64_t;
constexpr thread_state_flavor_t kThreadStateFlavor = x86_THREAD_STATE64;
constexpr mach_msg_type_number_t kThreadStateCount = x86_THREAD_STATE64_COUNT;
uint64_t &state_ip(mach_thread_state &state) {
  return state.__rip;
}
stopped_regs regs_of(const mach_thread_state &state) {
  return {state.__rip, state.__rsp};
}
#elif defined(SPUD_ARCH_ARM64)
using mach_thread_state = arm_thread_state64_t;
constexpr thread_state_flavor_t kThreadStateFlavor = ARM_THREAD_STATE64;
constexpr mach_msg_type_number_t kThreadStateCount = ARM_THREAD_STATE64_COUNT;
uint64_t &state_ip(mach_thread_state &state) {
  return state.__pc;
}
stopped_regs regs_of(const mach_thread_state &state) {
  return {state.__pc, state.__sp, state.__lr, state.__x[16]};
}
#endif

kern_return_t get_thread_state(thread_act_t thread, mach_thread_state *state) {
  mach_msg_type_number_t count = kThreadStateCount;
  return thread_get_state(thread, kThreadStateFlavor,
                          reinterpret_cast<thread_state_t>(state), &count);
}
} // namespace

struct safe_patch_session::PlatformState {
  struct target {
    thread_act_t port = MACH_PORT_NULL;
    bool suspended = false;
    // Read once when the stop is confirmed. A suspended thread's registers
    // change only through redirect(), which keeps this in step. Under
    // Rosetta, which services each read, a second read of a thread that
    // was exiting never returned ("ThreadContext::resume failed").
    mach_thread_state state{};
  };
  // One send right per entry. Reserved before the stop, so threads the
  // post-stop enumeration finds are added without allocating.
  std::vector<target> threads;
  mach_port_t self = MACH_PORT_NULL;
  // Under Rosetta, which runs threads of its own in the process.
  bool translated = false;

  ~PlatformState() {
    for (const auto &t : threads) {
      mach_port_deallocate(mach_task_self(), t.port);
    }
    if (self != MACH_PORT_NULL) {
      mach_port_deallocate(mach_task_self(), self);
    }
  }

  bool prepare() {
    int flag = 0;
    size_t size = sizeof(flag);
    translated =
        sysctlbyname("sysctl.proc_translated", &flag, &size, nullptr, 0) == 0 &&
        flag == 1;
    self = mach_thread_self();
    thread_act_array_t list = nullptr;
    mach_msg_type_number_t n = 0;
    const auto status = task_threads(mach_task_self(), &list, &n);
    if (status != KERN_SUCCESS) {
      note_os_error(static_cast<int>(status));
      return false;
    }
    threads.reserve(n * 2 + 64);
    for (mach_msg_type_number_t i = 0; i < n; ++i) {
      threads.push_back(target{.port = list[i]});
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list),
                  n * sizeof(thread_act_t));
    return true;
  }

  bool knows(thread_act_t port) const {
    for (const auto &t : threads) {
      if (t.port == port) {
        return true;
      }
    }
    return false;
  }

  // A retained port can return INVALID_ARGUMENT after its thread exits.
  // Confirm that it left this task; the same error on a live thread must
  // still refuse the patch.
  static bool thread_exited(thread_act_t thread) {
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
      return false;
    bool found = false;
    for (mach_msg_type_number_t i = 0; i < count; ++i) {
      found |= threads[i] == thread;
      mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                  count * sizeof(thread_act_t));
    return !found;
  }

  static bool gone(kern_return_t kr, thread_act_t thread) {
    return kr == MACH_SEND_INVALID_DEST || kr == MACH_SEND_INVALID_RIGHT ||
           kr == KERN_TERMINATED ||
           (kr == KERN_INVALID_ARGUMENT && thread_exited(thread));
  }

  // Suspends one thread unless it is ours or already gone. Records the
  // Mach status of a failure.
  // Rosetta's exception server is a native thread in the task. It never
  // runs translated code, so it cannot be inside a patch, and
  // thread_get_state on a translated thread can wait for it: suspending it
  // hung the stop window.
  bool rosetta_thread(thread_act_t thread) const {
    if (!translated)
      return false;
    thread_extended_info_data_t info{};
    mach_msg_type_number_t count = THREAD_EXTENDED_INFO_COUNT;
    return thread_info(thread, THREAD_EXTENDED_INFO,
                       reinterpret_cast<thread_info_t>(&info),
                       &count) == KERN_SUCCESS &&
           std::strncmp(info.pth_name, "com.apple.rosetta.", 18) == 0;
  }

  bool suspend_one(target &t) {
    if (t.port == self || rosetta_thread(t.port)) {
      return true;
    }
    const kern_return_t kr = thread_suspend(t.port);
    if (kr != KERN_SUCCESS) {
      if (gone(kr, t.port)) {
        return true;
      }
      note_os_error(static_cast<int>(kr));
      return false;
    }
    // Mark first so a failed state read still resumes this thread. A
    // suspended Mach thread can report WAITING; thread_get_state stops the
    // target in the kernel before reading registers, so it confirms the
    // stop where run_state cannot.
    t.suspended = true;
    const auto stopped = get_thread_state(t.port, &t.state);
    if (stopped == KERN_SUCCESS) {
      return true;
    }
    if (gone(stopped, t.port)) {
      t.suspended = false;
      return true;
    }
    note_os_error(static_cast<int>(stopped));
    return false;
  }

  bool suspend_all() {
    for (auto &t : threads) {
      if (!suspend_one(t)) {
        resume_all();
        return false;
      }
    }
    // A thread started between the enumeration in prepare() and its
    // creator's stop is still running. task_threads only vm_allocates its
    // reply, so it is safe here; enumerate again until a pass finds nobody
    // new.
    for (int pass = 0; pass < 16; ++pass) {
      thread_act_array_t list = nullptr;
      mach_msg_type_number_t n = 0;
      const auto status = task_threads(mach_task_self(), &list, &n);
      if (status != KERN_SUCCESS) {
        note_os_error(static_cast<int>(status));
        resume_all();
        return false;
      }
      bool added = false;
      bool ok = true;
      for (mach_msg_type_number_t i = 0; i < n; ++i) {
        if (!ok || knows(list[i])) {
          mach_port_deallocate(mach_task_self(), list[i]);
          continue;
        }
        if (threads.size() == threads.capacity()) {
          // More new threads than the reserve holds; the vector cannot grow
          // while threads are stopped.
          note_os_error(KERN_RESOURCE_SHORTAGE);
          mach_port_deallocate(mach_task_self(), list[i]);
          ok = false;
          continue;
        }
        threads.push_back(target{.port = list[i]});
        added = true;
        ok = suspend_one(threads.back());
      }
      vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list),
                    n * sizeof(thread_act_t));
      if (!ok) {
        resume_all();
        return false;
      }
      if (!added) {
        return true;
      }
    }
    note_os_error(KERN_ABORTED);
    resume_all();
    return false;
  }

  void resume_all() {
    for (auto &t : threads) {
      if (t.suspended) {
        thread_resume(t.port);
        t.suspended = false;
      }
    }
  }

  bool redirect(uintptr_t base, size_t size, uintptr_t tramp_base,
                const std::vector<std::pair<uintptr_t, uintptr_t>> &map,
                bool to_entry) {
    for (auto &t : threads) {
      if (!t.suspended) {
        continue;
      }
      const auto to =
          remap_ip(regs_of(t.state).ip, base, size, tramp_base, map, to_entry);
      if (!to) {
        continue;
      }
      mach_thread_state moved = t.state;
      state_ip(moved) = static_cast<uint64_t>(*to);
      if (thread_set_state(t.port, kThreadStateFlavor,
                           reinterpret_cast<thread_state_t>(&moved),
                           kThreadStateCount) != KERN_SUCCESS) {
        return false;
      }
      t.state = moved;
    }
    return true;
  }

  // Whether a stopped thread has never run user code: nothing has set its
  // registers or its TSD base, which the kernel installs before a thread's
  // first user instruction (pthread creation, workqueue thread setup) and a
  // raw thread_create() caller sets with the registers. Such a thread holds
  // no frames to return through. Held by our suspension it cannot start
  // either; when it does it enters at a fresh entry point and stack.
  // Workqueue threads parked before their first request look like this,
  // with SP 0, which the stack check would otherwise refuse as unreadable.
  static bool never_ran(thread_act_t thread, const stopped_regs &regs) {
    if (regs.ip != 0 || regs.sp != 0 || regs.lr != 0 || regs.ip0 != 0)
      return false;
    thread_identifier_info_data_t identity{};
    mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
    return thread_info(thread, THREAD_IDENTIFIER_INFO,
                       reinterpret_cast<thread_info_t>(&identity),
                       &count) == KERN_SUCCESS &&
           identity.thread_handle == 0;
  }

  bool any_ip_in(const ip_range *ranges, size_t n, bool returns_only = false) {
    for (const auto &t : threads) {
      if (!t.suspended) {
        continue;
      }
      const stopped_regs regs = regs_of(t.state);
      // A zero PC is in no range, so only the return check can misjudge it.
      if (returns_only && never_ran(t.port, regs)) {
        continue;
      }
      if (stopped_in(regs, ranges, n, returns_only)) {
        return true;
      }
    }
    return false;
  }
};
#endif // SPUD_OS_APPLE

safe_patch_session::safe_patch_session() {
  if (g_active_session != nullptr) {
    owner_ = g_active_session;
    prepared_ = true;
    return;
  }
#if SPUD_OS_WIN
  std::array<void *, 256> returns{};
  own_return_count_ = RtlCaptureStackBackTrace(
      0, static_cast<ULONG>(returns.size()), returns.data(), nullptr);
  for (size_t i = 0; i < own_return_count_; ++i)
    own_returns_[i] = reinterpret_cast<uintptr_t>(returns[i]);
  own_trace_complete_ =
      own_return_count_ != 0 && own_return_count_ < returns.size();
#else
  const auto traced = _Unwind_Backtrace(
      [](_Unwind_Context *context, void *opaque) {
        auto &session = *static_cast<safe_patch_session *>(opaque);
        const uintptr_t pc = _Unwind_GetIP(context);
        if (pc == 0)
          return _URC_NO_REASON;
        if (session.own_return_count_ == session.own_returns_.size())
          return _URC_NORMAL_STOP;
        session.own_returns_[session.own_return_count_++] = pc;
        return _URC_NO_REASON;
      },
      this);
  own_trace_complete_ = traced == _URC_END_OF_STACK && own_return_count_ != 0;
#endif
  state_ = new PlatformState();
  prewarm_monotonic_clock();
#if SPUD_OS_LINUX
  // Linux enumerates threads in suspend(), right before it stops them.
  prepared_ = true;
#else
  prepared_ = state_->prepare();
#endif
}

safe_patch_session::~safe_patch_session() {
  resume();
  delete state_;
}

// A nested session rides on its owner's stop. It only tracks its share of
// the window depth.
bool safe_patch_session::suspend() {
  if (owner_ != nullptr) {
    if (!owner_->suspended()) {
      return false;
    }
    if (!suspended_) {
      ++g_suspend_window_depth;
      suspended_ = true;
    }
    return true;
  }
  if (suspended_) {
    return true;
  }
  g_suspend_stats_for_tests.attempts.fetch_add(1, std::memory_order_relaxed);
  if (g_force_unguarded_patch_for_tests.load(std::memory_order_relaxed) ||
      !prepared_ || inject_fault(patch_fault::suspend) ||
      !state_->suspend_all()) {
    note_fallback();
    return false;
  }
  suspended_ = true;
  g_active_session = this;
  g_suspend_stats_for_tests.engaged.fetch_add(1, std::memory_order_relaxed);
  ++g_suspend_window_depth;
  return true;
}

void safe_patch_session::resume() {
  if (!suspended_) {
    return;
  }
  if (owner_ == nullptr) {
    state_->resume_all();
    if (g_active_session == this) {
      g_active_session = nullptr;
    }
  }
  suspended_ = false;
  --g_suspend_window_depth;
}

bool safe_patch_session::redirect_ips(
    uintptr_t base, size_t size, uintptr_t tramp_base,
    const std::vector<std::pair<uintptr_t, uintptr_t>> &relocation_offsets,
    bool to_entry) {
  if (owner_ != nullptr) {
    return owner_->redirect_ips(base, size, tramp_base, relocation_offsets,
                                to_entry);
  }
  if (inject_fault(patch_fault::redirect)) {
    return false;
  }
  if (!suspended_) {
    return true;
  }
  return state_->redirect(base, size, tramp_base, relocation_offsets, to_entry);
}

bool safe_patch_session::any_ip_in_ranges(const ip_range *ranges,
                                          size_t count) {
  if (owner_ != nullptr) {
    return owner_->any_ip_in_ranges(ranges, count);
  }
  if (!suspended_ || ranges == nullptr || count == 0) {
    return false;
  }
  return state_->any_ip_in(ranges, count);
}

bool safe_patch_session::any_return_in_ranges(const ip_range *ranges,
                                              size_t count) {
  if (owner_ != nullptr) {
    return owner_->any_return_in_ranges(ranges, count);
  }
  if (any_own_return_in_ranges(ranges, count))
    return true;
  if (!suspended_ || ranges == nullptr || count == 0) {
    return false;
  }
  return state_->any_ip_in(ranges, count, true);
}

bool safe_patch_session::any_own_return_in_ranges(const ip_range *ranges,
                                                  size_t count) const {
  if (owner_ != nullptr)
    return owner_->any_own_return_in_ranges(ranges, count);
  if (ranges == nullptr || count == 0)
    return false;
  if (!own_trace_complete_)
    return true;
  for (size_t i = 0; i < own_return_count_; ++i) {
    if (ip_in_ranges(own_returns_[i], ranges, count))
      return true;
  }
  return false;
}

} // namespace spud::detail

namespace spud {

void allow_unguarded_patches(bool allowed) {
  detail::g_allow_unguarded_patch.store(allowed, std::memory_order_relaxed);
}

patch_stats query_patch_stats() {
  auto &s = detail::g_suspend_stats_for_tests;
  return patch_stats{
      s.attempts.load(std::memory_order_relaxed),
      s.engaged.load(std::memory_order_relaxed),
      s.fallback.load(std::memory_order_relaxed),
      s.redirects.load(std::memory_order_relaxed),
      detail::last_os_error_any(),
  };
}

} // namespace spud

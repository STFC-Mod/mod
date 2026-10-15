#include "memory/safe_patch.h"
#include <spud/arch.h>
#include <spud/diagnostics.h>
#include <spud/memory.h>
#include <spud/utils.h>

#include "executable_pool.h"
#include "readable.h"

#if SPUD_OS_WIN
#include <windows.h>
#elif SPUD_OS_APPLE
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#elif SPUD_OS_LINUX
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <sys/auxv.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>
#else
#error "Unsupported platform"
#endif

#include <algorithm>
#include <atomic>
#include <cstring>
#include <optional>

namespace spud {

namespace detail {
std::atomic<bool> g_disable_procmap_for_tests{false};
} // namespace detail

#if defined(SPUD_OS_LINUX)
namespace {
std::atomic<size_t> g_page_size{0};

size_t memory_page_size() {
  size_t cached = g_page_size.load(std::memory_order_relaxed);
  if (cached == 0) {
    cached = static_cast<size_t>(sysconf(_SC_PAGE_SIZE));
    g_page_size.store(cached, std::memory_order_relaxed);
  }
  return cached;
}

struct linux_map {
  uintptr_t start = 0;
  uintptr_t end = 0;
  int prot = 0;
};

#if defined(__aarch64__)
constexpr int kProtBti = 0x10;
constexpr int kProtMte = 0x20;

// maps' rwx fields omit guarded-page and tagged-memory attributes. Only
// /proc/self/smaps reports them, and generating it walks every page table,
// so read it only on hardware that can set either attribute.
bool page_attributes_possible() {
  static const bool possible = [] {
    const unsigned long hwcap2 = getauxval(AT_HWCAP2);
    return (hwcap2 & ((1ul << 17) | (1ul << 18))) != 0; // HWCAP2_BTI, _MTE
  }();
  return possible;
}
#else
bool page_attributes_possible() {
  return false;
}
#endif

bool parse_linux_maps(std::vector<linux_map> *out, bool attributes) {
  FILE *maps =
      std::fopen(attributes ? "/proc/self/smaps" : "/proc/self/maps", "r");
  if (maps == nullptr) {
    return false;
  }
  out->clear();
  // A line can outrun the buffer (a mapped file's path can be 4KB). Only its
  // first piece is parsed; the rest, a piece of a path, could otherwise
  // read as a mapping of its own.
  char line[512];
  bool rest_of_line = false;
  while (std::fgets(line, sizeof(line), maps) != nullptr) {
    const bool continuation = rest_of_line;
    rest_of_line = std::strchr(line, '\n') == nullptr;
    if (continuation)
      continue;
#if defined(__aarch64__)
    if (std::strncmp(line, "VmFlags:", 8) == 0 && !out->empty()) {
      for (const char *p = line + 8; *p != '\0'; ++p) {
        if (p[0] == ' ' && p[1] == 'b' && p[2] == 't' &&
            (p[3] == ' ' || p[3] == '\n')) {
          out->back().prot |= kProtBti;
        } else if (p[0] == ' ' && p[1] == 'm' && p[2] == 't' &&
                   (p[3] == ' ' || p[3] == '\n')) {
          out->back().prot |= kProtMte;
        }
      }
      continue;
    }
#endif
    unsigned long long map_start = 0;
    unsigned long long map_end = 0;
    char perms[8] = {};
    if (std::sscanf(line, "%llx-%llx %7s", &map_start, &map_end, perms) < 3) {
      continue;
    }
    int p = 0;
    if (perms[0] == 'r') {
      p |= PROT_READ;
    }
    if (perms[1] == 'w') {
      p |= PROT_WRITE;
    }
    if (perms[2] == 'x') {
      p |= PROT_EXEC;
    }
    out->push_back(linux_map{static_cast<uintptr_t>(map_start),
                             static_cast<uintptr_t>(map_end), p});
  }
  std::fclose(maps);
  return true;
}

// PROCMAP_QUERY (Linux 6.11) answers for one address with an ioctl on an
// open maps file instead of formatting the whole map as text. The layout is
// copied so older headers still build.
struct procmap_query_args {
  uint64_t size;
  uint64_t query_flags;
  uint64_t query_addr;
  uint64_t vma_start;
  uint64_t vma_end;
  uint64_t vma_flags;
  uint64_t vma_page_size;
  uint64_t vma_offset;
  uint64_t inode;
  uint32_t dev_major;
  uint32_t dev_minor;
  uint32_t vma_name_size;
  uint32_t build_id_size;
  uint64_t vma_name_addr;
  uint64_t build_id_addr;
};
constexpr unsigned long kProcmapQuery = _IOWR('f', 17, procmap_query_args);
// Answers with the next mapping above an unmapped address.
constexpr uint64_t kProcmapCoveringOrNext = 0x10;

// Per-process caches: this process's pid, which glibc no longer caches and
// every OS read needs, and an open maps file for PROCMAP_QUERY. Both are
// filled only outside the suspend window. That keeps spud's tracer task,
// which runs only inside the window, from caching its own pid or maps file;
// it shares this process's memory, so it reads what the patcher cached.
//
// fork() clears both in the child. A child created with raw clone() or
// vfork() skips that handler; if it keeps calling spud without exec, its
// reads name the parent's pid (process_vm_readv then fails over to
// /proc/self/mem, or reads the parent where ptrace rules allow it) and its
// lookups describe the parent's mappings.
std::atomic<pid_t> g_pid{0};
std::atomic<int> g_procmap_fd{-1};
std::atomic<bool> g_procmap_unsupported{false};
std::atomic<bool> g_procmap_worked{false};

void watch_forks() {
  static const bool registered = [] {
    pthread_atfork(nullptr, nullptr, [] {
      g_pid.store(0, std::memory_order_relaxed);
      // The child has not run any of its own code yet, so the number is
      // still ours to close. It names the parent's maps.
      const int fd = g_procmap_fd.exchange(-1, std::memory_order_relaxed);
      if (fd >= 0)
        close(fd);
    });
    return true;
  }();
  (void)registered;
}

pid_t self_pid() {
  pid_t pid = g_pid.load(std::memory_order_relaxed);
  if (pid != 0)
    return pid;
  pid = getpid();
  if (detail::g_suspend_window_depth == 0) {
    watch_forks();
    g_pid.store(pid, std::memory_order_relaxed);
  }
  return pid;
}

// The cached descriptor, or a fresh one the caller closes (*owned) when
// none is cached and the window forbids caching.
int procmap_fd(bool *owned) {
  *owned = false;
  int fd = g_procmap_fd.load(std::memory_order_acquire);
  if (fd >= 0)
    return fd;
  fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
  if (fd < 0 || detail::g_suspend_window_depth != 0) {
    *owned = fd >= 0;
    return fd;
  }
  watch_forks();
  int expected = -1;
  if (!g_procmap_fd.compare_exchange_strong(expected, fd,
                                            std::memory_order_acq_rel)) {
    close(fd);
    return expected;
  }
  return fd;
}

enum class procmap_result { found, unmapped, unavailable };

procmap_result procmap_query(uintptr_t address, linux_map *out,
                             uint64_t flags = 0) {
  if (g_procmap_unsupported.load(std::memory_order_relaxed) ||
      detail::g_disable_procmap_for_tests.load(std::memory_order_relaxed))
    return procmap_result::unavailable;
  for (int attempt = 0; attempt < 2; ++attempt) {
    bool owned = false;
    const int fd = procmap_fd(&owned);
    if (fd < 0)
      return procmap_result::unavailable;
    procmap_query_args q{};
    q.size = sizeof(q);
    q.query_flags = flags;
    q.query_addr = address;
    const int status = ioctl(fd, kProcmapQuery, &q);
    const int error = errno;
    if (owned)
      close(fd);
    errno = error;
    if (status == 0) {
      g_procmap_worked.store(true, std::memory_order_relaxed);
      int prot = 0;
      if ((q.vma_flags & 1) != 0)
        prot |= PROT_READ;
      if ((q.vma_flags & 2) != 0)
        prot |= PROT_WRITE;
      if ((q.vma_flags & 4) != 0)
        prot |= PROT_EXEC;
      *out = {static_cast<uintptr_t>(q.vma_start),
              static_cast<uintptr_t>(q.vma_end), prot};
      return procmap_result::found;
    }
    if (errno == ENOENT)
      return procmap_result::unmapped;
    if (errno != EBADF && errno != ENOTTY && errno != EINVAL)
      return procmap_result::unavailable;
    if (!g_procmap_worked.load(std::memory_order_relaxed)) {
      // An older kernel, or a procfs emulation such as qemu-user's.
      g_procmap_unsupported.store(true, std::memory_order_relaxed);
      return procmap_result::unavailable;
    }
    // It worked before, so the application closed or reused the descriptor.
    // The number is no longer ours to close.
    int stale = fd;
    g_procmap_fd.compare_exchange_strong(stale, -1, std::memory_order_acq_rel);
  }
  return procmap_result::unavailable;
}

std::mutex g_maps_mu;
std::vector<linux_map> g_maps;

void refresh_linux_maps_locked(bool attributes) {
  std::vector<linux_map> fresh;
  if (!parse_linux_maps(&fresh, attributes)) {
    return;
  }
  // Room for the splits protection changes make inside a window, two per
  // scope; a batch of a hundred hooks fits.
  fresh.reserve(fresh.size() + 256);
  g_maps.swap(fresh);
}

linux_map *find_linux_map(uintptr_t address) {
  const auto it = std::upper_bound(
      g_maps.begin(), g_maps.end(), address,
      [](uintptr_t a, const linux_map &m) { return a < m.start; });
  if (it == g_maps.begin() || address >= std::prev(it)->end)
    return nullptr;
  return &*std::prev(it);
}

// Inside the suspend window no other thread can mmap, and the cache is
// the snapshot taken before it opened. Outside it, VMAs can appear, vanish,
// or reuse an address between calls, so every lookup rereads the maps.
std::unique_lock<std::mutex> lock_linux_maps() {
  if (detail::g_suspend_window_depth > 0) {
    return {};
  }
  return std::unique_lock<std::mutex>(g_maps_mu);
}

// Keeps the snapshot a window reads true after mprotect(start, end). A
// partial change splits the cached mapping as it splits the VMA; giving
// the whole entry the new protection made a second scope in the same
// mapping record, and later restore, the wrong original. The splits use
// capacity reserved before the window. Without room the touched entries
// are dropped, so a later lookup fails instead of reporting stale bits.
// Outside the window every lookup rereads, so there is nothing to keep.
void note_linux_prot(uintptr_t start, uintptr_t end, int prot) {
  if (detail::g_suspend_window_depth == 0) {
    return;
  }
  auto it = std::upper_bound(
      g_maps.begin(), g_maps.end(), start,
      [](uintptr_t a, const linux_map &m) { return a < m.start; });
  size_t i = static_cast<size_t>(it - g_maps.begin());
  if (i > 0 && g_maps[i - 1].end > start) {
    --i;
  }
  while (i < g_maps.size() && g_maps[i].start < end) {
    const linux_map m = g_maps[i];
    const size_t extra = (m.start < start ? 1 : 0) + (m.end > end ? 1 : 0);
    if (g_maps.size() + extra > g_maps.capacity()) {
      g_maps.erase(g_maps.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    linux_map parts[3];
    size_t n = 0;
    if (m.start < start) {
      parts[n++] = {m.start, start, m.prot};
    }
    parts[n++] = {std::max(m.start, start), std::min(m.end, end), prot};
    if (m.end > end) {
      parts[n++] = {end, m.end, m.prot};
    }
    g_maps[i] = parts[0];
    g_maps.insert(g_maps.begin() + static_cast<std::ptrdiff_t>(i + 1),
                  parts + 1, parts + n);
    i += n;
  }
}

// `attributes` asks for the arm64 BTI and MTE bits, which only the snapshot
// parsed from smaps carries.
std::optional<linux_map> query_linux_map(uintptr_t address, bool attributes) {
  attributes = attributes && page_attributes_possible();
  if (!attributes) {
    linux_map m;
    switch (procmap_query(address, &m)) {
    case procmap_result::found:
      return m;
    case procmap_result::unmapped:
      return std::nullopt;
    case procmap_result::unavailable:
      break;
    }
  }
  const auto lock = lock_linux_maps();
  if (lock.owns_lock()) {
    refresh_linux_maps_locked(attributes);
  }
  const linux_map *m = find_linux_map(address);
  if (m == nullptr) {
    return std::nullopt;
  }
  return *m;
}

// The kernel copy honours page protection: an unreadable page ends the
// read. -1 when nothing could be read or the call is missing (qemu-user).
ssize_t vm_read(uintptr_t address, std::span<uint8_t> out) {
  iovec local{out.data(), out.size()};
  iovec remote{reinterpret_cast<void *>(address), out.size()};
  return process_vm_readv(self_pid(), &local, 1, &remote, 1, 0);
}

} // namespace

namespace detail {

// Sets up what a lookup inside the suspend window needs: the maps
// descriptor, or the snapshot the window reads instead.
void prewarm_memory_page_size() {
  (void)memory_page_size();
  (void)self_pid();
  linux_map probe;
  const bool queried = procmap_query(reinterpret_cast<uintptr_t>(&probe),
                                     &probe) == procmap_result::found;
  if (queried && !page_attributes_possible())
    return;
  if (const auto lock = lock_linux_maps(); lock.owns_lock()) {
    refresh_linux_maps_locked(page_attributes_possible());
  }
}
} // namespace detail
#else
namespace detail {
void prewarm_memory_page_size() {}
} // namespace detail
#endif

namespace {
#if SPUD_OS_WIN
using native_prot = DWORD;
#elif SPUD_OS_APPLE
using native_prot = vm_prot_t;
#elif SPUD_OS_LINUX
using native_prot = int;
#endif

// What the OS reports for an address with no mapping, and for a page that
// refuses the access, so a failure found by a lookup reads like one the OS
// call itself would have returned.
#if SPUD_OS_WIN
constexpr int kUnmappedError = ERROR_INVALID_ADDRESS;
constexpr int kNoAccessError = ERROR_NOACCESS;
#elif SPUD_OS_APPLE
constexpr int kUnmappedError = KERN_INVALID_ADDRESS;
constexpr int kNoAccessError = KERN_PROTECTION_FAILURE;
#elif SPUD_OS_LINUX
constexpr int kUnmappedError = ENOMEM;
constexpr int kNoAccessError = EFAULT;
#endif

// `prot` keeps the OS flags so a scope can restore them exactly.
struct native_region {
  uintptr_t start = 0;
  size_t size = 0;
  native_prot prot = 0;
};

// `attributes` matters only on arm64 Linux, where it also asks for the BTI
// and MTE bits a protection change must carry over.
std::optional<native_region> query_native(uintptr_t address,
                                          bool attributes = false) {
#if SPUD_OS_WIN
  (void)attributes;
  MEMORY_BASIC_INFORMATION info;
  if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) ==
          0 ||
      info.State != MEM_COMMIT) {
    return std::nullopt;
  }
  return native_region{reinterpret_cast<uintptr_t>(info.BaseAddress),
                       info.RegionSize, info.Protect};
#elif SPUD_OS_APPLE
  (void)attributes;
  vm_size_t vmsize = 0;
  vm_address_t addr = static_cast<vm_address_t>(address);
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
  memory_object_name_t object = MACH_PORT_NULL;
  const kern_return_t status = vm_region_64(
      mach_task_self(), &addr, &vmsize, VM_REGION_BASIC_INFO_64,
      reinterpret_cast<vm_region_info_t>(&info), &info_count, &object);
  if (status != KERN_SUCCESS) {
    return std::nullopt;
  }
  if (object != MACH_PORT_NULL) {
    mach_port_deallocate(mach_task_self(), object);
  }
  // vm_region returns the next mapping when `address` sits in a gap.
  if (address < addr || address >= addr + vmsize) {
    return std::nullopt;
  }
  return native_region{static_cast<uintptr_t>(addr),
                       static_cast<size_t>(vmsize), info.protection};
#elif SPUD_OS_LINUX
  const auto m = query_linux_map(address, attributes);
  if (!m) {
    return std::nullopt;
  }
  return native_region{m->start, static_cast<size_t>(m->end - m->start),
                       m->prot};
#endif
}

#if !SPUD_OS_WIN
// The POSIX and Mach protections are bit sets; Windows has its own table.
mem_protection combine_prot(bool read, bool write, bool execute) {
  auto bits = mem_protection::kNone;
  if (read) {
    bits |= mem_protection::kRead;
  }
  if (write) {
    bits |= mem_protection::kWrite;
  }
  if (execute) {
    bits |= mem_protection::kExecute;
  }
  return bits;
}
#endif

#if SPUD_OS_WIN
DWORD native_from(mem_protection protect) {
  const bool write = has_protection(protect, mem_protection::kWrite);
  if (has_protection(protect, mem_protection::kExecute)) {
    if (write)
      return PAGE_EXECUTE_READWRITE;
    return has_protection(protect, mem_protection::kRead) ? PAGE_EXECUTE_READ
                                                          : PAGE_EXECUTE;
  }
  if (write)
    return PAGE_READWRITE;
  return has_protection(protect, mem_protection::kRead) ? PAGE_READONLY
                                                        : PAGE_NOACCESS;
}

mem_protection protection_from(DWORD protect) {
  switch (protect & 0xFFu) {
  case PAGE_READONLY:
    return mem_protection::kRead;
  case PAGE_READWRITE:
  case PAGE_WRITECOPY:
    return mem_protection::kReadWrite;
  case PAGE_EXECUTE:
    return mem_protection::kExecute;
  case PAGE_EXECUTE_READ:
    return mem_protection::kReadExecute;
  case PAGE_EXECUTE_READWRITE:
  case PAGE_EXECUTE_WRITECOPY:
    return mem_protection::kReadWriteExecute;
  default:
    return mem_protection::kNone;
  }
}
#elif SPUD_OS_APPLE
vm_prot_t native_from(mem_protection protect) {
  const bool write = has_protection(protect, mem_protection::kWrite);
  if (has_protection(protect, mem_protection::kExecute)) {
    // Outside MAP_JIT the kernel refuses write and execute together. A
    // copy-on-write private page can be written, but cannot run until the
    // protection is put back.
    return write ? VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY
                 : VM_PROT_READ | VM_PROT_EXECUTE;
  }
  if (write)
    return VM_PROT_READ | VM_PROT_WRITE;
  return has_protection(protect, mem_protection::kRead) ? VM_PROT_READ
                                                        : VM_PROT_NONE;
}

mem_protection protection_from(vm_prot_t prot) {
  return combine_prot((prot & VM_PROT_READ) != 0, (prot & VM_PROT_WRITE) != 0,
                      (prot & VM_PROT_EXECUTE) != 0);
}
#elif SPUD_OS_LINUX
int native_from(mem_protection protect) {
  int prot = PROT_NONE;
  if (has_protection(protect, mem_protection::kRead))
    prot |= PROT_READ;
  if (has_protection(protect, mem_protection::kWrite))
    prot |= PROT_READ | PROT_WRITE;
  if (has_protection(protect, mem_protection::kExecute))
    prot |= PROT_EXEC;
  return prot;
}

mem_protection protection_from(int prot) {
  return combine_prot((prot & PROT_READ) != 0, (prot & PROT_WRITE) != 0,
                      (prot & PROT_EXEC) != 0);
}
#endif
} // namespace

protection_scope::protection_scope(uintptr_t address, size_t size,
                                   mem_protection protect)
    : address_(address), size_(size) {
  if (size == 0) {
    // Nothing to change or put back.
    address_ = 0;
    ok_ = true;
    return;
  }
  if (!has_protection(mem_protection::kReadWriteExecute, protect)) {
    return;
  }
  native_prot protection = native_from(protect);
  const auto region = query_native(address_, true);
  if (!region) {
    detail::note_os_error(kUnmappedError);
    return;
  }
#if defined(SPUD_OS_LINUX) && defined(__aarch64__)
  // RWX changes for patching must retain allocation tags and guarded
  // execution.
  protection |= region->prot & kProtMte;
  if ((protection & PROT_EXEC) != 0) {
    protection |= region->prot & kProtBti;
  }
#endif
  // A patch can straddle two mappings: a text page an earlier patch wrote
  // is private memory the kernel cannot merge back into its file-backed
  // neighbour. Take the range when the mappings are contiguous and share
  // the one protection restore() puts back.
  uintptr_t covered = region->start + region->size;
  while (covered < address_ + size_) {
    const auto next = query_native(covered, true);
    if (!next || next->start != covered) {
      detail::note_os_error(kUnmappedError);
      return;
    }
    // Two protections cannot be put back as one; that is no OS failure.
    if (next->prot != region->prot) {
      return;
    }
    covered = next->start + next->size;
  }
#ifdef SPUD_OS_WIN
  DWORD old_protection = 0;
  if (VirtualProtect(reinterpret_cast<LPVOID>(address_), size_, protection,
                     &old_protection) == 0) {
    detail::note_current_os_error();
    return;
  }
  original_protection_ = old_protection;
  aligned_address_ = address_;
  aligned_size_ = size_;
#elif SPUD_OS_APPLE
  const kern_return_t kr =
      mach_vm_protect(mach_task_self(), address_, size_, FALSE, protection);
  if (kr != KERN_SUCCESS) {
    detail::note_os_error(static_cast<int>(kr));
    return;
  }
  original_protection_ = static_cast<uintptr_t>(region->prot);
  aligned_address_ = address_;
  aligned_size_ = size_;
#elif SPUD_OS_LINUX
  const size_t page_size = memory_page_size();
  if (page_size == 0) {
    detail::note_current_os_error();
    return;
  }
  aligned_address_ = std::max(address & ~(page_size - 1), region->start);
  const uintptr_t aligned_end =
      std::min((address + size + page_size - 1) & ~(page_size - 1), covered);
  aligned_size_ = aligned_end - aligned_address_;
  if (mprotect(reinterpret_cast<void *>(aligned_address_), aligned_size_,
               protection) != 0) {
    detail::note_current_os_error();
    return;
  }
  original_protection_ = static_cast<uintptr_t>(region->prot);
  note_linux_prot(aligned_address_, aligned_end, protection);
#endif
  ok_ = true;
}

protection_scope::protection_scope(protection_scope &&other) noexcept
    : address_(other.address_), size_(other.size_),
      aligned_address_(other.aligned_address_),
      aligned_size_(other.aligned_size_),
      original_protection_(other.original_protection_), ok_(other.ok_) {
  other.detach();
}

protection_scope &
protection_scope::operator=(protection_scope &&other) noexcept {
  if (this != &other) {
    restore();
    address_ = other.address_;
    size_ = other.size_;
    aligned_address_ = other.aligned_address_;
    aligned_size_ = other.aligned_size_;
    original_protection_ = other.original_protection_;
    ok_ = other.ok_;
    other.detach();
  }
  return *this;
}

protection_scope::~protection_scope() {
  restore();
}

void protection_scope::restore() {
  if (address_ == 0 || !ok_) {
    return;
  }
  // A failed restore leaves the pages with the scope's protection. The
  // destructor cannot report it, so the OS error is the only trace.
#if SPUD_OS_WIN
  DWORD old_protection = 0;
  if (VirtualProtect(reinterpret_cast<LPVOID>(address_), size_,
                     static_cast<DWORD>(original_protection_),
                     &old_protection) == 0) {
    detail::note_current_os_error();
  }
#elif SPUD_OS_APPLE
  const kern_return_t kr =
      mach_vm_protect(mach_task_self(), address_, size_, FALSE,
                      static_cast<vm_prot_t>(original_protection_));
  if (kr != KERN_SUCCESS) {
    detail::note_os_error(static_cast<int>(kr));
  }
#elif SPUD_OS_LINUX
  if (mprotect(reinterpret_cast<void *>(aligned_address_), aligned_size_,
               static_cast<int>(original_protection_)) != 0) {
    detail::note_current_os_error();
  } else {
    note_linux_prot(aligned_address_, aligned_address_ + aligned_size_,
                    static_cast<int>(original_protection_));
  }
#endif
  detach();
}

namespace detail {

size_t read_code_prefix(uintptr_t address, std::span<uint8_t> out) {
  if (out.empty()) {
    return 0;
  }
#if defined(SPUD_OS_LINUX)
  const ssize_t read = vm_read(address, out);
  if (read >= 0) {
    return static_cast<size_t>(read);
  }
  // qemu-user does not implement process_vm_readv. procfs also bypasses
  // allocation-tag checks and reports an unreadable address as a short read.
  const int fd = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return 0;
  }
  const ssize_t copied =
      pread(fd, out.data(), out.size(), static_cast<off_t>(address));
  close(fd);
  return copied > 0 ? static_cast<size_t>(copied) : 0;
#elif defined(SPUD_OS_WIN)
  // ReadProcessMemory can reject a whole read crossing an inaccessible page.
  // Bound each read to its readable region to preserve the prefix contract.
  size_t total = 0;
  while (total < out.size()) {
    const auto region = query_native(address + total);
    if (!region || (region->prot & PAGE_GUARD) != 0 ||
        !has_protection(protection_from(region->prot), mem_protection::kRead))
      break;
    const auto length = std::min(
        out.size() - total, region->size - (address + total - region->start));
    SIZE_T copied = 0;
    ReadProcessMemory(GetCurrentProcess(),
                      reinterpret_cast<const void *>(address + total),
                      out.data() + total, length, &copied);
    total += copied;
    if (copied < length)
      break;
  }
  return total;
#elif defined(SPUD_OS_APPLE)
  // Mach rejects an entire read that crosses an unreadable mapping. Keep
  // the readable prefix, as process_vm_readv and ReadProcessMemory do.
  size_t total = 0;
  while (total < out.size()) {
    const auto region = query_native(address + total);
    if (!region || (region->prot & VM_PROT_READ) == 0)
      break;
    const size_t length = std::min(
        out.size() - total, region->size - (address + total - region->start));
    mach_vm_size_t copied = 0;
    const auto status = mach_vm_read_overwrite(
        mach_task_self(), address + total, length,
        reinterpret_cast<mach_vm_address_t>(out.data() + total), &copied);
    if (status != KERN_SUCCESS || copied == 0)
      break;
    total += copied;
    if (copied < length)
      break;
  }
  return total;
#endif
}

bool read_code_bytes(uintptr_t address, std::span<uint8_t> out) {
  return read_code_prefix(address, out) == out.size();
}

bool bti_guarded_page(uintptr_t address) {
#if SPUD_OS_LINUX && defined(__aarch64__)
  const auto region = query_native(address, true);
  // Without the page's attributes, assume hardware that can guard it does.
  if (!region) {
    return page_attributes_possible();
  }
  return (region->prot & kProtBti) != 0;
#else
  (void)address;
  return false;
#endif
}

std::vector<std::pair<uintptr_t, uintptr_t>> unmapped_ranges(uintptr_t lo,
                                                             uintptr_t hi) {
  std::vector<std::pair<uintptr_t, uintptr_t>> gaps;
  uintptr_t cursor = lo;
#if SPUD_OS_WIN
  // MEM_FREE regions are the gaps directly. The walk is bounded by the
  // range; each VirtualQuery steps over one region.
  while (cursor < hi) {
    MEMORY_BASIC_INFORMATION info;
    if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) ==
        0)
      break;
    const auto start = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const uintptr_t end = start + info.RegionSize;
    if (end <= cursor)
      break;
    if (info.State == MEM_FREE)
      gaps.emplace_back(std::max(start, cursor), std::min(end, hi));
    cursor = end;
  }
  return gaps;
#else
  auto mapped = [&](uintptr_t start, uintptr_t end) {
    if (start > cursor)
      gaps.emplace_back(cursor, std::min(start, hi));
    cursor = std::max(cursor, end);
  };
#if SPUD_OS_APPLE
  while (cursor < hi) {
    mach_vm_address_t addr = cursor;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    // Returns the first region at or above addr; failure means none is left.
    if (mach_vm_region(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64,
                       reinterpret_cast<vm_region_info_t>(&info), &count,
                       &object) != KERN_SUCCESS)
      break;
    if (object != MACH_PORT_NULL)
      mach_port_deallocate(mach_task_self(), object);
    if (addr >= hi)
      break;
    const uintptr_t before = cursor;
    mapped(static_cast<uintptr_t>(addr), static_cast<uintptr_t>(addr + size));
    if (cursor <= before)
      break;
  }
#elif SPUD_OS_LINUX
  // One query per mapping in the range, instead of formatting and parsing
  // every mapping in the process.
  linux_map m;
  auto found = procmap_result::found;
  while (cursor < hi) {
    // ENOENT (unmapped) here means nothing is mapped above the cursor.
    found = procmap_query(cursor, &m, kProcmapCoveringOrNext);
    if (found != procmap_result::found || m.start >= hi || m.end <= cursor)
      break;
    mapped(m.start, m.end);
  }
  if (found == procmap_result::unavailable) {
    gaps.clear();
    cursor = lo;
    std::vector<linux_map> maps;
    if (!parse_linux_maps(&maps, false))
      return gaps;
    for (const auto &entry : maps) {
      if (entry.end <= cursor)
        continue;
      if (entry.start >= hi)
        break;
      mapped(entry.start, entry.end);
    }
  }
#endif
  if (cursor < hi)
    gaps.emplace_back(cursor, hi);
  return gaps;
#endif
}

size_t readable_bytes_from(uintptr_t address, size_t limit) {
  size_t readable = 0;
  std::optional<bool> executable;
  while (readable < limit) {
    const auto region = find_region(address + readable);
    if (!region || !has_protection(region->protection, mem_protection::kRead)) {
      break;
    }
    const bool exec =
        has_protection(region->protection, mem_protection::kExecute);
    if (executable.value_or(exec) != exec) {
      break;
    }
    executable = exec;
    readable = region->start + region->size - address;
  }
  return std::min(readable, limit);
}

std::optional<memory_region> find_region(uintptr_t address) {
  if (address == 0) {
    return std::nullopt;
  }
  const auto region = query_native(address);
  if (!region) {
    return std::nullopt;
  }
  return memory_region{region->start, region->size,
                       protection_from(region->prot)};
}

#if SPUD_OS_APPLE
bool jit_mapping(uintptr_t address, size_t size) {
  mach_vm_address_t base = address;
  mach_vm_size_t length = 0;
  natural_t depth = 0;
  vm_region_submap_info_data_64_t info{};
  for (;;) {
    auto count = VM_REGION_SUBMAP_INFO_COUNT_64;
    if (mach_vm_region_recurse(
            mach_task_self(), &base, &length, &depth,
            reinterpret_cast<vm_region_recurse_info_t>(&info),
            &count) != KERN_SUCCESS)
      return false;
    if (!info.is_submap)
      break;
    ++depth;
  }
  bool jit = info.user_tag == kExecutableMemoryTag &&
             (info.max_protection & VM_PROT_WRITE) != 0;
#ifdef VM_REGION_FLAG_JIT_ENABLED
  jit |= (info.flags & VM_REGION_FLAG_JIT_ENABLED) != 0;
#endif
  return jit && address >= base && address - base < length &&
         size <= length - (address - base);
}
#endif

} // namespace detail

// The public calls below clear the calling thread's OS error on entry and
// record one on failure, so last_os_error() describes the call just made.
std::optional<memory_region> query_memory(uintptr_t address) {
  detail::clear_os_error();
  auto region = detail::find_region(address);
  if (!region) {
    detail::note_os_error(kUnmappedError);
  }
  return region;
}

bool protect_memory(uintptr_t address, size_t size, mem_protection protection) {
  detail::clear_os_error();
  protection_scope scope{address, size, protection};
  if (!scope.ok()) {
    return false;
  }
  scope.detach();
  return true;
}

bool read_memory(uintptr_t address, void *out, size_t size) {
  detail::clear_os_error();
  if (size == 0) {
    return true;
  }
  if (address == 0 || out == nullptr) {
    return false;
  }
  auto *bytes = static_cast<uint8_t *>(out);
  // The OS copy fails cleanly on an unreadable page, so only those pay for
  // a lookup per mapping. Linux skips read_code_bytes here: its procfs
  // fallback reads through any protection, PROT_NONE included.
#if SPUD_OS_LINUX
  if (vm_read(address, {bytes, size}) == static_cast<ssize_t>(size)) {
    return true;
  }
#else
  if (detail::read_code_bytes(address, {bytes, size})) {
    return true;
  }
#endif
  // One mapping at a time: an installed hook can leave a code page in a
  // mapping of its own, with a different protection from its neighbours.
  for (size_t done = 0; done < size;) {
    const uintptr_t at = address + done;
    const auto region = detail::find_region(at);
    if (!region) {
      detail::note_os_error(kUnmappedError);
      return false;
    }
    const size_t piece =
        std::min(size - done, region->start + region->size - at);
    std::optional<protection_scope> scope;
    if (!has_protection(region->protection, mem_protection::kRead)) {
      // Only execute-only code is made readable, and it stays executable
      // because another thread may be running it. Any other unreadable
      // page is unreadable on purpose: a guard page, or locked memory.
      if (region->protection != mem_protection::kExecute) {
        detail::note_os_error(kNoAccessError);
        return false;
      }
      scope.emplace(at, piece, mem_protection::kReadExecute);
      if (!scope->ok()) {
        return false;
      }
    }
    // Through the OS even when readable: a Windows guard page reads as
    // readable but faults on the first touch.
    if (!detail::read_code_bytes(at, {bytes + done, piece})) {
      detail::note_os_error(kNoAccessError);
      return false;
    }
    done += piece;
  }
  return true;
}

namespace {
// Writes [address, address + size), which lies inside the one mapping
// `native`.
bool write_mapping(uintptr_t address, const void *in, size_t size,
                   const native_region &native) {
  const auto protection = protection_from(native.prot);
  const bool executable = has_protection(protection, mem_protection::kExecute);
#if SPUD_OS_WIN
  const bool guarded = (native.prot & PAGE_GUARD) != 0;
#else
  constexpr bool guarded = false;
#endif
  if (!executable && !guarded &&
      has_protection(protection, mem_protection::kWrite)) {
    std::memcpy(reinterpret_cast<void *>(address), in, size);
    return true;
  }
#if SPUD_OS_APPLE
  if (executable && detail::jit_mapping(address, size)) {
    SPUD_SCOPED_JIT_WRITE();
    std::memcpy(reinterpret_cast<void *>(address), in, size);
    if (!flush_cache(reinterpret_cast<void *>(address), size)) {
      detail::note_current_os_error();
      return false;
    }
    return true;
  }
#endif
  const auto prot = executable ? mem_protection::kReadWriteExecute
                               : mem_protection::kReadWrite;
  protection_scope scope{address, size, prot};
  if (!scope.ok()) {
    return false;
  }
  {
    SPUD_SCOPED_JIT_WRITE();
    std::memcpy(reinterpret_cast<void *>(address), in, size);
  }
  if (executable && !flush_cache(reinterpret_cast<void *>(address), size)) {
    detail::note_current_os_error();
    return false;
  }
  return true;
}
} // namespace

bool write_memory(uintptr_t address, const void *in, size_t size) {
  detail::clear_os_error();
  if (size == 0) {
    return true;
  }
  if (address == 0 || in == nullptr || address + size < address) {
    return false;
  }
  // Refuse a range with a hole before writing any of it.
  for (uintptr_t at = address; at < address + size;) {
    const auto native = query_native(at);
    if (!native) {
      detail::note_os_error(kUnmappedError);
      return false;
    }
    at = native->start + native->size;
  }
  // One mapping at a time, each with its own protection: removing a hook
  // restores a prologue that can cross from a page the install split off
  // into the untouched one next to it. Each step looks its mapping up
  // again, since restoring the previous one can merge mappings back.
  const auto *bytes = static_cast<const uint8_t *>(in);
  for (size_t done = 0; done < size;) {
    const uintptr_t at = address + done;
    const auto native = query_native(at);
    if (!native) {
      detail::note_os_error(kUnmappedError);
      return false;
    }
    const size_t piece =
        std::min(size - done, native->start + native->size - at);
    if (!write_mapping(at, bytes + done, piece, *native)) {
      return false;
    }
    done += piece;
  }
  return true;
}

} // namespace spud

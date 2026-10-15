#include "executable_pool.h"
#include "detour/detour_impl.h"
#include "readable.h"
#include "unwind.h"

#include "memory/safe_patch.h"
#include <spud/arch.h>
#include <spud/diagnostics.h>
#include <spud/memory.h>
#include <spud/utils.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

#ifdef SPUD_OS_WIN
#include <windows.h>
#elif SPUD_OS_APPLE
#include <mach/mach.h>
#include <sys/mman.h>
#include <unistd.h>
#elif SPUD_OS_LINUX
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace spud::detail {

executable_relay::~executable_relay() {
  if (memory != nullptr)
    executable_pool::instance().deallocate(memory, size);
}

namespace {
struct retired_modules {
  std::vector<void *> values;
  ~retired_modules() {
    for (void *module : values)
      unwind_registration::close_source_module(module);
  }
};
} // namespace

static size_t align_up(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

bool executable_pool::add_would_overflow(size_t a, size_t b) {
  return b > (std::numeric_limits<size_t>::max() - a);
}

executable_pool::executable_pool() {
  free_list_.reserve(kMaxFreeBlocks);
}

executable_pool &executable_pool::instance() {
  static auto *pool = new executable_pool();
  return *pool;
}

executable_pool::~executable_pool() {
  for (auto &block : blocks_) {
    if (block.source_module != nullptr)
      unwind_registration::close_source_module(block.source_module);
  }
  blocks_.clear();
  for (auto &p : pages_) {
    os_free(p.mapped, p.mapped_size);
  }
}

size_t executable_pool::query_alloc_granularity() {
#ifdef SPUD_OS_WIN
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwAllocationGranularity;
#else
  return static_cast<size_t>(sysconf(_SC_PAGE_SIZE));
#endif
}

// How far a pool block may sit from the code that jumps into it: rel32 on
// x86-64, B on arm64. A short landing pad and the trampoline's direct forms
// both need it. Both ends of a block are checked; the 1MB margin is slack.
#if SPUD_ARCH_ARM64
constexpr uintptr_t kReach = (uintptr_t{128} << 20) - (uintptr_t{1} << 20);
#else
constexpr uintptr_t kReach = (uintptr_t{2048} << 20) - (uintptr_t{1} << 20);
#endif

bool executable_pool::within_reach(uintptr_t start, size_t size,
                                   uintptr_t hint) {
  auto reaches = [&](uintptr_t a) {
    return (a > hint ? a - hint : hint - a) < kReach;
  };
  return reaches(start) && reaches(start + size);
}

void *executable_pool::os_alloc(size_t size) {
#ifdef SPUD_OS_WIN
  void *addr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE,
                            PAGE_EXECUTE_READWRITE);
  if (addr == nullptr) {
    detail::note_current_os_error();
  }
  return addr;
#elif SPUD_OS_APPLE
  auto addr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT,
                   VM_MAKE_TAG(kExecutableMemoryTag), 0);
  if (addr == MAP_FAILED) {
    detail::note_current_os_error();
    return nullptr;
  }
  return addr;
#elif SPUD_OS_LINUX
  auto addr = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED) {
    detail::note_current_os_error();
    return nullptr;
  }
  return addr;
#endif
}

// Maps `size` bytes exactly at `addr`, or returns null.
void *executable_pool::os_map_at(uintptr_t addr, size_t size) {
  if (addr == 0) {
    return nullptr;
  }
#ifdef SPUD_OS_WIN
  return VirtualAlloc(reinterpret_cast<void *>(addr), size,
                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef SPUD_OS_APPLE
  flags |= MAP_JIT;
#endif
#ifdef MAP_FIXED_NOREPLACE
  flags |= MAP_FIXED_NOREPLACE;
#endif
  const int fd =
#if SPUD_OS_APPLE
      VM_MAKE_TAG(kExecutableMemoryTag);
#else
      -1;
#endif
  void *p = mmap(reinterpret_cast<void *>(addr), size,
                 PROT_READ | PROT_WRITE | PROT_EXEC, flags, fd, 0);
  if (p == MAP_FAILED) {
    return nullptr;
  }
  // Without MAP_FIXED_NOREPLACE the address is only a hint.
  if (p != reinterpret_cast<void *>(addr)) {
    munmap(p, size);
    return nullptr;
  }
  return p;
#endif
}

void *executable_pool::os_alloc_near(size_t size, uintptr_t hint) {
  if (size == 0 || hint == 0) {
    return nullptr;
  }
  const size_t gran = query_alloc_granularity();
  if (gran == 0) {
    return nullptr;
  }
  const uintptr_t aligned = hint & ~(static_cast<uintptr_t>(gran) - 1);
  const uintptr_t span = align_up(size, gran);

  // The kernel takes a plain hint when that range is free. A hint inside a
  // mapping, the usual case since it is the hook target, never is; the
  // kernel would put the block wherever it likes.
  if (!query_memory(aligned)) {
#ifdef SPUD_OS_WIN
    if (void *p = os_map_at(aligned, size)) {
      return p;
    }
#else
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef SPUD_OS_APPLE
    flags |= MAP_JIT;
#endif
    const int fd =
#if SPUD_OS_APPLE
        VM_MAKE_TAG(kExecutableMemoryTag);
#else
        -1;
#endif
    void *hinted = mmap(reinterpret_cast<void *>(aligned), size,
                        PROT_READ | PROT_WRITE | PROT_EXEC, flags, fd, 0);
    if (hinted != MAP_FAILED) {
      if (within_reach(reinterpret_cast<uintptr_t>(hinted), size, hint)) {
        return hinted;
      }
      munmap(hinted, size);
    }
#endif
  }

  // Walk the unmapped gaps within reach and try the edge of each one that
  // is nearest the hint. A busy target can have mappings all around it
  // while a gap a few hundred MB away is still free.
  // Nothing maps below 64KB (mmap_min_addr, the Windows null region).
  const uintptr_t lo =
      std::max(hint > kReach ? hint - kReach : 0, uintptr_t{1} << 16);
  const uintptr_t hi =
      hint < UINTPTR_MAX - kReach ? hint + kReach : UINTPTR_MAX;
  struct candidate {
    uintptr_t addr;
    uintptr_t distance;
  };
  std::vector<candidate> candidates;
  for (const auto &[gap_start, gap_end] : unmapped_ranges(lo, hi)) {
    if (gap_end - gap_start < span)
      continue;
    const uintptr_t first = align_up(gap_start, gran);
    const uintptr_t last =
        (gap_end - span) & ~(static_cast<uintptr_t>(gran) - 1);
    if (first == 0 || first > last)
      continue;
    const uintptr_t addr = std::clamp(aligned, first, last);
    if (within_reach(addr, span, hint))
      candidates.push_back({addr, addr > hint ? addr - hint : hint - addr});
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const candidate &a, const candidate &b) {
              return a.distance < b.distance;
            });
  // Another thread can map into a gap between the walk and our map call.
  // The next gap is a fine answer; past a few dozen, give up.
  constexpr size_t kMaxTries = 32;
  for (size_t i = 0; i < candidates.size() && i < kMaxTries; ++i) {
    if (void *p = os_map_at(candidates[i].addr, size)) {
      return p;
    }
  }
  detail::note_current_os_error();
  return nullptr;
}

void executable_pool::os_free(void *ptr, size_t size) {
#ifdef SPUD_OS_WIN
  (void)size;
  VirtualFree(ptr, 0, MEM_RELEASE);
#else
  munmap(ptr, size);
#endif
}

void *executable_pool::allocate_from_free_list(size_t size, uintptr_t hint) {
  for (auto it = free_list_.begin(); it != free_list_.end(); ++it) {
    if (it->size < size) {
      continue;
    }
    if (hint != 0 &&
        !within_reach(reinterpret_cast<uintptr_t>(it->ptr), size, hint)) {
      continue;
    }
    void *ptr = it->ptr;
    size_t remaining = it->size - size;
    if (remaining >= kAlignment) {
      it->ptr = static_cast<uint8_t *>(ptr) + size;
      it->size = remaining;
    } else {
      free_list_.erase(it);
    }
    return ptr;
  }
  return nullptr;
}

executable_pool::page *executable_pool::adopt_page(void *mem, size_t size) {
  if (mem == nullptr || size <= kPagePrefix) {
    if (mem != nullptr) {
      os_free(mem, size);
    }
    return nullptr;
  }
  page p{};
  p.mapped = mem;
  p.mapped_size = size;
  p.base = static_cast<uint8_t *>(mem) + kPagePrefix;
  p.capacity = size - kPagePrefix;
  p.bump = 0;
  const auto at = std::upper_bound(
      pages_.begin(), pages_.end(), p.base,
      [](void *base, const page &other) { return base < other.base; });
  reserved_bytes_ += size;
  return &*pages_.insert(at, p);
}

executable_pool::page *executable_pool::find_page(const void *ptr) {
  const auto it = std::upper_bound(
      pages_.begin(), pages_.end(), ptr,
      [](const void *p, const page &other) { return p < other.base; });
  if (it == pages_.begin())
    return nullptr;
  page &p = *std::prev(it);
  const auto *base = static_cast<const uint8_t *>(p.base);
  return static_cast<const uint8_t *>(ptr) < base + p.capacity ? &p : nullptr;
}

std::vector<executable_pool::block_unwind>::iterator
executable_pool::find_block(void *ptr) {
  const auto it = std::lower_bound(
      blocks_.begin(), blocks_.end(), ptr,
      [](const block_unwind &block, void *p) { return block.ptr < p; });
  return it != blocks_.end() && it->ptr == ptr ? it : blocks_.end();
}

executable_pool::block_unwind &
executable_pool::insert_block(block_unwind block) {
  const auto it = std::lower_bound(
      blocks_.begin(), blocks_.end(), block.ptr,
      [](const block_unwind &other, void *p) { return other.ptr < p; });
  return *blocks_.insert(it, std::move(block));
}

void *executable_pool::allocate(size_t requested_size) {
  return allocate_near(requested_size, 0);
}

void *executable_pool::allocate_near(size_t requested_size, uintptr_t hint,
                                     unwind_metadata unwind) {
  if (requested_size == 0 || g_suspend_window_depth != 0) {
    return nullptr;
  }
  retired_modules modules;
  auto lock = lock_for_alloc();
  if (!lock.owns_lock()) {
    return nullptr;
  }
  drain_deferred(modules.values);
  const size_t prefix = unwind == unwind_metadata::kLeaf
                            ? kWindowsLeafUnwindBytes
                        : unwind != unwind_metadata::kNone ? kWindowsUnwindBytes
                                                           : 0;
  if (add_would_overflow(requested_size, prefix)) {
    ++alloc_failures_;
    return nullptr;
  }
  void *ptr = allocate_locked(requested_size + prefix, hint);
  if (ptr == nullptr && hint != 0) {
    ptr = allocate_locked(requested_size + prefix, 0);
  }
  if (ptr == nullptr) {
    ++alloc_failures_;
    return nullptr;
  }
#if SPUD_UNWIND_SUPPORT
  if (unwind != unwind_metadata::kNone) {
    ptr = static_cast<uint8_t *>(ptr) + prefix;
    total_allocated_ -= prefix;
  }
#endif
  sizes_.emplace(ptr, align_up(requested_size, kAlignment));
#if SPUD_UNWIND_SUPPORT
  if (unwind != unwind_metadata::kNone) {
    // Record the prefix before publication so a failed registration frees
    // the same physical block as a successful one.
    auto &block = insert_block({ptr, nullptr, nullptr, prefix, nullptr});
    // describe_trampoline publishes a trampoline's records.
    if (unwind == unwind_metadata::kLeaf) {
      auto &registration = block.registration;
      registration = std::make_unique<unwind_registration>();
      if (!registration->prepare_leaf(reinterpret_cast<uintptr_t>(ptr),
                                      requested_size) ||
          !registration->publish()) {
        deallocate_locked(ptr, align_up(requested_size, kAlignment),
                          modules.values);
        ++alloc_failures_;
        ptr = nullptr;
      }
    }
  }
#endif
  return ptr;
}

// A zero hint accepts any address.
void *executable_pool::allocate_locked(size_t requested_size, uintptr_t hint) {
  if (requested_size == 0 ||
      add_would_overflow(requested_size, kAlignment - 1)) {
    return nullptr;
  }
  const size_t size = align_up(requested_size, kAlignment);
  if (chunk_size_ == 0) {
    chunk_size_ = std::max(kChunkBytes, query_alloc_granularity());
  }

  if (void *ptr = allocate_from_free_list(size, hint)) {
    total_allocated_ += size;
    return ptr;
  }

  for (auto it = pages_.rbegin(); it != pages_.rend(); ++it) {
    if (add_would_overflow(it->bump, kAlignment - 1)) {
      continue;
    }
    const size_t aligned_bump = align_up(it->bump, kAlignment);
    const auto at = reinterpret_cast<uintptr_t>(it->base) + aligned_bump;
    if (hint != 0 && !within_reach(at, size, hint)) {
      continue;
    }
    if (!add_would_overflow(aligned_bump, size) &&
        aligned_bump + size <= it->capacity) {
      it->bump = aligned_bump + size;
      total_allocated_ += size;
      return static_cast<uint8_t *>(it->base) + aligned_bump;
    }
  }

  if (add_would_overflow(size, kPagePrefix + chunk_size_)) {
    return nullptr;
  }
  const size_t map_size = align_up(size + kPagePrefix, chunk_size_);
  if (map_size > kMaxReservedBytes - reserved_bytes_) {
    return nullptr;
  }
  void *mem = hint != 0 ? os_alloc_near(map_size, hint) : os_alloc(map_size);
  page *pg = adopt_page(mem, map_size);
  if (pg == nullptr) {
    return nullptr;
  }
  pg->bump = size;
  total_allocated_ += size;
  return pg->base;
}

// A freed range that ends at its page's bump pointer goes back to the bump
// allocator. The range has already absorbed any free neighbour below, so
// one step is enough.
//
// A page with nothing handed out is unmapped, except that one ordinary
// empty page stays per reach window. Installing and removing a single hook
// would otherwise map and unmap a page, and search the address space for a
// spot near the target, on every cycle.
bool executable_pool::return_to_bump(void *ptr, size_t size) {
  page *p = find_page(ptr);
  if (p == nullptr)
    return false;
  auto *start = static_cast<uint8_t *>(ptr);
  auto *base = static_cast<uint8_t *>(p->base);
  if (start + size != base + p->bump)
    return false;
  p->bump = static_cast<size_t>(start - base);
  if (p->bump != 0)
    return true;
  const auto at = reinterpret_cast<uintptr_t>(p->base);
  const bool spare_nearby =
      std::any_of(pages_.begin(), pages_.end(), [&](const page &other) {
        return &other != p && other.bump == 0 &&
               within_reach(reinterpret_cast<uintptr_t>(other.base),
                            other.capacity, at);
      });
  if (p->mapped_size == chunk_size_ && !spare_nearby)
    return true;
  os_free(p->mapped, p->mapped_size);
  reserved_bytes_ -= p->mapped_size;
  pages_.erase(pages_.begin() + (p - pages_.data()));
  return true;
}

// False when the block had to be dropped: the free list is full and the
// block borders nothing free.
bool executable_pool::insert_free_block(void *ptr, size_t size) {
  auto it =
      std::lower_bound(free_list_.begin(), free_list_.end(), ptr,
                       [](const free_block &a, void *p) { return a.ptr < p; });
  auto *bytes = static_cast<uint8_t *>(ptr);
  const bool has_prev = it != free_list_.begin();
  const auto *prev_end = has_prev ? static_cast<uint8_t *>(std::prev(it)->ptr) +
                                        std::prev(it)->size
                                  : nullptr;
  // Merge before counting entries: a block that touches a free neighbour
  // never needs a slot of its own, however full the list is.
  const bool joins_prev = has_prev && prev_end == bytes;
  const bool joins_next = it != free_list_.end() && bytes + size == it->ptr;
  if (joins_prev) {
    it = std::prev(it);
    it->size += size;
    if (joins_next) {
      it->size += std::next(it)->size;
      free_list_.erase(std::next(it));
    }
  } else if (joins_next) {
    it->ptr = ptr;
    it->size += size;
  } else if (return_to_bump(ptr, size)) {
    return true;
  } else if (free_list_.size() >= kMaxFreeBlocks) {
    return false;
  } else {
    it = free_list_.insert(it, {ptr, size});
  }
  if (return_to_bump(it->ptr, it->size))
    free_list_.erase(it);
  return true;
}

void executable_pool::deallocate(void *ptr, size_t size) {
  if (!ptr || size == 0 || add_would_overflow(size, kAlignment - 1)) {
    return;
  }
  size = align_up(size, kAlignment);
  retired_modules modules;
  auto lock = lock_for_alloc();
  // Inside the window a frozen thread may hold the lock, and the deferred
  // list cannot grow. Either way the block leaks rather than wait.
  if (!lock.owns_lock()) {
    dropped_bytes_.fetch_add(size, std::memory_order_relaxed);
    return;
  }
  if (g_suspend_window_depth != 0) {
    if (deferred_count_ < deferred_.size())
      deferred_[deferred_count_++] = {ptr, size};
    else
      dropped_bytes_.fetch_add(size, std::memory_order_relaxed);
    return;
  }
  drain_deferred(modules.values);
  deallocate_locked(ptr, size, modules.values);
}

void executable_pool::drain_deferred(std::vector<void *> &modules) {
  for (size_t i = 0; i < deferred_count_; ++i) {
    deallocate_locked(deferred_[i].ptr, deferred_[i].size, modules);
  }
  deferred_count_ = 0;
}

void executable_pool::deallocate_locked(void *ptr, size_t size,
                                        std::vector<void *> &modules) {
  // The recorded size is the authority. Trusting the caller's would put
  // live neighbours on the free list or hand a freed block out twice, so a
  // release build refuses the free and leaves the block allocated.
  const auto known = sizes_.find(ptr);
  if (known == sizes_.end() || known->second != size) {
    assert(false && "executable memory freed twice, never allocated, or with "
                    "the wrong size");
    return;
  }
  sizes_.erase(known);
  size_t prefix = 0;
  std::unique_ptr<executable_relays> relays;
  const auto it = find_block(ptr);
  if (it != blocks_.end()) {
    relays = std::move(it->relays);
    prefix = it->prefix;
    if (it->registration != nullptr) {
      if (void *module = it->registration->release_source_module())
        modules.push_back(module);
    }
    if (it->source_module != nullptr)
      modules.push_back(it->source_module);
    blocks_.erase(it);
  }
  if (relays != nullptr)
    for (auto &relay : relays->entries) {
      const auto [memory, length] = relay.release();
      deallocate_locked(memory, align_up(length, kAlignment), modules);
    }
  if (insert_free_block(static_cast<uint8_t *>(ptr) - prefix, size + prefix))
    total_freed_ += size;
  else
    dropped_bytes_.fetch_add(size, std::memory_order_relaxed);
}

bool executable_pool::describe_trampoline(
    void *ptr, size_t size, uintptr_t entry, uintptr_t source, size_t copied,
    trampoline_buffer &trampoline, bool unwind,
    std::span<const size_t> instruction_offsets,
    const unwind_registration **out_registration, size_t patch_size,
    source_view code) {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
#if !SPUD_UNWIND_SUPPORT
  (void)code;
  (void)size;
  (void)entry;
  (void)copied;
  (void)trampoline;
  (void)instruction_offsets;
  (void)patch_size;
#endif
  if (!SPUD_UNWIND_SUPPORT || !unwind) {
    if (out_registration != nullptr)
      *out_registration = nullptr;
    void *module = nullptr;
    if (!unwind_registration::retain_source_module(source, module))
      return false;
    if (module != nullptr || trampoline.relays != nullptr) {
      // Code still calls the source body when its unwind records are disabled.
      auto lock = lock_for_alloc();
      insert_block({ptr, nullptr, module, 0, std::move(trampoline.relays)});
    }
    return true;
  }
#if SPUD_UNWIND_SUPPORT
  auto registration = std::make_unique<unwind_registration>();
  if (!registration->prepare(reinterpret_cast<uintptr_t>(ptr), size, entry,
                             source, copied, trampoline, instruction_offsets,
                             patch_size, code)) {
    return false;
  }
  if (trampoline.relays != nullptr)
    for (const auto &relay : trampoline.relays->entries)
      registration->add_dependency(relay.metadata);
  auto lock = lock_for_alloc();
  const auto it = find_block(ptr);
  if (it == blocks_.end() || it->registration != nullptr ||
      it->relays != nullptr)
    return false;
  if (!registration->publish())
    return false;
  it->registration = std::move(registration);
  it->relays = std::move(trampoline.relays);
  if (out_registration != nullptr)
    *out_registration = it->registration.get();
  return true;
#endif
}

std::unique_lock<std::mutex> executable_pool::lock_for_alloc() {
  if (g_suspend_window_depth != 0) {
    // Deferred frees may arrive while a frozen thread owns this mutex.
    return std::unique_lock<std::mutex>(mu_, std::try_to_lock);
  }
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  return std::unique_lock<std::mutex>(mu_);
}

pool_stats executable_pool::stats() const {
  std::lock_guard<std::mutex> lock(mu_);
  pool_stats s;
  s.live_bytes = total_allocated_ - total_freed_ -
                 dropped_bytes_.load(std::memory_order_relaxed);
  s.reserved_bytes = reserved_bytes_;
  for (const auto &b : free_list_) {
    s.free_bytes += b.size;
  }
  s.alloc_failures = alloc_failures_;
  s.dropped_bytes = dropped_bytes_.load(std::memory_order_relaxed);
  return s;
}

} // namespace spud::detail

namespace spud {

pool_stats query_pool_stats() {
  return detail::executable_pool::instance().stats();
}

} // namespace spud

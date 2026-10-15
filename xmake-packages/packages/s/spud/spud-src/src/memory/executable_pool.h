#pragma once

#include "memory/unwind.h"

#include <spud/arch.h>
#include <spud/diagnostics.h>
#if SPUD_OS_APPLE
#include <mach/vm_statistics.h>
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace spud::detail {
#if SPUD_OS_APPLE
// Identifies pool mappings after Rosetta drops their MAP_JIT region flag.
inline constexpr unsigned kExecutableMemoryTag =
    VM_MEMORY_APPLICATION_SPECIFIC_16;
#endif
class unwind_registration;
struct trampoline_buffer;
struct executable_relays;

class executable_pool {
public:
  // Process-lifetime owner of executable pages. Intentionally never
  // destroyed: static hooks can run their destructors after other function-
  // local statics, and unmapping first would leave them walking freed pages.
  static executable_pool &instance();

  // Unwind records an allocation carries. A stub gets a frameless leaf
  // record. A trampoline reserves the metadata window and leaves its only
  // registration to describe_trampoline.
  enum class unwind_metadata { kNone, kLeaf, kTrampoline };

  // Allocate `size` bytes of executable memory from the pool.
  // Returns nullptr on failure, including size 0 and alignment overflow.
  // Alignment is 16 bytes. Thread-safe outside the suspend window.
  void *allocate(size_t size);

  // Same, preferring an address a direct branch at `hint` can reach (2GB on
  // x86-64, 128MB on arm64), so a short landing pad and the trampoline's
  // PC-relative forms fit. Falls back to allocate() when nothing nearby is
  // free. Hook preparation uses one unwind choice for sizing, emission and
  // allocation.
  void *allocate_near(size_t size, uintptr_t hint,
                      unwind_metadata unwind = unwind_metadata::kNone);

  // Free a previously allocated block, returning it to the free list.
  // Thread-safe outside the suspend window. Does not allocate: when the
  // free list is full, a block with no free neighbour and not at its page's
  // bump pointer leaks for process lifetime rather than growing the list.
  void deallocate(void *ptr, size_t size);
  bool describe_trampoline(void *ptr, size_t size, uintptr_t entry,
                           uintptr_t source, size_t copied,
                           trampoline_buffer &trampoline, bool unwind,
                           std::span<const size_t> instruction_offsets = {},
                           const unwind_registration **registration = nullptr,
                           size_t patch_size = 0, source_view code = {});

  pool_stats stats() const;

private:
  executable_pool();
  ~executable_pool();

  executable_pool(const executable_pool &) = delete;
  executable_pool &operator=(const executable_pool &) = delete;

  static constexpr size_t kAlignment = 16;
  static constexpr size_t kMaxFreeBlocks = 1024;
  // Pages are mapped in chunks of at least this size, or the allocation
  // granularity when that is larger (64KB on Windows, where a smaller
  // VirtualAlloc wastes the rest of the granule).
  static constexpr size_t kChunkBytes = size_t{64} << 10;
  // A hook needs a few hundred bytes, so 256MB is room for hundreds of
  // thousands of them. The bound exists to stop a leak from mapping RWX
  // memory without end, not to ration ordinary use.
  static constexpr size_t kMaxReservedBytes = size_t{256} << 20;
  // Clang's function sanitizer reads a signature eight bytes before an
  // indirect-call target. Keep the first allocation off the page boundary.
  static constexpr size_t kPagePrefix = kAlignment;
  size_t chunk_size_ = 0;

  struct page {
    // OS mapping.
    void *mapped;
    size_t mapped_size;
    // Usable bump region.
    void *base;
    size_t capacity;
    size_t bump;
  };

  struct free_block {
    void *ptr;
    size_t size;
  };

  mutable std::mutex mu_;
  // Sorted by address, as are free_list_ and blocks_.
  std::vector<page> pages_;
  std::vector<free_block> free_list_;
  struct block_unwind {
    void *ptr;
    std::unique_ptr<unwind_registration> registration;
    void *source_module = nullptr;
    size_t prefix = 0;
    std::unique_ptr<executable_relays> relays;
  };
  std::vector<block_unwind> blocks_;
  // Size of every live allocation, keyed by the pointer handed out.
  std::unordered_map<void *, size_t> sizes_;
  std::array<free_block, kMaxFreeBlocks> deferred_{};
  size_t deferred_count_ = 0;
  void drain_deferred(std::vector<void *> &modules);
  void deallocate_locked(void *ptr, size_t size, std::vector<void *> &modules);

  size_t total_allocated_ = 0;
  size_t total_freed_ = 0;
  size_t reserved_bytes_ = 0;
  size_t alloc_failures_ = 0;
  // Freed but leaked. Atomic because a free inside the window can find the
  // lock held by a frozen thread.
  std::atomic<size_t> dropped_bytes_{0};

  void *allocate_locked(size_t size, uintptr_t hint);
  void *allocate_from_free_list(size_t size, uintptr_t hint);
  std::unique_lock<std::mutex> lock_for_alloc();
  page *adopt_page(void *mem, size_t size);
  page *find_page(const void *ptr);
  std::vector<block_unwind>::iterator find_block(void *ptr);
  block_unwind &insert_block(block_unwind block);
  bool insert_free_block(void *ptr, size_t size);
  bool return_to_bump(void *ptr, size_t size);

  // Whether code at `hint` can branch to every byte of [start, start + size).
  static bool within_reach(uintptr_t start, size_t size, uintptr_t hint);
  static size_t query_alloc_granularity();
  static void *os_alloc(size_t size);
  void *os_alloc_near(size_t size, uintptr_t hint);
  static void *os_map_at(uintptr_t addr, size_t size);
  static void os_free(void *ptr, size_t size);
  static bool add_would_overflow(size_t a, size_t b);
};

} // namespace spud::detail

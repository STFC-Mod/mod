#include "detour/chain.h"
#include "detour/detour_stub.h"
#include "memory/safe_patch.h"
#include <spud/arch.h>
#include <spud/detour.h>
#include <spud/memory.h>
#include <spud/utils.h>

#include "control_flow.h"
#include "detour_impl.h"
#include "memory/executable_pool.h"
#include "memory/readable.h"

#if SPUD_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unwind.h>
#endif

#if SPUD_OS_APPLE
#include <libkern/OSCacheControl.h>
#include <mach/kern_return.h>
#include <mach/mach_init.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#endif

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace spud {
namespace detail {

std::atomic<bool> g_force_far_landing_for_tests{false};

hook_node *new_hook_node(bool counted) {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  const size_t size = sizeof(hook_node) + (counted ? sizeof(entry_counts) : 0);
  void *memory = ::operator new(size, std::align_val_t{alignof(hook_node)});
  auto *node = new (memory) hook_node();
  node->counted = counted;
  if (counted)
    new (node->counts()) entry_counts();
  return node;
}

void delete_hook_node(hook_node *node) {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  if (node == nullptr)
    return;
  if (node->counted)
    node->counts()->~entry_counts();
  node->~hook_node();
  ::operator delete(node, std::align_val_t{alignof(hook_node)});
}

// What an unlinked hook leaves behind: its node and the pool block a
// thread can still be running for it. Unlinking only stops new calls from
// reaching the hook. This is freed once no call is counted in the node
// and no thread runs or returns into that code, or retired to a
// process-wide list until then. An anchor whose last hook was freed drains
// the same way, with no node.
struct unlinked_hook {
  hook_node *node = nullptr;
  hook_anchor *anchor = nullptr;
  // The counted wrapper the context's stub jumps to, used to spot a caller
  // that is itself inside the hook.
  uintptr_t wrapper = 0;
  // A reference to the handle's callable, which the calls being drained
  // may still be running.
  callable_box *callable = nullptr;
  std::pair<void *, size_t> block{};
  // Set inside a stop once nothing can reach the hook.
  bool drained = false;
  // While retired: stops that found it idle but still reachable, and when
  // an install or uninstall may stop threads for it again.
  uint32_t misses = 0;
  uint64_t next_scan_ns = 0;
  // The operation that retired it.
  uint64_t operation = 0;
  unlinked_hook *next = nullptr;

  bool empty() const {
    return node == nullptr && anchor == nullptr && block.first == nullptr;
  }
  ip_range code() const {
    return {reinterpret_cast<uintptr_t>(block.first), block.second};
  }
};

// A detour a transaction unlinked, kept until the transaction ends so a
// rollback can put it back where it was.
struct staged_unlink {
  bool linked_out = false;
  // The removal was the target's last and wrote the original bytes back.
  bool unhooked = false;
  hook_node *above = nullptr;
  hook_node *below = nullptr;
  // Drained after the window unless a rollback takes it back.
  unlinked_hook left;
  // The never-linked node the handle held when a rollback took its old one
  // back.
  hook_node *returned = nullptr;
};

struct DetourImpl {
  stub_bytes (*create_absolute_jump)(uintptr_t target, uintptr_t data,
                                     bool counted);
  std::tuple<relocation_info, size_t> (*collect_relocations)(
      uintptr_t address, size_t jump_size,
      std::span<const uint8_t> original_prefix, size_t function_size);
  trampoline_buffer (*create_trampoline)(
      uintptr_t at, uintptr_t return_address, std::span<const uint8_t> target,
      const relocation_info &relocation_infos, bool sizing_only, bool unwind);
  uintptr_t (*maybe_resolve_jump)(uintptr_t);
};

namespace {

// The backend for `arch`, or null when this build cannot hook it. 32-bit x86
// has none.
const DetourImpl *hook_backend(Arch arch) {
  static constexpr DetourImpl kX64{
      .create_absolute_jump = x64::create_absolute_jump,
      .collect_relocations = x64::collect_relocations,
      .create_trampoline = x64::create_trampoline,
      .maybe_resolve_jump = x64::maybe_resolve_jump};
#if SPUD_AARCH64_SUPPORT
  static constexpr DetourImpl kArm64{
      .create_absolute_jump = arm64::create_absolute_jump,
      .collect_relocations = arm64::collect_relocations,
      .create_trampoline = arm64::create_trampoline,
      .maybe_resolve_jump = arm64::maybe_resolve_jump};
#endif
  switch (arch) {
  case Arch::kX86_64:
    return &kX64;
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return &kArm64;
#endif
  default:
    return nullptr;
  }
}

// Writes bytes to the hook target, which is not normally writable.
// Window-safe: syscalls + memcpy only.
bool patch_target_bytes(uintptr_t address, const void *data, size_t size) {
  SPUD_SCOPED_JIT_WRITE();
#if SPUD_OS_APPLE
  // MAP_JIT memory is writable through the thread's JIT scope. Remapping
  // it is forbidden and would discard the mapping's JIT attributes.
  const auto region = query_memory(address);
  if (region && has_protection(region->protection, mem_protection::kWrite) &&
      size <= region->size - (address - region->start)) {
    std::memcpy(reinterpret_cast<void *>(address), data, size);
    return flush_cache(reinterpret_cast<void *>(address), size);
  }
  // A JIT mapping can report RX while the scoped thread write permission
  // still permits stores; see jit_mapping().
  if (jit_mapping(address, size)) {
    std::memcpy(reinterpret_cast<void *>(address), data, size);
    return flush_cache(reinterpret_cast<void *>(address), size);
  }
  // Code pages are written through an alias, then the alias is mapped back
  // over them. A shared initial alias plus cache invalidation updates
  // Rosetta's translation. Its write protection makes a private copy before
  // publish; requesting that copy at remap time can leave the old
  // translation active.
#if defined(SPUD_ARCH_X86_64)
  constexpr boolean_t copy = FALSE;
#else
  constexpr boolean_t copy = TRUE;
#endif
  mach_vm_address_t alias = 0;
  vm_prot_t cur = 0;
  vm_prot_t max = 0;
  kern_return_t status = mach_vm_remap(
      mach_task_self(), &alias, size, 0,
      VM_FLAGS_ANYWHERE | VM_FLAGS_RETURN_DATA_ADDR, mach_task_self(), address,
      copy, &cur, &max, VM_INHERIT_NONE);
  if (status != KERN_SUCCESS || alias == 0) {
    note_os_error(static_cast<int>(status));
    return false;
  }
  struct alias_mapping {
    mach_vm_address_t address;
    size_t size;
    ~alias_mapping() {
      mach_vm_deallocate(mach_task_self(), address, size);
    }
  } owned{alias, size};
  {
    protection_scope protect{alias, size, mem_protection::kReadWriteExecute};
    if (!protect.ok()) {
      return false;
    }
    std::memcpy(reinterpret_cast<void *>(alias), data, size);
#if defined(SPUD_ARCH_X86_64)
    (void)flush_cache(reinterpret_cast<void *>(address), size);
#endif
  }
  if (g_inject_patch_fault_for_tests.load(std::memory_order_relaxed) ==
      static_cast<uint8_t>(patch_fault::remap))
    return false;
  // The published mapping must survive fork(), including untouched code on
  // the same page. Publish only after its original RX protection is restored.
  mach_vm_address_t published = address;
  status = mach_vm_remap(mach_task_self(), &published, size, 0,
                         VM_FLAGS_OVERWRITE | VM_FLAGS_RETURN_DATA_ADDR,
                         mach_task_self(), alias, FALSE, &cur, &max,
                         VM_INHERIT_COPY);
  if (status != KERN_SUCCESS) {
    note_os_error(static_cast<int>(status));
    return false;
  }
  return flush_cache(reinterpret_cast<void *>(address), size);
#else
  protection_scope protect{address, size, mem_protection::kReadWriteExecute};
  if (!protect.ok()) {
    return false;
  }
  std::memcpy(reinterpret_cast<void *>(address), data, size);
  return flush_cache(reinterpret_cast<void *>(address), size);
#endif
}

bool target_bytes_match(uintptr_t address, std::span<const uint8_t> expected) {
  std::array<uint8_t, 512> bytes{};
  for (size_t off = 0; off < expected.size();) {
    const size_t length = std::min(bytes.size(), expected.size() - off);
    if (!read_code_bytes(address + off, {bytes.data(), length}) ||
        std::memcmp(bytes.data(), expected.data() + off, length) != 0) {
      return false;
    }
    off += length;
  }
  return true;
}

// After the shard counts read zero, how long removal keeps stopping threads
// to look for one still running or returning into the hook's code. That is
// rare and brief once the counts are zero, and a conservative stack scan can
// keep seeing a stale pointer under traffic, so the hook retires instead of
// holding up uninstall().
constexpr uint64_t kDrainScanNs = 50'000'000;

// Removed hooks still waiting to drain, newest first. Only what is pending
// lives here, never a live hook. Guarded by the patch lock.
unlinked_hook *g_retired = nullptr;
size_t g_retired_count = 0;

void delete_anchor(hook_anchor *anchor) {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  // A stale landing decoded later must not validate against freed memory
  // that still holds these bytes.
  anchor->magic = 0;
  delete anchor;
}

// Frees an anchor nothing was ever linked through.
struct anchor_deleter {
  void operator()(hook_anchor *anchor) const {
    if (anchor->block != nullptr)
      free_executable_memory(anchor->block, anchor->block_size);
    delete_anchor(anchor);
  }
};
using anchor_ptr = std::unique_ptr<hook_anchor, anchor_deleter>;

unlinked_hook anchor_leftover(hook_anchor *anchor) {
  unlinked_hook left;
  left.anchor = anchor;
  left.block = {anchor->block, anchor->block_size};
  return left;
}

// Uses the pool and the heap: call outside the window. Returns the hook's
// anchor when this was the last reference to it; its block drains next.
hook_anchor *free_unlinked(unlinked_hook &left) {
  if (left.block.first != nullptr)
    free_executable_memory(left.block.first, left.block.second);
  hook_anchor *orphan = nullptr;
  if (left.node != nullptr) {
    if (left.node->anchor != nullptr &&
        release_node(*left.node->anchor, left.node))
      orphan = left.node->anchor;
    delete_hook_node(left.node);
  }
  if (left.anchor != nullptr)
    delete_anchor(left.anchor);
  // Last: a callable's destructor is user code and may call back into spud.
  release_callable(left.callable);
  return orphan;
}

// A trampoline lease keeps the hook's original() callable, and so its
// block and its next slot, which the anchor keeps current while it drains.
bool leased(const unlinked_hook &left) {
  return left.node != nullptr &&
         left.node->pins.load(std::memory_order_acquire) != 0;
}

// Inside a stop: whether no thread can still reach `left`. The counts alone
// miss a thread between the dispatch and the stub's increment, a thread
// running the relocated prologue, and a pending return into it.
bool drained(safe_patch_session &patch, const unlinked_hook &left) {
  if (leased(left) || (left.node != nullptr && left.node->active()))
    return false;
  if (!patch.suspended()) {
    // Running threads: the counts are all there is to go on.
    return unguarded_patch_permitted();
  }
  const ip_range code = left.code();
  return !patch.any_ip_in_ranges(&code, 1) &&
         !patch.any_return_in_ranges(&code, 1);
}

// Counts operations, so a sweep can tell the hooks retired before it began.
uint64_t g_operation = 0;

void retire(unlinked_hook &left) {
  auto *retired = new unlinked_hook(left);
  retired->operation = g_operation;
  retired->next = std::exchange(g_retired, retired);
  ++g_retired_count;
}

// No call is counted in it and no lease holds it, so only a stop can tell
// whether it drained.
bool idle(const unlinked_hook &left) {
  return !leased(left) && (left.node == nullptr || !left.node->active());
}

// A retired hook whose counts are zero but that a stop still finds
// reachable is usually held by a stale stack word that may stay put for a
// long time. Installs and uninstalls retry it after 10ms, doubling up to a
// second, so it costs them at most one stop of every thread per second.
// spud::collect() always retries.
constexpr uint64_t kRetryBaseNs = 10'000'000;
constexpr uint64_t kRetryMaxNs = 1'000'000'000;

// Inside a stop: marks the retired hooks no thread can reach any more and
// backs off the idle ones it still finds reachable. Installs and uninstalls
// call this in the stop they take for their own work, so the retired list
// rarely needs a stop of its own. free_retired() frees what it marked.
void scan_retired(safe_patch_session &patch) {
  const uint64_t now = monotonic_ns();
  for (unlinked_hook *left = g_retired; left != nullptr; left = left->next) {
    left->drained = drained(patch, *left);
    if (left->drained || !idle(*left))
      continue;
    left->misses = std::min<uint32_t>(left->misses + 1, 11);
    left->next_scan_ns =
        now + std::min(kRetryBaseNs << (left->misses - 1), kRetryMaxNs);
  }
}

// After scan_retired's stop. Returns whether freeing a hook left an anchor
// with no references, which then waits in the retired list.
bool free_retired() {
  // Unhook the drained entries first: freeing runs callable destructors,
  // which may install or remove hooks and so collect again.
  unlinked_hook *done = nullptr;
  for (unlinked_hook **link = &g_retired; *link != nullptr;) {
    unlinked_hook *left = *link;
    if (!left->drained) {
      link = &left->next;
      continue;
    }
    *link = left->next;
    left->next = done;
    done = left;
    --g_retired_count;
  }
  bool orphaned = false;
  while (done != nullptr) {
    unlinked_hook *left = std::exchange(done, done->next);
    if (hook_anchor *orphan = free_unlinked(*left)) {
      unlinked_hook anchor = anchor_leftover(orphan);
      retire(anchor);
      orphaned = true;
    }
    delete left;
  }
  return orphaned;
}

// Frees retired hooks that have drained, then the anchors that freed up.
// Takes a stop per pass, and only when some retired hook is idle and,
// unless `force`, due for another look and retired before `operation`.
// Patch lock held.
size_t collect_retired(bool force = false, uint64_t operation = UINT64_MAX) {
  if (g_suspend_window_depth != 0)
    return g_retired_count;
  for (;;) {
    const uint64_t now = monotonic_ns();
    bool due = false;
    for (unlinked_hook *left = g_retired; left != nullptr; left = left->next) {
      due |=
          idle(*left) &&
          (force || (now >= left->next_scan_ns && left->operation < operation));
    }
    if (!due)
      break;
    {
      SPUD_SCOPED_SAFE_PATCH(patch);
      scan_retired(patch);
    }
    if (!free_retired())
      break;
  }
  return g_retired_count;
}

// One install, uninstall or transaction's share of collecting retired
// hooks. A stop the operation takes for its own work scans the retired list
// too (scan()), and the sweep frees what that found when it goes out of
// scope. An operation that took no stop collects as before, but not the
// hooks it retired itself, which have had no chance to drain yet. Inert
// inside a transaction's window, whose own sweep covers it. Declare it
// after the patch lock, so it runs while that is still held.
struct retired_sweep {
  const bool active = g_suspend_window_depth == 0;
  const uint64_t operation = ++g_operation;
  bool scanned = false;

  ~retired_sweep() {
    if (!active)
      return;
    if (scanned)
      free_retired();
    else
      collect_retired(false, operation);
  }
  void scan(safe_patch_session &patch) {
    if (active && !scanned && !patch.nested()) {
      scan_retired(patch);
      scanned = true;
    }
  }
};

// Whether `[low, high)` or one of `registers` points into `left`'s entry
// shards. The range is a frame of the calling thread's own stack.
#if defined(__GNUC__) || defined(__clang__)
[[gnu::no_sanitize_address]]
#endif
bool frame_names(const unlinked_hook &left, uintptr_t low, uintptr_t high,
                 std::span<const uintptr_t> registers) {
  const auto lo = reinterpret_cast<uintptr_t>(left.node->counts());
  const uintptr_t hi = lo + sizeof(entry_counts);
  auto names = [&](uintptr_t word) { return word >= lo && word < hi; };
  if (std::any_of(registers.begin(), registers.end(), names))
    return true;
  for (uintptr_t at = low; at + sizeof(uintptr_t) <= high;
       at += sizeof(uintptr_t)) {
    if (names(*reinterpret_cast<const volatile uintptr_t *>(at)))
      return true;
  }
  return false;
}

// Whether the calling thread is inside the hook `left` unlinked: removing a
// hook from its own callback, or from something that callback called,
// counts the remover's own call until the remover returns. Hooks with one
// signature and callable type share a wrapper, so a wrapper frame on this
// stack does not say which hook it runs. The wrapper keeps the entry shard
// it decrements on exit live across its call into the hook, in a
// callee-saved register or a slot of its own frame, and the unwinder
// recovers both as they were in that frame. Removal's own frames, which
// also name the hook's context, are not looked at. A stale copy in the
// wrapper's frame can only make removal retire the hook instead of waiting
// for it. Unwinds, which allocates on some platforms: call outside the
// suspend window.
bool calling_thread_inside(const unlinked_hook &left) {
#if SPUD_OS_WIN
  CONTEXT context{};
  RtlCaptureContext(&context);
  for (int depth = 0; depth < 256 && context.Rip != 0; ++depth) {
    DWORD64 base = 0;
    auto *function = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
    if (function == nullptr) {
      // A leaf: the return address is at the stack pointer.
      context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
      context.Rsp += sizeof(DWORD64);
      continue;
    }
    const bool wrapper = base + function->BeginAddress == left.wrapper;
    const uintptr_t low = context.Rsp;
    const uintptr_t registers[] = {context.Rbx, context.Rbp, context.Rsi,
                                   context.Rdi, context.R12, context.R13,
                                   context.R14, context.R15};
    void *handler_data = nullptr;
    DWORD64 establisher = 0;
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, function, &context,
                     &handler_data, &establisher, nullptr);
    if (wrapper && frame_names(left, low, context.Rsp, registers))
      return true;
  }
  return false;
#else
  // DWARF numbers of the callee-saved general registers.
#if SPUD_ARCH_X86_64
  static constexpr int kSaved[] = {3, 6, 12, 13, 14, 15};
#else
  static constexpr int kSaved[] = {19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29};
#endif
  struct search {
    const unlinked_hook &left;
    // A wrapper frame seen last: its stack pointer, which the unwinder
    // reports as the frame's CFA, and its registers. The frame ends at the
    // next frame's stack pointer.
    uintptr_t low = 0;
    uintptr_t registers[std::size(kSaved)] = {};
    bool inside = false;
  } state{left};
  _Unwind_Backtrace(
      [](_Unwind_Context *context, void *opaque) {
        auto &s = *static_cast<search *>(opaque);
        if (_Unwind_GetIP(context) == 0)
          return _URC_NO_REASON;
        const uintptr_t sp = _Unwind_GetCFA(context);
        if (s.low != 0 && frame_names(s.left, s.low, sp, s.registers)) {
          s.inside = true;
          return _URC_NORMAL_STOP;
        }
        s.low = 0;
        if (_Unwind_GetRegionStart(context) == s.left.wrapper) {
          s.low = sp;
          for (size_t i = 0; i < std::size(kSaved); ++i)
            s.registers[i] = _Unwind_GetGR(context, kSaved[i]);
        }
        return _URC_NO_REASON;
      },
      &state);
  return state.inside;
#endif
}

// Inside a stop: marks the unlinked hooks no thread can reach any more, so
// the drain needs no stop for them.
void mark_drained(safe_patch_session &patch, std::span<unlinked_hook> items) {
  for (unlinked_hook &left : items) {
    if (!left.drained)
      left.drained = drained(patch, left);
  }
}

// Adds each anchor whose landing is gone and that only the hooks in `work`
// still name. It drains in the same stops as they do, and is freed after
// them when they all were. Allocates nothing while `work` has room for one
// more entry per hook.
void add_orphaned_anchors(std::vector<unlinked_hook> &work) {
  const size_t hooks = work.size();
  for (size_t i = 0; i < hooks; ++i) {
    hook_anchor *anchor =
        work[i].node != nullptr ? work[i].node->anchor : nullptr;
    if (anchor == nullptr || anchor->hooked)
      continue;
    const bool listed =
        std::any_of(work.begin(), work.end(), [&](const unlinked_hook &left) {
          return left.anchor == anchor;
        });
    const auto naming = static_cast<size_t>(
        std::count_if(work.begin(), work.end(), [&](const unlinked_hook &left) {
          return left.node != nullptr && left.node->anchor == anchor;
        }));
    if (!listed && naming == anchor->refs)
      work.push_back(anchor_leftover(anchor));
  }
}

// Waits for unlinked hooks to drain: first for their counts to reach zero
// with threads running, then with short stops for threads still in their
// code. Marks the ones that did. The first stop also scans the retired
// list for `sweep`. Patch lock held, outside the window.
void drain_unlinked(std::span<unlinked_hook> items, retired_sweep &sweep) {
  const uint64_t deadline = monotonic_ns() + quiesce_timeout_ns();
  std::vector<bool> waiting(items.size());
  for (size_t i = 0; i < items.size(); ++i) {
    unlinked_hook &left = items[i];
    const bool counted = left.node != nullptr && left.node->active();
    left.drained |= !counted && left.block.second == 0;
    // Neither the remover's own call nor a lease ends while we wait.
    waiting[i] = !left.drained && !leased(left) &&
                 !(counted && left.wrapper != 0 && calling_thread_inside(left));
  }
  auto any_waiting = [&] {
    return std::find(waiting.begin(), waiting.end(), true) != waiting.end();
  };
  auto counting = [&] {
    for (size_t i = 0; i < items.size(); ++i) {
      if (waiting[i] && items[i].node != nullptr && items[i].node->active())
        return true;
    }
    return false;
  };
  while (counting()) {
    if (monotonic_ns() >= deadline)
      return;
    std::this_thread::yield();
  }
  const uint64_t scan_deadline =
      std::min(deadline, monotonic_ns() + kDrainScanNs);
  while (any_waiting()) {
    {
      SPUD_SCOPED_SAFE_PATCH(patch);
      for (size_t i = 0; i < items.size(); ++i) {
        if (!waiting[i])
          continue;
        unlinked_hook &left = items[i];
        const ip_range code = left.code();
        // The calling thread's own pending return never drains here.
        if (patch.any_own_return_in_ranges(&code, 1)) {
          waiting[i] = false;
          continue;
        }
        left.drained = drained(patch, left);
        waiting[i] = !left.drained;
      }
      if (!sweep.scanned)
        sweep.scan(patch);
    }
    if (!any_waiting() || monotonic_ns() >= scan_deadline)
      return;
    std::this_thread::yield();
  }
}

// Frees what drained (after waiting, when asked to) and retires the rest.
// Entries already marked drained, by a stop the caller took, are freed
// without another. An anchor whose last hook this frees goes with them.
// Patch lock held, outside the window.
void release_unlinked(std::span<unlinked_hook> items, bool wait,
                      retired_sweep &sweep) {
  std::vector<unlinked_hook> work(items.begin(), items.end());
  for (unlinked_hook &left : items)
    left = {};
  add_orphaned_anchors(work);
  if (wait)
    drain_unlinked(work, sweep);
  // Hooks first: freeing the last one that names an anchor orphans it, and
  // only an anchor listed here can be orphaned by them.
  std::vector<hook_anchor *> orphans;
  for (unlinked_hook &left : work) {
    if (left.anchor != nullptr)
      continue;
    if (left.drained || left.empty()) {
      if (hook_anchor *orphan = free_unlinked(left))
        orphans.push_back(orphan);
    } else {
      retire(left);
    }
  }
  for (unlinked_hook &left : work) {
    if (left.anchor == nullptr ||
        std::find(orphans.begin(), orphans.end(), left.anchor) == orphans.end())
      continue;
    if (left.drained)
      free_unlinked(left);
    else
      retire(left);
  }
  // Not expected: an anchor is only orphaned when every hook naming it was
  // in `work`, which then listed it. Retired rather than leaked if so.
  for (hook_anchor *orphan : orphans) {
    if (std::none_of(work.begin(), work.end(), [&](const unlinked_hook &left) {
          return left.anchor == orphan;
        })) {
      unlinked_hook anchor = anchor_leftover(orphan);
      retire(anchor);
    }
  }
}

// Preserve an existing x86 IBT entry. On ARM64, unguarded pages take a
// bare branch; guarded pages keep bti c or receive bti jc before the branch.
// PACIASP/PACIBSP must not precede the hook because they sign LR.
// TODO(alex): a guarded target whose first word is no landing can only be
// reached by direct branches, so a bare branch would be safe there too.
uint32_t landing_for(Arch arch, uintptr_t address,
                     std::span<const uint8_t> original_prefix) {
  if (arch == Arch::kX86_64) {
    // An entry without endbr64 was not a valid tracked indirect target
    // before hooking either. Preserve existing pads instead of adding one.
    return x64::needs_ibt_landing(address, original_prefix) ? x64::kEndbrLanding
                                                            : 0;
  }
#if SPUD_AARCH64_SUPPORT
  if (arch != Arch::kArm64)
    return 0;
  if (!bti_guarded_page(address))
    return arm64::kNoLanding;
  uint32_t first = 0;
  if (original_prefix.size() >= sizeof(first)) {
    std::memcpy(&first, original_prefix.data(), sizeof(first));
  } else if (!read_code_bytes(address,
                              {reinterpret_cast<uint8_t *>(&first), 4})) {
    return arm64::kBtiJc;
  }
  return first == arm64::kBtiC ? arm64::kBtiC : arm64::kBtiJc;
#else
  (void)arch;
  (void)address;
  (void)original_prefix;
  return 0;
#endif
}

// An anchor for a target nothing hooks yet: its block holds the dispatch and
// the relocated prologue, and the landing is ready to write.
struct prepared_fresh {
  detour_error error = detour_error::kInsufficientBytes;
  anchor_ptr anchor;
  // The bytes the prologue was relocated from, checked again at the write.
  relocation_info source;

  bool source_matches() const {
    if (anchor->metadata != nullptr && !anchor->metadata->source_matches())
      return false;
    return target_bytes_match(source.source_address, source.source_bytes);
  }
};

prepared_fresh prepare_fresh_hook(uintptr_t address, Arch arch,
                                  const DetourImpl &impl,
                                  std::span<const uint8_t> original_prefix = {},
                                  size_t function_size = 0) {
  prepared_fresh result;
  const bool unwind = unwind_support_enabled();
  const uint32_t pad = landing_for(arch, address, original_prefix);
  const bool force_far =
      g_force_far_landing_for_tests.load(std::memory_order_relaxed);
  for (const bool short_patch : {true, false}) {
    if (short_patch && force_far)
      continue;
    const size_t patch = short_patch ? short_stub_size_for_arch(arch, pad)
                                     : far_stub_size_for_arch(arch, pad);
    auto [source, required] = impl.collect_relocations(
        address, patch, original_prefix, function_size);
    // The far landing overwrites more than the short one and is refused for
    // anything the short one is; it is only worth trying when the pool
    // block landed out of branch reach. The anchor still goes near when it
    // can, where the trampoline keeps its PC-relative forms short.
    if (required < patch) {
      result.error = source.refusal;
      return result;
    }
    const std::span<const uint8_t> prologue{
        reinterpret_cast<const uint8_t *>(address), required};
    const auto bound = impl.create_trampoline(0, address + required, prologue,
                                              source, true, unwind);
    if (bound.data.empty()) {
      result.error = detour_error::kDecodeFailed;
      return result;
    }
    const size_t total = kAnchorPrefixSize + bound.data.size();
    void *mem = executable_pool::instance().allocate_near(
        total, address,
        unwind ? executable_pool::unwind_metadata::kTrampoline
               : executable_pool::unwind_metadata::kNone);
    if (mem == nullptr) {
      result.error = detour_error::kProtectionFailed;
      return result;
    }
    const uintptr_t body = reinterpret_cast<uintptr_t>(mem);
    auto trampoline =
        impl.create_trampoline(body + kAnchorPrefixSize, address + required,
                               prologue, source, false, unwind);
    if (trampoline.data.empty() || trampoline.data.size() > bound.data.size()) {
      free_executable_memory(mem, total);
      result.error = detour_error::kDecodeFailed;
      return result;
    }
    auto landing = short_patch
                       ? create_short_jump_for_arch(arch, address, body, pad)
                       : create_far_jump_for_arch(arch, body, pad);
    if (landing.empty()) {
      free_executable_memory(mem, total);
      continue;
    }
    anchor_ptr anchor{new hook_anchor()};
    anchor->target = address;
    anchor->arch = arch;
    anchor->block = mem;
    anchor->block_size = total;
    anchor->original = body + kAnchorPrefixSize + trampoline.start;
    const unwind_registration **metadata = nullptr;
#if SPUD_UNWIND_SUPPORT
    metadata = &anchor->metadata;
#endif
    // Relays the trampoline jumps through are written first; its unwind
    // record depends on theirs.
    if (!trampoline.publish_relays(body + kAnchorPrefixSize, address, required,
                                   source, unwind, &result.error))
      return result;
    // The prefix before the trampoline entry gets a leaf record.
    if (!executable_pool::instance().describe_trampoline(
            mem, total, anchor->original, address, required, trampoline, unwind,
            source.instruction_offsets, metadata, landing.size(),
            {source.source_address, source.source_bytes})) {
      result.error = detour_error::kUnwindFailed;
      return result;
    }
    const auto dispatch =
        create_indirect_jump_for_arch(arch, body, body + kAnchorHeadOffset);
    {
      SPUD_SCOPED_JIT_WRITE();
      auto *bytes = static_cast<uint8_t *>(mem);
      std::memcpy(bytes, dispatch.data(), dispatch.size());
      // No hook yet: the dispatch falls through to the original.
      std::memcpy(bytes + kAnchorHeadOffset, &anchor->original,
                  sizeof(uintptr_t));
      hook_anchor *owner = anchor.get();
      std::memcpy(bytes + kAnchorOwnerOffset, &owner, sizeof(owner));
      std::memcpy(bytes + kAnchorPrefixSize, trampoline.data.data(),
                  trampoline.data.size());
    }
    if (!flush_cache(mem, total)) {
      result.error = detour_error::kProtectionFailed;
      return result;
    }
    if (!register_call_target(mem, total)) {
      result.error = detour_error::kControlFlowEnforced;
      return result;
    }
    // The source bytes start with the original prologue even when the
    // target is hooked now (original_prefix), which is what removal puts back.
    anchor->saved_bytes.assign(source.source_bytes.begin(),
                               source.source_bytes.begin() +
                                   static_cast<std::ptrdiff_t>(landing.size()));
    anchor->landing = landing;
    anchor->required = required;
    anchor->relocation_offsets = std::move(trampoline.relocation_offsets);
    result.error = detour_error::kOk;
    result.anchor = std::move(anchor);
    result.source = std::move(source);
    return result;
  }
  return result;
}

// What an install needs from the pool and heap, made before any stop: a
// node block always, and an anchor in case the target is not hooked yet.
} // namespace

struct prepared_install {
  std::optional<prepared_fresh> fresh;
  void *block = nullptr;
  size_t block_size = 0;

  prepared_install() = default;
  prepared_install(const prepared_install &) = delete;
  prepared_install &operator=(const prepared_install &) = delete;
  ~prepared_install() {
    if (block != nullptr)
      free_executable_memory(block, block_size);
  }
};

namespace {

// Node blocks hold only stubs that build no frame, so a leaf record lets
// stack walks pass through them. They need not be near the target.
void *allocate_node(Arch arch, uintptr_t hint) {
  return executable_pool::instance().allocate_near(
      node_block_size(arch), hint,
      unwind_support_enabled() ? executable_pool::unwind_metadata::kLeaf
                               : executable_pool::unwind_metadata::kNone);
}

// Fills a node block and hands it to `node`. `next` is where original() goes
// until the chain says otherwise. The code is flushed before link_top
// publishes the entry with a release store, so a thread that loads the new
// pointer finds the finished stub: x86-64 keeps instruction fetch coherent
// with stores, and on ARM64 the flush is the dc cvau / ic ivau / dsb ish
// sequence that makes the bytes visible to every core's fetch before the
// pointer is. A block can only be reused after a stop proved no thread was
// in it, and resuming from that stop is a context synchronization event, so
// no core holds its old instructions prefetched.
detour_error write_node(hook_node *node, void *mem, Arch arch,
                        const stub_bytes &entry, uintptr_t next) {
  const size_t size = node_block_size(arch);
  const auto base = reinterpret_cast<uintptr_t>(mem);
  const uintptr_t original = base + node_original_offset(arch);
  const uintptr_t slot = base + node_next_offset(arch);
  const auto jump = create_indirect_jump_for_arch(arch, original, slot);
  {
    SPUD_SCOPED_JIT_WRITE();
    auto *bytes = static_cast<uint8_t *>(mem);
    std::memcpy(bytes, entry.data(), entry.size());
    std::memcpy(bytes + node_original_offset(arch), jump.data(), jump.size());
    std::memcpy(bytes + node_next_offset(arch), &next, sizeof(next));
  }
  if (!flush_cache(mem, size)) {
    free_executable_memory(mem, size);
    return detour_error::kProtectionFailed;
  }
  if (!register_call_target(mem, size)) {
    free_executable_memory(mem, size);
    return detour_error::kControlFlowEnforced;
  }
  node->block = mem;
  node->block_size = size;
  node->context.trampoline = original;
  return detour_error::kOk;
}

// Writes the anchor's landing at its target. A thread inside the bytes the
// relocated prologue copied moves to its place in the copy. Frees nothing.
// `stranded` reports threads left in the anchor's block after a failed
// write, which then must not be freed without draining.
template <typename Verify>
detour_error hook_target(safe_patch_session &patch, hook_anchor &anchor,
                         Verify &&verify, bool *stranded = nullptr) {
  if (!patch_can_commit(patch)) {
    return detour_error::kSuspensionUnavailable;
  }
  if (decode_landing(anchor.target, anchor.arch, nullptr) || !verify()) {
    return detour_error::kTargetChanged;
  }
  // The whole copied span, not just the landing: the copy only runs past the
  // landing when a later loop jumps back into it or a later instruction
  // reads the patched bytes, and a thread returning into the original
  // there would do the same against the landing.
  const ip_range returns[] = {{anchor.target + 1, anchor.required - 1}};
  if (patch.any_return_in_ranges(returns, std::size(returns))) {
    return detour_error::kBusy;
  }
  if (!patch.redirect_ips(anchor.target, anchor.required, anchor.original,
                          anchor.relocation_offsets)) {
    return detour_error::kSuspensionUnavailable;
  }
  if (!patch_target_bytes(anchor.target, anchor.landing.data(),
                          anchor.landing.size())) {
    // Send threads moved into the copy back to the start of the target.
    static const std::vector<std::pair<uintptr_t, uintptr_t>> identity{};
    const bool reversed =
        patch.redirect_ips(reinterpret_cast<uintptr_t>(anchor.block),
                           anchor.block_size, anchor.target, identity, true);
    if (stranded != nullptr)
      *stranded = !reversed;
    return detour_error::kProtectionFailed;
  }
  anchor.hooked = true;
  return detour_error::kOk;
}

// Puts the original bytes back over the landing. A thread stopped inside
// the landing, or exactly at the target, has not reached any hook; it
// restarts in the relocated prologue, which stays until the anchor drains.
// Leaving one at the target to run the restored bytes is not enough under
// Rosetta: a thread can stop inside the translation of the far landing's
// `jmp [rip]` with its x86 IP still at the target, and resume to load the
// destination from bytes that are now the original code. Rewriting its IP
// makes Rosetta leave that translation.
detour_error unhook_target(safe_patch_session &patch, hook_anchor &anchor) {
  if (!patch_can_commit(patch)) {
    return detour_error::kSuspensionUnavailable;
  }
  landing_info landing;
  if (!decode_landing(anchor.target, anchor.arch, &landing) ||
      landing.anchor != &anchor) {
    return detour_error::kTargetChanged;
  }
  if (!patch.redirect_ips(anchor.target, landing.size, anchor.original, {},
                          true)) {
    return detour_error::kSuspensionUnavailable;
  }
  if (!patch_target_bytes(anchor.target, anchor.saved_bytes.data(),
                          anchor.saved_bytes.size())) {
    return detour_error::kProtectionFailed;
  }
  anchor.hooked = false;
  return detour_error::kOk;
}

} // namespace

detour_error detour::install(Arch arch) {
  return record_error([&] { return install_impl(arch); });
}

detour_error detour::install_permanent(Arch arch) {
  return record_error([&] {
    // An installed hook keeps its counting stub and simply stops being
    // removable.
    const detour_error err = state_ == handle_state::kInstalled
                                 ? detour_error::kOk
                                 : install_impl(arch, true);
    if (err == detour_error::kOk) {
      // The hook keeps a reference to the callable for good.
      retain_callable(callable_);
      state_ = handle_state::kDetached;
    }
    return err;
  });
}

detour_error detour::install_impl(Arch arch, bool permanent, hook_node **spare,
                                  prepared_install *prepared) {
  if (state_ == handle_state::kInstalled) {
    return detour_error::kOk;
  }
  // A detached handle gave its hook away, and that hook still runs; another
  // install would hook the target a second time with the same callable.
  if (address_ == 0 || state_ == handle_state::kDetached) {
    return detour_error::kNotFound;
  }
  // A node left by a failed permanent install has no counts to reuse.
  if (node_ != nullptr && !permanent && !node_->counted) {
    delete_hook_node(std::exchange(node_, nullptr));
  }
  if (node_ == nullptr) {
    node_ = spare != nullptr && *spare != nullptr
                ? std::exchange(*spare, nullptr)
                : new_hook_node(!permanent);
  }

#if SPUD_ARCH_ARM64E
  return detour_error::kControlFlowEnforced;
#endif

  const DetourImpl *backend = hook_backend(arch);
  if (backend == nullptr) {
    return detour_error::kUnsupportedArch;
  }
  const DetourImpl &impl = *backend;

  // Serializes whole install/uninstall operations (atomics only, no OS lock),
  // then warms caches so the suspend window never touches them lazily.
  patch_flag_guard patch_lock;
  prewarm_memory_page_size();
  retired_sweep sweep;

  // We don't want to hook things that point to a direct jump. This resolves
  // that jump and hooks the underlying function instead. Hooking an
  // already-hooked address stacks: the resolve would stop at our own
  // landing, which leads to the target's anchor, so that is checked first.
  landing_info hooked;
  bool stacked = decode_landing(address_, arch, &hooked);
  if (!stacked) {
    const uintptr_t resolved = impl.maybe_resolve_jump(address_);
    if (resolved != address_) {
      if (function_size_ != 0)
        return detour_error::kDecodeFailed;
      address_ = resolved;
      stacked = decode_landing(address_, arch, &hooked);
    }
  }
  // An incremental-link thunk in front of a wrapper would put a jump
  // between the stub and the code that reads its registers.
  if (!wrappers_resolved_) {
    wrapper_ = impl.maybe_resolve_jump(wrapper_);
    uncounted_wrapper_ = impl.maybe_resolve_jump(uncounted_wrapper_);
    wrappers_resolved_ = true;
  }
  arch_ = arch;

  if (stacked && hooked.anchor->live >= kMaxChainLinks) {
    return detour_error::kChainFull;
  }

  detour_context &context = node_->context;
  context.func = func_;
  context.callable = callable_ != nullptr ? callable_->object : nullptr;
#if SPUD_DETOUR_TRACING
  context.location = location_;
#endif

  // A transaction prepared the fresh path before its stop when the target
  // was unhooked or an earlier step could unhook it.
  prepared_install local;
  if (prepared == nullptr) {
    prepared = &local;
    if (!stacked)
      prepared->fresh =
          prepare_fresh_hook(address_, arch, impl, {}, function_size_);
    prepared->block_size = node_block_size(arch);
    prepared->block = allocate_node(arch, address_);
  }
  prepared_fresh *fresh =
      stacked || !prepared->fresh ? nullptr : &*prepared->fresh;
  if (fresh != nullptr && fresh->error != detour_error::kOk) {
    return fresh->error;
  }
  if (prepared->block == nullptr) {
    return detour_error::kProtectionFailed;
  }
  if (!stacked && fresh == nullptr) {
    // Unhooked by something outside the transaction since it prepared.
    return detour_error::kTargetChanged;
  }
  hook_anchor &anchor = stacked ? *hooked.anchor : *fresh->anchor;

  // The stub counts at the node's counts, which a permanent node does not
  // have; its stub never touches them, and the wrapper only reads the
  // context in front.
  const auto entry = impl.create_absolute_jump(
      permanent ? uncounted_wrapper_ : wrapper_,
      reinterpret_cast<uintptr_t>(node_->counts()), !permanent);
  const uintptr_t next =
      anchor.top != nullptr ? node_entry(*anchor.top) : anchor.original;
  detour_error err = write_node(node_, std::exchange(prepared->block, nullptr),
                                arch, entry, next);
  if (err != detour_error::kOk) {
    return err;
  }
  node_->anchor = &anchor;
  ++anchor.refs;
  // On a hooked target the new hook is live once the head slot names it:
  // no thread stop and no write to the target.
  link_top(anchor, node_);
  if (!stacked) {
    bool stranded = false;
    {
      SPUD_SCOPED_SAFE_PATCH(patch);
      err = hook_target(
          patch, anchor, [&] { return fresh->source_matches(); }, &stranded);
      sweep.scan(patch);
    }
    if (err != detour_error::kOk) {
      // The landing never went in, so nothing reached the node. Threads a
      // failed write left in the anchor's copy keep it alive for good.
      unlink_node(anchor, node_);
      release_node(anchor, node_);
      free_executable_memory(std::exchange(node_->block, nullptr),
                             node_->block_size);
      node_->anchor = nullptr;
      context.trampoline = 0;
      if (stranded)
        (void)fresh->anchor.release();
      return err;
    }
    // Owned from here by the hooks that name it.
    (void)fresh->anchor.release();
  }
  state_ = handle_state::kInstalled;
  return detour_error::kOk;
}

detour_error detour::remove() {
  return record_error([&] { return unlink(nullptr, true); });
}

detour_error detour::request_remove() {
  return record_error([&] { return unlink(nullptr, false); });
}

trampoline_pin detour::pin_trampoline() {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  patch_flag_guard lock;
  trampoline_pin pin;
  if (!is_installed() || node_ == nullptr)
    return pin;
  // Only this hook: original() follows its next slot, which removing a
  // hook below repoints before that hook's memory goes.
  node_->pins.fetch_add(1, std::memory_order_relaxed);
  pin.pins_ = &node_->pins;
  pin.address_ = node_->context.trampoline;
  return pin;
}

uintptr_t detour::trampoline() const {
  return node_ != nullptr ? node_->context.trampoline : 0;
}

detour_error detour::unlink(staged_unlink *staged, bool wait) {
  // The hook a detached handle let go of is not its to remove.
  if (state_ == handle_state::kDetached) {
    return detour_error::kNotFound;
  }
  if (node_ == nullptr) {
    return detour_error::kOk;
  }
  if (state_ != handle_state::kInstalled) {
    // A node a failed install left behind, never linked.
    if (staged == nullptr)
      delete_hook_node(std::exchange(node_, nullptr));
    return detour_error::kOk;
  }

  patch_flag_guard patch_lock;
  prewarm_memory_page_size();
  retired_sweep sweep;

  hook_node *const node = node_;
  hook_anchor &anchor = *node->anchor;
  hook_node *const above = node->above;
  hook_node *const below = node->below;
  const bool last = anchor.live == 1;
  // The hook, then its anchor when nothing else names it any more.
  unlinked_hook left[2];
  left[0].node = node;
  left[0].block = {node->block, node->block_size};
  size_t leftovers = 1;
  if (last) {
    if (anchor.refs == 1)
      left[leftovers++] = anchor_leftover(&anchor);
    // The only write to the target besides the first install's.
    detour_error err = detour_error::kOk;
    {
      SPUD_SCOPED_SAFE_PATCH(patch);
      err = unhook_target(patch, anchor);
      if (err == detour_error::kOk) {
        unlink_node(anchor, node);
        // Usually nothing is inside the hook any more; finding that out in
        // this stop spares the drain one of its own.
        if (staged == nullptr)
          mark_drained(patch, {left, leftovers});
      }
      sweep.scan(patch);
    }
    if (err == detour_error::kTargetChanged && staged == nullptr) {
      // Something else rewrote the target. It may have copied our landing
      // into its own trampoline, so the hook can still be entered and is
      // leaked rather than freed. The handle lets go of it.
      retain_callable(callable_);
      state_ = handle_state::kDetached;
    }
    if (err != detour_error::kOk) {
      return err;
    }
  } else {
    // A pointer store. Calls that already loaded this hook's entry still
    // arrive; the drain below waits for them.
    unlink_node(anchor, node);
  }

  node_ = nullptr;
  state_ = handle_state::kIdle;

  if (staged != nullptr) {
    staged->linked_out = true;
    staged->unhooked = last;
    staged->above = above;
    staged->below = below;
    staged->left = left[0];
    return detour_error::kOk;
  }
  left[0].wrapper = wrapper_;
  left[0].callable = callable_;
  retain_callable(callable_);
  release_unlinked({left, leftovers}, wait, sweep);
  return detour_error::kOk;
}

detour_error detour::relink(staged_unlink &staged) {
  if (!staged.linked_out) {
    return detour_error::kOk;
  }
  // The handle takes its old node back. One it held instead was never
  // linked and is freed with the staged state.
  staged.returned =
      std::exchange(node_, std::exchange(staged.left.node, nullptr));
  state_ = handle_state::kIdle;
  hook_anchor &anchor = *node_->anchor;
  detour_error err = detour_error::kOk;
  {
    patch_flag_guard patch_lock;
    if (staged.unhooked) {
      // The anchor kept everything the landing needs. Its source was checked
      // when it was built; the target must still hold what removal wrote.
      SPUD_SCOPED_SAFE_PATCH(patch);
      err = hook_target(patch, anchor, [&] {
        return target_bytes_match(anchor.target, anchor.saved_bytes) &&
               (anchor.metadata == nullptr ||
                anchor.metadata->source_matches());
      });
      if (err == detour_error::kOk)
        relink_node(anchor, node_, staged.above, staged.below);
    } else {
      relink_node(anchor, node_, staged.above, staged.below);
    }
  }
  if (err != detour_error::kOk) {
    // Still unlinked: the node drains with the rest of the staged state.
    staged.left.node =
        std::exchange(node_, std::exchange(staged.returned, nullptr));
    state_ = handle_state::kIdle;
    return err;
  }
  staged.left = {};
  staged.linked_out = false;
  state_ = handle_state::kInstalled;
  return detour_error::kOk;
}

detail::detour &detour::detach() {
  if (state_ == handle_state::kDetached) {
    return *this;
  }
  if (state_ != handle_state::kInstalled) {
    // Never installed: nothing is linked, just drop a leftover node.
    delete_hook_node(std::exchange(node_, nullptr));
    state_ = handle_state::kDetached;
    return *this;
  }
  // Installed: the chain still references the node, so it is
  // intentionally leaked. The object owns nothing from here on. The hook
  // keeps a reference to the callable.
  retain_callable(callable_);
  state_ = handle_state::kDetached;
  return *this;
}

} // namespace detail

const std::error_category &detour_category() noexcept {
  struct category final : std::error_category {
    const char *name() const noexcept override {
      return "spud.detour";
    }
    std::string message(int value) const override {
      return std::string(to_string(static_cast<detour_error>(value)));
    }
  };
  static const category instance;
  return instance;
}

size_t collect() {
  // A transaction's stop on this thread holds the patch lock, and freeing
  // inside it is not allowed anyway.
  if (detail::g_suspend_window_depth != 0)
    return 0;
  detail::patch_flag_guard lock;
  return detail::collect_retired(true);
}

namespace {
detour_plan plan_detour(uintptr_t address, Arch arch, size_t function_size) {
  detour_plan plan;
  if (function_size > 0x10000 || function_size > UINTPTR_MAX - address)
    return plan;
#if SPUD_ARCH_ARM64E
  plan.error = detour_error::kControlFlowEnforced;
  return plan;
#endif
  const detail::DetourImpl *backend = detail::hook_backend(arch);
  if (backend == nullptr) {
    plan.error = detour_error::kUnsupportedArch;
    return plan;
  }
  const detail::DetourImpl &impl = *backend;
  uintptr_t resolved = address;
  {
    // A hooked target already holds our landing, and another install only
    // links into its chain. Relocating the landing would describe nothing.
    detail::patch_flag_guard lock;
    detail::landing_info hooked;
    bool stacked = detail::decode_landing(address, arch, &hooked);
    if (!stacked) {
      resolved = impl.maybe_resolve_jump(address);
      stacked = resolved != address && function_size == 0 &&
                detail::decode_landing(resolved, arch, &hooked);
    }
    if (stacked) {
      plan.resolved_target = resolved;
      plan.hooked = true;
      plan.jump_size = hooked.size;
      plan.required_patch_bytes = hooked.anchor->required;
      plan.trampoline_size =
          hooked.anchor->block_size - detail::kAnchorPrefixSize;
      plan.error = hooked.anchor->live >= detail::kMaxChainLinks
                       ? detour_error::kChainFull
                       : detour_error::kOk;
      return plan;
    }
  }
  plan.resolved_target = resolved;
  if (function_size != 0 && resolved != address)
    return plan;
  address = resolved;
  // A larger patch only overwrites more, so if the short landing cannot be
  // placed the far one cannot either. The collector reads the bytes it
  // accepts, so an accepted extent is also readable.
  const uint32_t pad = detail::landing_for(arch, address, {});
  const size_t patch = detail::short_stub_size_for_arch(arch, pad);
  const auto [info, required] =
      impl.collect_relocations(address, patch, {}, function_size);
  plan.jump_size = patch;
  plan.required_patch_bytes = required;
  plan.relocated_instruction_count = info.relocations.size();
  if (required < patch) {
    plan.error = info.refusal;
    return plan;
  }
  plan.far_jump_size = detail::far_stub_size_for_arch(arch, pad);
  plan.far_required_patch_bytes = std::get<1>(
      impl.collect_relocations(address, plan.far_jump_size, {}, function_size));

  auto trampoline = impl.create_trampoline(
      0, address + required, {reinterpret_cast<uint8_t *>(address), required},
      info, true, unwind_support_enabled());
  if (trampoline.data.empty()) {
    plan.error = detour_error::kDecodeFailed;
    return plan;
  }
  plan.trampoline_size = trampoline.data.size();
  plan.error = detour_error::kOk;
  return plan;
}
} // namespace

detour_plan inspect_detour(uintptr_t address, Arch arch, size_t function_size) {
  detail::clear_os_error();
  const detour_plan plan = plan_detour(address, arch, function_size);
  // Sizing the far landing can refuse while the plan itself is fine.
  if (plan.error == detour_error::kOk)
    detail::clear_os_error();
  return plan;
}

detour_capabilities query_detour_capabilities(Arch arch) {
  detour_capabilities caps;
  if (detail::hook_backend(arch) == nullptr) {
    return caps;
  }
  caps.decode = true;
  caps.emit = true;
  caps.execute = arch == Arch::kHost;
  caps.protected_patch = caps.execute;

  return caps;
}

namespace detail {

namespace {
// One suspend window around a group of installs or uninstalls. Each hook's
// own session nests inside it.
struct patch_batch {
  patch_flag_guard lock;
  safe_patch_session session;

  patch_batch() {
    prewarm_memory_page_size();
    session.suspend();
  }
};

// What commit() keeps for one step: the resources prepared before the stop,
// a node an install can take inside it, whether applying the step changed
// anything, and what an uninstall unlinked for a rollback.
struct step_state {
  std::unique_ptr<prepared_install> prepared;
  hook_node *spare = nullptr;
  bool changed = false;
  staged_unlink unlinked{};

  step_state() = default;
  step_state(const step_state &) = delete;
  step_state &operator=(const step_state &) = delete;
  // Frees the nodes no hook ever used. Runs after the window closes;
  // unlinked hooks were drained or retired before that.
  ~step_state() {
    assert(unlinked.left.empty());
    delete_hook_node(unlinked.returned);
    delete_hook_node(spare);
  }
};
} // namespace

detour_error commit(std::span<hook_step> steps) {
  if (steps.empty()) {
    return detour_error::kOk;
  }
  if (g_suspend_window_depth != 0)
    return detour_error::kBusy;
  patch_flag_guard operation_lock;
  retired_sweep sweep;
  // Declared before the stop below, so it is destroyed after it ends.
  std::vector<step_state> state(steps.size());
  auto apply = [](hook_step &s, step_state &st) {
    if (s.handle == nullptr) {
      return s.install ? s.try_install(s.hook) : s.uninstall(s.hook);
    }
    detour &d = *s.handle;
    return d.record_error([&] {
      return s.install ? d.install_impl(Arch::kHost, false, &st.spare,
                                        st.prepared.get())
                       : d.unlink(&st.unlinked);
    });
  };
  // Newer steps are undone first, so the chain matches the moment after this
  // step and undoing an install unlinks the hook it added.
  auto revert = [](hook_step &s, step_state &st) {
    if (s.handle == nullptr) {
      return s.install ? s.uninstall(s.hook) : s.try_install(s.hook);
    }
    detour &d = *s.handle;
    return d.record_error([&] {
      return s.install ? d.unlink(&st.unlinked) : d.relink(st.unlinked);
    });
  };

  // Whether an earlier step removes a hook from `target`, which could be
  // its last.
  auto unhooked_before = [&](size_t i, uintptr_t target) {
    for (size_t j = 0; j < i; ++j) {
      const detour *d = steps[j].handle;
      if (!steps[j].install && d != nullptr && d->is_installed() &&
          d->node_->anchor->target == target)
        return true;
    }
    return false;
  };
  // Whether a removal can be the last on its target: the batch removes as
  // many of its hooks as are live.
  auto removes_last = [&](const detour &d) {
    const hook_anchor *anchor = d.node_->anchor;
    const auto removing = static_cast<size_t>(
        std::count_if(steps.begin(), steps.end(), [&](const hook_step &s) {
          return !s.install && s.handle != nullptr &&
                 s.handle->is_installed() && s.handle->node_->anchor == anchor;
        }));
    return removing >= anchor->live;
  };
  bool writes = false;
  size_t changes = 0;
  for (size_t i = 0; i < steps.size(); ++i) {
    hook_step &s = steps[i];
    if (s.handle == nullptr)
      continue;
    detour &d = *s.handle;
    if (!s.install) {
      // Removals and their rollbacks only move pointers and the landing an
      // anchor already holds, so they prepare no memory.
      if (d.is_installed()) {
        ++changes;
        writes |= removes_last(d);
      }
      continue;
    }
    // An installed handle prepares too: an earlier step can remove it first.
    changes += d.is_installed() ? 0 : 1;
    if (d.address_ == 0 || d.is_detached())
      continue;
    // The stop cannot allocate the node a handle gets on install.
    state[i].spare = new_hook_node(true);
    const Arch arch = Arch::kHost;
    const auto *impl = hook_backend(arch);
    if (impl == nullptr)
      continue;
    landing_info hooked;
    uintptr_t address = d.address_;
    bool stacked = decode_landing(address, arch, &hooked);
    if (!stacked) {
      address = impl->maybe_resolve_jump(address);
      stacked = address != d.address_ && decode_landing(address, arch, &hooked);
    }
    std::span<const uint8_t> prefix;
    if (stacked) {
      prefix = hooked.anchor->saved_bytes;
    }
    auto resources = std::make_unique<prepared_install>();
    // A first hook on the target writes it, and is built here because the
    // stop cannot allocate. A stacked one only links a node, unless an
    // earlier step may take the target's last hook off first.
    if (!stacked || unhooked_before(i, address)) {
      writes |= !d.is_installed() || unhooked_before(i, address);
      if (d.function_size_ != 0 && address != d.address_) {
        resources->fresh.emplace();
        resources->fresh->error = detour_error::kDecodeFailed;
      } else {
        resources->fresh =
            prepare_fresh_hook(address, arch, *impl, prefix, d.function_size_);
      }
    }
    resources->block_size = node_block_size(arch);
    resources->block = allocate_node(arch, address);
    state[i].prepared = std::move(resources);
  }
  detour_error err = detour_error::kOk;
  auto apply_steps = [&] {
    size_t applied = 0;
    for (; applied < steps.size(); ++applied) {
      hook_step &s = steps[applied];
      step_state &st = state[applied];
      const bool was_installed = s.is_installed(s.hook);
      err = apply(s, st);
      if (err != detour_error::kOk) {
        break;
      }
      st.changed = s.is_installed(s.hook) != was_installed;
    }
    if (err != detour_error::kOk) {
      for (size_t i = applied; i-- > 0;) {
        if (state[i].changed &&
            revert(steps[i], state[i]) != detour_error::kOk) {
          err = detour_error::kRollbackFailed;
        }
      }
    }
  };
  // Arbitrary hook methods can allocate or take locks. Only native
  // detours participate in the shared stop; mixed batches retain rollback
  // and serialize patches, but each native step owns its own stop.
  const bool native_only =
      std::all_of(steps.begin(), steps.end(),
                  [](const hook_step &s) { return s.handle != nullptr; });
  // What the steps left unlinked, and the anchors only they name. Room is
  // made up front so the stop below can fill it.
  std::vector<unlinked_hook> leftovers;
  leftovers.reserve(2 * steps.size());
  auto gather = [&] {
    for (size_t i = 0; i < steps.size(); ++i) {
      unlinked_hook &left = state[i].unlinked.left;
      if (left.empty())
        continue;
      if (left.node != nullptr && steps[i].handle != nullptr) {
        left.wrapper = steps[i].handle->wrapper_;
        left.callable = steps[i].handle->callable_;
        retain_callable(left.callable);
      }
      leftovers.push_back(std::exchange(left, {}));
    }
    add_orphaned_anchors(leftovers);
  };
  // A batch that writes no target only stores pointers, which needs no stop
  // when there is a single change; several still take effect at once.
  if (native_only && (writes || changes > 1)) {
    patch_batch batch;
    apply_steps();
    // Usually nothing is inside the removed hooks any more; finding that
    // out in this stop spares the drain one of its own.
    gather();
    mark_drained(batch.session, leftovers);
    sweep.scan(batch.session);
  } else {
    apply_steps();
    gather();
  }

  // Threads run again. What is still reachable drains here.
  release_unlinked(leftovers, true, sweep);
  return err;
}

} // namespace detail

} // namespace spud

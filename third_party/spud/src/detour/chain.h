#pragma once

#include "detour/detour_impl.h"
#include "detour/detour_stub.h"
#include <spud/arch.h>
#include <spud/detour.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace spud::detail {

struct hook_anchor;

// One hook: what its wrapper reads, how it links into its target's chain,
// and, for a removable hook, its entry counts, which follow the node in the
// same allocation; a permanent hook's allocation ends before them. The
// entry stub embeds the counts' address, and the context is the last cache
// line before it.
struct alignas(64) hook_node {
  // Pool block holding the entry stub, original() and the next slot.
  void *block = nullptr;
  size_t block_size = 0;
  hook_anchor *anchor = nullptr;
  // Neighbours while linked, newer and older. drain_next chains removed
  // hooks that are not freed yet. All guarded by the patch lock.
  hook_node *above = nullptr;
  hook_node *below = nullptr;
  hook_node *drain_next = nullptr;
  // Trampoline leases. A removed hook is not freed while one is held.
  std::atomic<uint64_t> pins{0};
  bool counted = false;
  detour_context context;

  // Where the stub counts entries; past the end of an uncounted node.
  entry_counts *counts() {
    return reinterpret_cast<entry_counts *>(this + 1);
  }
  const entry_counts *counts() const {
    return reinterpret_cast<const entry_counts *>(this + 1);
  }
  // Whether a call is inside the hook; see entry_counts::active.
  bool active() const {
    return counted && counts()->active();
  }
};
static_assert(offsetof(hook_node, context) + sizeof(detour_context) ==
                  sizeof(hook_node),
              "the context must be the cache line right before the counts");

// Removable hooks get entry counts; permanent ones do not need them. Debug
// builds assert neither happens inside the suspend window.
hook_node *new_hook_node(bool counted);
void delete_hook_node(hook_node *node);

// Owns the hook chain on one target. The target's landing jumps here once,
// for as long as any hook is installed, and the anchor's dispatch jumps on
// through its head slot to the newest hook. Each hook's original() jumps
// through its own next slot to the hook below it, and the bottom one to the
// relocated prologue. Adding or removing a hook other than the first or
// last is a pointer store; only those two write the target.
//
// Lives as long as a hook links to it or a removed hook that still has
// calls in flight can jump to its relocated prologue.
struct hook_anchor {
  // "SPUDANC1" as little-endian bytes; checked before a decoded landing is
  // trusted, so foreign code that jumps into similar bytes is not ours.
  static constexpr uint64_t kMagic = 0x31434E4144555053ULL;
  uint64_t magic = kMagic;
  uintptr_t target = 0;
  Arch arch = Arch::kHost;
  // Dispatch, head slot, owner pointer, then the relocated prologue.
  void *block = nullptr;
  size_t block_size = 0;
  uintptr_t original = 0;
  // The landing and the original bytes it covers.
  stub_bytes landing;
  std::vector<uint8_t> saved_bytes;
  // How far the relocated prologue reaches into the target, and where an
  // instruction pointer inside that span continues in the copy.
  size_t required = 0;
  std::vector<std::pair<uintptr_t, uintptr_t>> relocation_offsets;
  // The copy's unwind registration, which snapshots the source metadata
  // the copy was made from. Rechecked inside each stop that writes the
  // landing.
  const unwind_registration *metadata = nullptr;

  // Live hooks, newest first through hook_node::below.
  hook_node *top = nullptr;
  size_t live = 0;
  // Removed hooks whose memory is not freed yet, through drain_next. A call
  // still inside one can follow its next slot, so removing the hook that
  // slot names repoints it as well.
  hook_node *draining = nullptr;
  // Hooks naming this anchor that are not freed yet.
  size_t refs = 0;
  // The target holds the landing.
  bool hooked = false;

  uintptr_t head_slot() const {
    return reinterpret_cast<uintptr_t>(block) + kAnchorHeadOffset;
  }
};

// Live hooks per target. Nothing in the call path walks the chain; the
// bound keeps a runaway install loop from growing one target without end.
inline constexpr size_t kMaxChainLinks = 256;

// A target that holds a landing to a validated anchor.
struct landing_info {
  hook_anchor *anchor = nullptr;
  // Bytes the landing occupies at the target.
  size_t size = 0;
};

// Decodes the landing at `target` and validates the anchor block it reaches.
// Reads through the OS and allocates nothing, so it is safe inside the
// suspend window.
bool decode_landing(uintptr_t target, Arch arch, landing_info *out);

// Whether `body` is the start of a live anchor block.
hook_anchor *anchor_at(uintptr_t body, Arch arch);

// The code a hook's entry stub starts at, and its next slot.
inline uintptr_t node_entry(const hook_node &node) {
  return reinterpret_cast<uintptr_t>(node.block);
}
inline uintptr_t node_next_slot(const hook_node &node, Arch arch) {
  return node_entry(node) + node_next_offset(arch);
}

// Slots are read by the indirect jumps without any lock. Stores publish
// with release; the code they point at was written and flushed first.
inline uintptr_t load_slot(uintptr_t slot) {
  return std::atomic_ref<uintptr_t>(*reinterpret_cast<uintptr_t *>(slot))
      .load(std::memory_order_acquire);
}
void store_slot(uintptr_t slot, uintptr_t value);

// Pointer edits on the chain. Patch lock held. Window-safe: no allocation.
// link_top publishes `node` as the newest hook; its next slot must already
// name the hook below it.
void link_top(hook_anchor &anchor, hook_node *node);
// Points every slot that names `node` at what follows it and moves `node` to
// the draining list. Calls that already loaded its address still arrive.
void unlink_node(hook_anchor &anchor, hook_node *node);
// Puts back a hook unlink_node removed, between the same neighbours. Undo
// runs newest first, so they are adjacent again.
void relink_node(hook_anchor &anchor, hook_node *node, hook_node *above,
                 hook_node *below);
// Drops a freed hook from the draining list. Returns true when that was the
// anchor's last reference and the target no longer holds its landing.
bool release_node(hook_anchor &anchor, hook_node *node);

} // namespace spud::detail

#include "detour/chain.h"

#include "detour/detour_stub.h"
#include "memory/readable.h"
#include <spud/arch.h>
#include <spud/utils.h>

#include <array>
#include <cstddef>
#include <cstring>

#if SPUD_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif SPUD_OS_LINUX || SPUD_OS_APPLE
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace spud::detail {

namespace {

// Whether every byte of [ptr, ptr + size) is mapped. Decoding a landing
// follows a pointer read from code, which may be foreign.
bool range_is_mapped(const void *ptr, size_t size) {
  if (ptr == nullptr) {
    return false;
  }
#if SPUD_OS_WIN
  MEMORY_BASIC_INFORMATION info;
  if (VirtualQuery(ptr, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT)
    return false;
  const auto end = reinterpret_cast<uintptr_t>(ptr) + size;
  return end <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
#elif SPUD_OS_LINUX || SPUD_OS_APPLE
  static const long page = sysconf(_SC_PAGESIZE);
  if (page <= 0) {
    return false;
  }
  const auto mask = ~static_cast<uintptr_t>(page - 1);
  const auto first = reinterpret_cast<uintptr_t>(ptr) & mask;
  const auto last = (reinterpret_cast<uintptr_t>(ptr) + size - 1) & mask;
  // One call covers both ends; the anchor rarely straddles a page.
#if SPUD_OS_APPLE
  char vec[2] = {};
#else
  unsigned char vec[2] = {};
#endif
  return mincore(reinterpret_cast<void *>(first),
                 static_cast<size_t>(last - first) + 1, vec) == 0;
#else
  return true;
#endif
}

bool is_valid_anchor(const hook_anchor *anchor) {
  return range_is_mapped(anchor, sizeof(hook_anchor)) &&
         anchor->magic == hook_anchor::kMagic;
}

} // namespace

hook_anchor *anchor_at(uintptr_t body, Arch arch) {
  std::array<uint8_t, kAnchorPrefixSize> prefix{};
  if (body == 0 || !read_code_bytes(body, prefix)) {
    return nullptr;
  }
  uintptr_t slot = 0;
  if (!try_decode_indirect_jump_for_arch(arch, prefix.data(), prefix.size(),
                                         body, &slot) ||
      slot != body + kAnchorHeadOffset) {
    return nullptr;
  }
  hook_anchor *anchor = nullptr;
  std::memcpy(&anchor, prefix.data() + kAnchorOwnerOffset, sizeof(anchor));
  // The owner names the block back, so an anchor-shaped prefix copied
  // somewhere else, or one left in a freed block, is not taken for ours.
  if (!is_valid_anchor(anchor) ||
      anchor->block != reinterpret_cast<void *>(body) || anchor->arch != arch) {
    return nullptr;
  }
  return anchor;
}

bool decode_landing(uintptr_t target, Arch arch, landing_info *out) {
  // The longest landing is the ARM64 far pad behind a BTI word.
  std::array<uint8_t, 24> bytes{};
  const size_t available = read_code_prefix(target, bytes);
  uintptr_t body = 0;
  size_t size = 0;
  switch (arch) {
  case Arch::kX86_64:
    if (!x64::try_decode_short_jump(bytes.data(), available, target, &body,
                                    &size) &&
        !x64::try_decode_far_jump(bytes.data(), available, &body, &size)) {
      return false;
    }
    break;
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64: {
    uint32_t landing = 0;
    if (!arm64::try_decode_short_jump(bytes.data(), available, target, &body,
                                      &size, &landing) &&
        !arm64::try_decode_far_jump(bytes.data(), available, &body, &size,
                                    &landing)) {
      return false;
    }
    break;
  }
#endif
  default:
    return false;
  }
  hook_anchor *anchor = anchor_at(body, arch);
  // A landing whose anchor serves another target is a forwarder to it.
  if (anchor == nullptr || anchor->target != target) {
    return false;
  }
  if (out != nullptr) {
    *out = {anchor, size};
  }
  return true;
}

void store_slot(uintptr_t slot, uintptr_t value) {
  // Pool pages are MAP_JIT on Apple; a store needs the thread's write window
  // like any code write.
  SPUD_SCOPED_JIT_WRITE();
  std::atomic_ref<uintptr_t>(*reinterpret_cast<uintptr_t *>(slot))
      .store(value, std::memory_order_release);
}

void link_top(hook_anchor &anchor, hook_node *node) {
  node->above = nullptr;
  node->below = anchor.top;
  if (anchor.top != nullptr) {
    anchor.top->above = node;
  }
  anchor.top = node;
  ++anchor.live;
  store_slot(anchor.head_slot(), node_entry(*node));
}

void unlink_node(hook_anchor &anchor, hook_node *node) {
  const uintptr_t entry = node_entry(*node);
  const uintptr_t next =
      node->below != nullptr ? node_entry(*node->below) : anchor.original;
  store_slot(node->above != nullptr ? node_next_slot(*node->above, anchor.arch)
                                    : anchor.head_slot(),
             next);
  for (hook_node *d = anchor.draining; d != nullptr; d = d->drain_next) {
    const uintptr_t slot = node_next_slot(*d, anchor.arch);
    if (load_slot(slot) == entry) {
      store_slot(slot, next);
    }
  }
  if (node->above != nullptr) {
    node->above->below = node->below;
  } else {
    anchor.top = node->below;
  }
  if (node->below != nullptr) {
    node->below->above = node->above;
  }
  node->above = nullptr;
  node->below = nullptr;
  --anchor.live;
  node->drain_next = anchor.draining;
  anchor.draining = node;
}

namespace {
void drop_draining(hook_anchor &anchor, hook_node *node) {
  for (hook_node **link = &anchor.draining; *link != nullptr;
       link = &(*link)->drain_next) {
    if (*link == node) {
      *link = node->drain_next;
      node->drain_next = nullptr;
      return;
    }
  }
}
} // namespace

void relink_node(hook_anchor &anchor, hook_node *node, hook_node *above,
                 hook_node *below) {
  drop_draining(anchor, node);
  // A later step may have repointed this slot while the hook was draining.
  store_slot(node_next_slot(*node, anchor.arch),
             below != nullptr ? node_entry(*below) : anchor.original);
  node->above = above;
  node->below = below;
  if (below != nullptr) {
    below->above = node;
  }
  if (above != nullptr) {
    above->below = node;
  } else {
    anchor.top = node;
  }
  ++anchor.live;
  store_slot(above != nullptr ? node_next_slot(*above, anchor.arch)
                              : anchor.head_slot(),
             node_entry(*node));
}

bool release_node(hook_anchor &anchor, hook_node *node) {
  drop_draining(anchor, node);
  return --anchor.refs == 0 && !anchor.hooked;
}

} // namespace spud::detail

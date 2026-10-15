#pragma once

#if SPUD_AARCH64_SUPPORT
#include "aarch64/aarch64.h"
#endif
#include "detour/control_flow.h"
#include "memory/executable_pool.h"
#include "memory/readable.h"
#include "memory/unwind.h"
#include "x86_64/x86_64.h"

#include <spud/diagnostics.h>
#include <spud/error.h>
#include <spud/utils.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace spud {
namespace detail {

// Test-only: installs skip the short landing and write the far one, as if
// no pool block were within branch reach of the target.
extern std::atomic<bool> g_force_far_landing_for_tests;

// A trampoline owns its auxiliary code until the pool adopts it. Retirement
// releases the group together, after callers and leases have drained.
struct executable_relay {
  void *memory;
  size_t size;
  const unwind_registration *metadata = nullptr;
  // What publish_relays() still has to write to `memory`, empty once it is
  // written, and the source offset of the instruction the relay stands for,
  // which its unwind record describes.
  std::vector<uint8_t> code;
  size_t source_offset = 0;
  executable_relay(void *allocation, size_t length)
      : memory(allocation), size(length) {}
  executable_relay(const executable_relay &) = delete;
  executable_relay(executable_relay &&other) noexcept
      : memory(std::exchange(other.memory, nullptr)), size(other.size),
        metadata(other.metadata), code(std::move(other.code)),
        source_offset(other.source_offset) {}
  ~executable_relay();
  std::pair<void *, size_t> release() {
    return {std::exchange(memory, nullptr), size};
  }
};

struct executable_relays {
  std::vector<executable_relay> entries;
};

struct relocation_info;

struct trampoline_buffer {
  uintptr_t start;
  std::vector<uint8_t> data;
  // (original_offset -> cumulative relocated delta) table, consumed by
  // relocated_location_at() to map instruction pointers from overwritten
  // bytes to their relocated counterparts during safe patching.
  std::vector<std::pair<uintptr_t, uintptr_t>> relocation_offsets;
  std::vector<unwind_range> unwind_ranges;
  std::vector<stack_adjustment> stack_adjustments;
  std::unique_ptr<executable_relays> relays;

  trampoline_buffer(uintptr_t entry, std::vector<uint8_t> bytes,
                    std::vector<std::pair<uintptr_t, uintptr_t>> offsets,
                    std::vector<unwind_range> ranges = {},
                    std::vector<stack_adjustment> stack = {},
                    std::unique_ptr<executable_relays> auxiliary = {})
      : start(entry), data(std::move(bytes)),
        relocation_offsets(std::move(offsets)),
        unwind_ranges(std::move(ranges)), stack_adjustments(std::move(stack)),
        relays(std::move(auxiliary)) {}

  // Writes, describes and registers the relays a trampoline built for `at`
  // allocated and that are not written yet. `source` and `copied` are the
  // relocated code. Runs before the trampoline itself is described, since
  // its unwind record depends on theirs. On failure `*error` says which
  // step refused, as install reports it.
  bool publish_relays(uintptr_t at, uintptr_t source, size_t copied,
                      const relocation_info &relocations,
                      bool unwind = bool(SPUD_UNWIND_SUPPORT),
                      detour_error *error = nullptr);
};

// Preserved original bytes keep their address modulo this, so aligned
// scalar and vector self-reads stay aligned. A build for an unknown address
// reserves the largest padding it can need.
inline constexpr size_t kPreservedAlignment = 64;

using relocation_entry = std::variant<x64::relocation_entry
#if SPUD_AARCH64_SUPPORT
                                      ,
                                      arm64::relocation_entry
#endif
                                      >;

struct relocation_info {
  std::vector<relocation_entry> relocations;
  // A back edge can extend the copy beyond the prefix that is patched.
  size_t overwritten_size = 0;
  // Decode and emission use one immutable view. Commit validates this entire
  // range, including extended copies and preserved data, before patching.
  uintptr_t source_address = 0;
  std::vector<uint8_t> source_bytes;
  std::vector<size_t> instruction_offsets;
  size_t function_end = 0;
  // What install reports when the collector copies nothing. Only length is
  // kInsufficientBytes: the function ends, or stops being readable, before
  // the patch does. Code that cannot be decoded or proven safe to move is
  // kDecodeFailed. Either way the reason goes to last_failure_detail().
  detour_error refusal = detour_error::kInsufficientBytes;

  // Records a refusal at `at`, an instruction or the end of the function.
  void refuse(const char *reason, uintptr_t at,
              detour_error error = detour_error::kDecodeFailed) {
    refusal = error;
    note_failure(reason, at);
  }

  struct scan_bounds {
    // Snapshot size, or 0 when it is unreadable or shorter than the patch.
    size_t window = 0;
    // Where the scan stops: the end of the unwind entry, or `window`.
    size_t limit = 0;
    bool bounded = false;
  };

  // Snapshots the code at `address` for a scan. An explicit `function_size`
  // is read whole. Otherwise an unwind entry that starts at `address`
  // bounds the scan. Past an unwind entry, and past an explicit extent
  // shorter than the patch, the snapshot covers a patch's worth and a little
  // more for the padding a short function needs. Without either the scan
  // may read 16KB. Sizes are rounded down to
  // `granule`. The first bytes come from `original_prefix` when a hook
  // already covers them.
  scan_bounds read_source(uintptr_t address, size_t jump_size,
                          std::span<const uint8_t> original_prefix,
                          size_t function_size, size_t granule) {
    // 16KB covers long inferred scans; explicit extents may be up to 64KB.
    constexpr size_t kScanWindow = 0x4000;
    constexpr size_t kMaxFunctionSize = 0x10000;
    // Past the patch, so the last padding instruction (a long x86 NOP)
    // decodes whole.
    constexpr size_t kPaddingSlack = 64;
    // An explicit extent shorter than the patch is read like a short entry.
    const bool short_extent = function_size != 0 && function_size < jump_size;
    if (function_size > kMaxFunctionSize ||
        function_size > UINTPTR_MAX - address || function_size % granule != 0) {
      refuse("invalid_function_size", address);
      return {};
    }
    size_t want = short_extent ? function_size + jump_size + kPaddingSlack
                               : function_size;
    size_t entry = 0;
    if (function_size == 0) {
      want = kScanWindow;
      if (const auto extent = function_extent(address);
          extent && extent->second - address <= kScanWindow &&
          (extent->second - address) % granule == 0) {
        entry = extent->second - address;
        want = std::min(entry + jump_size + kPaddingSlack, kScanWindow);
      }
    }
    auto fits = [&](size_t window) {
      return window >= jump_size &&
             (function_size == 0 || short_extent || window == function_size);
    };
    // Fewer readable bytes than the patch, or than an explicit extent, is
    // a length refusal at the first byte that could not be read.
    auto unreadable = [&](size_t readable) {
      refuse("source_unreadable", address + readable,
             detour_error::kInsufficientBytes);
      return scan_bounds{};
    };
    size_t window = readable_bytes_from(address, want) & ~(granule - 1);
    if (!fits(window))
      return unreadable(window);
    source_address = address;
    source_bytes.resize(window);
    window = read_code_prefix(address, source_bytes) & ~(granule - 1);
    if (!fits(window) || original_prefix.size() > window)
      return unreadable(window);
    source_bytes.resize(window);
    std::copy(original_prefix.begin(), original_prefix.end(),
              source_bytes.begin());
    if (function_size != 0)
      return {window, function_size, false};
    // An entry that is not readable to its end leaves the scan unbounded.
    if (entry != 0 && entry <= window)
      return {window, entry, true};
    return {window, window, false};
  }

  // Extends the snapshot `count` bytes before its start, for preserved
  // reads that begin before the function.
  bool read_before(size_t count) {
    std::vector<uint8_t> before(count);
    if (!read_code_bytes(source_address - count, before))
      return false;
    source_bytes.insert(source_bytes.begin(), before.begin(), before.end());
    source_address -= count;
    return true;
  }

  // The snapshot of [address, address + size), or empty when the snapshot
  // does not cover all of it.
  std::span<const uint8_t> snapshot(uintptr_t address, size_t size) const {
    if (address < source_address ||
        address - source_address > source_bytes.size() ||
        size > source_bytes.size() - (address - source_address))
      return {};
    return {source_bytes.data() + (address - source_address), size};
  }
};

inline bool
trampoline_buffer::publish_relays(uintptr_t at, uintptr_t source, size_t copied,
                                  const relocation_info &relocations,
                                  bool unwind, detour_error *error) {
  auto fail = [&](detour_error reason) {
    if (error != nullptr)
      *error = reason;
    return false;
  };
  if (relays == nullptr)
    return true;
  for (auto &relay : relays->entries) {
    if (relay.code.empty())
      continue;
    const auto entry = reinterpret_cast<uintptr_t>(relay.memory);
    std::vector<unwind_range> frame;
    if (SPUD_UNWIND_SUPPORT && unwind)
      frame.push_back(
          {0, relay.size, relay.source_offset, unwind_range::state::kFixed});
    trampoline_buffer code(0, relay.code, {}, std::move(frame));
    // Fixed-state CFI ignores this table. LSDA landing pads inside the copy
    // must still point into the main trampoline, not the relay.
    if (SPUD_UNWIND_SUPPORT && unwind) {
      code.relocation_offsets = relocation_offsets;
      for (auto &[offset, delta] : code.relocation_offsets)
        delta += at - entry;
    }
    if (!executable_pool::instance().describe_trampoline(
            relay.memory, relay.size, entry, source, copied, code, unwind,
            at != 0 ? std::span(relocations.instruction_offsets)
                    : std::span<const size_t>{},
            &relay.metadata, relocations.overwritten_size,
            {relocations.source_address, relocations.source_bytes}))
      return fail(detour_error::kUnwindFailed);
    {
      SPUD_SCOPED_JIT_WRITE();
      std::memcpy(relay.memory, relay.code.data(), relay.size);
    }
    if (!flush_cache(relay.memory, relay.size))
      return fail(detour_error::kProtectionFailed);
    if (!register_call_target(relay.memory, relay.size))
      return fail(detour_error::kControlFlowEnforced);
    relay.code.clear();
  }
  return true;
}

} // namespace detail
} // namespace spud

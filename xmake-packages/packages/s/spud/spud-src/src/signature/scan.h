#pragma once

#include <spud/arch.h>
#include <spud/signature.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The x86 kernels take their ISA from a function attribute, not a per-file
// -mavx2. With the flag, every inline function the file instantiates (vector
// growth, span and string_view accessors) is also emitted as AVX2 code, and
// the linker may keep that copy for callers on a CPU without AVX2. MSVC
// accepts the intrinsics without either.
#if defined(__GNUC__) || defined(__clang__)
#define SPUD_TARGET(isa) __attribute__((target(isa)))
#else
#define SPUD_TARGET(isa)
#endif

// scan_anchors below must carry the same attribute as the ISA it is
// instantiated for, and a template cannot take an attribute as a parameter.
// A kernel file whose ISA is above the baseline defines SPUD_SCAN_ISA before
// including this header. Each instantiation lives in exactly one file, with
// traits from an anonymous namespace, so no copy is shared across ISAs.
#ifdef SPUD_SCAN_ISA
#define SPUD_SCAN_KERNEL SPUD_TARGET(SPUD_SCAN_ISA)
#else
#define SPUD_SCAN_KERNEL
#endif

namespace spud::detail {

// One pattern prepared for one buffer. The kernels compare two anchor bytes
// per position and verify the whole pattern only where both match. `first`
// is the rarest fixed byte of the pattern and `second` another one, or the
// same one when the pattern has a single fixed byte.
struct pattern_scan {
  const uint8_t *buffer = nullptr;
  signature_view pattern;
  // Last start offset with the whole pattern inside the buffer.
  size_t last_start = 0;
  size_t first = 0;
  size_t second = 0;
  uint8_t first_byte = 0;
  uint8_t second_byte = 0;
};

// Receives each confirmed match in order. Returning false stops the scan.
using match_fn = bool (*)(void *ctx, size_t start);

inline constexpr size_t kScanStopped = static_cast<size_t>(-1);

// Each kernel reports every match starting in the whole kScanStep steps it
// covers and returns the first start offset it did not examine, or
// kScanStopped.
#if SPUD_ARCH_X86_FAMILY
size_t scan_anchors_sse2(const pattern_scan &scan, match_fn on_match,
                         void *ctx);
size_t scan_anchors_avx2(const pattern_scan &scan, match_fn on_match,
                         void *ctx);
#elif SPUD_ARCH_ARM_FAMILY
size_t scan_anchors_neon(const pattern_scan &scan, match_fn on_match,
                         void *ctx);
#endif

inline uint64_t load_u64(const uint8_t *p) {
  uint64_t value;
  std::memcpy(&value, p, sizeof(value));
  return value;
}

// Full compare at one start offset, 8 bytes at a time. The last word
// overlaps the one before it rather than reading past the pattern, so this
// never touches a byte beyond start + pattern.size.
inline bool matches_at(const pattern_scan &scan, size_t start) {
  const uint8_t *at = scan.buffer + start;
  const uint8_t *bytes = scan.pattern.bytes;
  const uint8_t *mask = scan.pattern.mask;
  const size_t n = scan.pattern.size;
  if (n < 8) {
    for (size_t i = 0; i < n; ++i) {
      if (((at[i] ^ bytes[i]) & mask[i]) != 0) {
        return false;
      }
    }
    return true;
  }
  for (size_t i = 0; i + 8 <= n; i += 8) {
    if (((load_u64(at + i) ^ load_u64(bytes + i)) & load_u64(mask + i)) != 0) {
      return false;
    }
  }
  const size_t i = n - 8;
  return ((load_u64(at + i) ^ load_u64(bytes + i)) & load_u64(mask + i)) == 0;
}

#if defined(_MSC_VER) && !defined(__clang__)
#define SPUD_SCAN_NOINLINE __declspec(noinline)
#else
#define SPUD_SCAN_NOINLINE __attribute__((noinline))
#endif

// Starts examined per iteration of the kernel loop.
inline constexpr size_t kScanStep = 64;

// Reports the confirmed matches among `hits`, one bit per start from
// `offset`. Out of line so that the scan loop holds no call: with the
// indirect on_match call inlined into it, GCC kept the splatted anchors and
// buffer pointers on the stack and reloaded them every block, a third of the
// throughput.
SPUD_SCAN_NOINLINE inline bool report_hits(const pattern_scan &scan,
                                           size_t offset, uint64_t hits,
                                           match_fn on_match, void *ctx) {
  for (; hits != 0; hits &= hits - 1) {
    const size_t start = offset + static_cast<size_t>(std::countr_zero(hits));
    if (matches_at(scan, start) && !on_match(ctx, start)) {
      return false;
    }
  }
  return true;
}

// Isa supplies the vector type and these operations:
//   kWidth            bytes per vector; kScanStep is a multiple
//   splat(b)          b in every lane
//   load(p)           kWidth unaligned bytes at p
//   match(a, x, b, y) all-ones in the lanes where a == x and b == y
//   either(v, w)      v | w
//   any(v)            whether any lane of a match() result is set
//   step_bits(m)      the kScanStep / kWidth match() results as one bit per
//                     lane, the first vector in the low bits
//
// The loop stops where a step's last start would pass last_start. Every
// start a step covers then has the whole pattern, both anchors included,
// inside the buffer, so neither the loads nor matches_at need a bounds
// check. In code most steps hold no candidate, so a step is loads, compares
// and one branch.
template <class Isa>
SPUD_SCAN_KERNEL size_t scan_anchors(const pattern_scan &scan,
                                     match_fn on_match, void *ctx) {
  constexpr size_t kWidth = Isa::kWidth;
  constexpr size_t kVectors = kScanStep / kWidth;
  static_assert(kVectors * kWidth == kScanStep);
  const uint8_t *first_at = scan.buffer + scan.first;
  const uint8_t *second_at = scan.buffer + scan.second;
  const size_t end = scan.last_start + 1;
  size_t offset = 0;
  while (offset + kScanStep <= end) {
    // Splatted here, not once above, so that nothing vector-typed is live
    // across the report_hits call and the inner loop keeps it in registers.
    const auto first = Isa::splat(scan.first_byte);
    const auto second = Isa::splat(scan.second_byte);
    uint64_t hits = 0;
    for (; offset + kScanStep <= end; offset += kScanStep) {
      typename Isa::vec found[kVectors];
      for (size_t i = 0; i < kVectors; ++i) {
        found[i] =
            Isa::match(first, Isa::load(first_at + offset + i * kWidth), second,
                       Isa::load(second_at + offset + i * kWidth));
      }
      auto any = found[0];
      for (size_t i = 1; i < kVectors; ++i) {
        any = Isa::either(any, found[i]);
      }
      if (Isa::any(any)) {
        hits = Isa::step_bits(found);
        break;
      }
    }
    if (hits == 0) {
      break;
    }
    if (!report_hits(scan, offset, hits, on_match, ctx)) {
      return kScanStopped;
    }
    offset += kScanStep;
  }
  // The last starts, fewer than kScanStep, are left to the scalar tail.
  return offset;
}

} // namespace spud::detail

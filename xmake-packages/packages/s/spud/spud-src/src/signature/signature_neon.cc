#include "scan.h"

#include <arm_neon.h>

namespace spud::detail {

namespace {

// NEON is part of AArch64, so this kernel needs no target attribute.
struct neon {
  using vec = uint8x16_t;
  static constexpr size_t kWidth = 16;

  static vec splat(uint8_t b) {
    return vdupq_n_u8(b);
  }
  static vec load(const uint8_t *p) {
    return vld1q_u8(p);
  }
  static vec match(vec a, vec x, vec b, vec y) {
    return vandq_u8(vceqq_u8(a, x), vceqq_u8(b, y));
  }
  static vec either(vec v, vec w) {
    return vorrq_u8(v, w);
  }
  static bool any(vec v) {
    return vmaxvq_u8(v) != 0;
  }
  // NEON has no movemask. Keep one weight bit per lane, 1 to 0x80 across
  // each 8 lanes, and three rounds of pairwise adds fold the four vectors
  // into 64 bits in lane order.
  static uint64_t step_bits(const vec *m) {
    const uint8x16_t weights = {1, 2, 4, 8, 16, 32, 64, 128,
                                1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t low =
        vpaddq_u8(vandq_u8(m[0], weights), vandq_u8(m[1], weights));
    const uint8x16_t high =
        vpaddq_u8(vandq_u8(m[2], weights), vandq_u8(m[3], weights));
    uint8x16_t sum = vpaddq_u8(low, high);
    sum = vpaddq_u8(sum, sum);
    return vgetq_lane_u64(vreinterpretq_u64_u8(sum), 0);
  }
};

} // namespace

size_t scan_anchors_neon(const pattern_scan &scan, match_fn on_match,
                         void *ctx) {
  return scan_anchors<neon>(scan, on_match, ctx);
}

} // namespace spud::detail

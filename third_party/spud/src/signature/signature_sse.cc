#include "scan.h"

#include <emmintrin.h>

namespace spud::detail {

namespace {

// SSE2 is part of x86-64, so this kernel needs no target attribute.
struct sse2 {
  using vec = __m128i;
  static constexpr size_t kWidth = 16;

  static vec splat(uint8_t b) {
    return _mm_set1_epi8(static_cast<char>(b));
  }
  static vec load(const uint8_t *p) {
    return _mm_loadu_si128(reinterpret_cast<const __m128i *>(p));
  }
  static vec match(vec a, vec x, vec b, vec y) {
    return _mm_and_si128(_mm_cmpeq_epi8(a, x), _mm_cmpeq_epi8(b, y));
  }
  static vec either(vec v, vec w) {
    return _mm_or_si128(v, w);
  }
  static bool any(vec v) {
    return _mm_movemask_epi8(v) != 0;
  }
  static uint64_t step_bits(const vec *m) {
    uint64_t bits = 0;
    for (int i = 0; i < 4; ++i) {
      bits |=
          static_cast<uint64_t>(static_cast<uint32_t>(_mm_movemask_epi8(m[i])))
          << (16 * i);
    }
    return bits;
  }
};

} // namespace

size_t scan_anchors_sse2(const pattern_scan &scan, match_fn on_match,
                         void *ctx) {
  return scan_anchors<sse2>(scan, on_match, ctx);
}

} // namespace spud::detail

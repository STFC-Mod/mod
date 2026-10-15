#define SPUD_SCAN_ISA "avx2"
#include "scan.h"

#include <immintrin.h>

namespace spud::detail {

namespace {

struct avx2 {
  using vec = __m256i;
  static constexpr size_t kWidth = 32;

  SPUD_TARGET("avx2") static vec splat(uint8_t b) {
    return _mm256_set1_epi8(static_cast<char>(b));
  }
  SPUD_TARGET("avx2") static vec load(const uint8_t *p) {
    return _mm256_loadu_si256(reinterpret_cast<const __m256i *>(p));
  }
  SPUD_TARGET("avx2") static vec match(vec a, vec x, vec b, vec y) {
    return _mm256_and_si256(_mm256_cmpeq_epi8(a, x), _mm256_cmpeq_epi8(b, y));
  }
  SPUD_TARGET("avx2") static vec either(vec v, vec w) {
    return _mm256_or_si256(v, w);
  }
  SPUD_TARGET("avx2") static bool any(vec v) {
    return _mm256_testz_si256(v, v) == 0;
  }
  SPUD_TARGET("avx2") static uint64_t step_bits(const vec *m) {
    return static_cast<uint32_t>(_mm256_movemask_epi8(m[0])) |
           static_cast<uint64_t>(
               static_cast<uint32_t>(_mm256_movemask_epi8(m[1])))
               << 32;
  }
};

} // namespace

SPUD_TARGET("avx2")
size_t scan_anchors_avx2(const pattern_scan &scan, match_fn on_match,
                         void *ctx) {
  return scan_anchors<avx2>(scan, on_match, ctx);
}

} // namespace spud::detail

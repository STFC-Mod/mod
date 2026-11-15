#pragma once

#include <cmath>
#include <cstdint>

namespace MouseNavigation
{
enum class Phase { kDiscrete, kBegan, kChanged, kEnded, kCancelled };

// AppKit/WebKit convention: positive horizontal delta is Back, negative is Forward. Ignore vertical gestures.
inline int32_t Button(double x, double y)
{
  if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) <= std::abs(y))
    return -1;
  return x > 0 ? 3 : 4;
}

struct Swipe {
  int32_t pending = -1;

  int32_t Observe(int32_t button, Phase phase)
  {
    if (phase == Phase::kCancelled) {
      pending = -1;
      return -1;
    }
    if (phase == Phase::kDiscrete) {
      pending = -1;
      return button;
    }
    if (phase == Phase::kBegan)
      pending = -1;
    if (button >= 0)
      pending = button;
    if (phase != Phase::kEnded)
      return -1;
    const auto result = pending;
    pending           = -1;
    return result;
  }
};
} // namespace MouseNavigation

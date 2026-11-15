#pragma once

#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

namespace haven_roads
{
inline constexpr std::size_t kRendererLimit        = 4096;
inline constexpr std::size_t kMaterialsPerRenderer = 16;
inline constexpr std::size_t kMaterialLimit        = kRendererLimit * kMaterialsPerRenderer;

// The backend owns native liveness/rooting. Retained owner and material entries
// are weak; a refresh may hold a temporary rooted owner without retaining it.
template <typename Backend> class DiscoveryCache
{
public:
  using Clock = std::chrono::steady_clock;
  using Weak  = typename Backend::Weak;

  void Refresh(Clock::time_point now)
  {
    if (!Backend::GetOwner(owner_)) {
      owner_ = {};
      materials_.clear();
    }
    if (now < next_discovery_)
      return;
    next_discovery_ = now + std::chrono::seconds(5);
    // A replacement can occupy the canonical path while the old wrapper stays
    // alive and active. Recheck identity at discovery cadence, never per frame.
    auto owner = Backend::FindOwner();
    if (!owner) {
      owner_ = {};
      materials_.clear();
      return;
    }
    owner_ = Backend::Retain(owner);
    if (!owner_) {
      materials_.clear();
      return;
    }
    std::vector<Weak> next;
    if (!Backend::ScanOwner(owner, next)) {
      materials_.clear();
      return;
    }
    if (next.size() > kMaterialLimit)
      next.resize(kMaterialLimit);
    materials_ = std::move(next);
  }

  const std::vector<Weak>& Materials() const
  { return materials_; }

private:
  Weak              owner_;
  std::vector<Weak> materials_;
  Clock::time_point next_discovery_{};
};
} // namespace haven_roads

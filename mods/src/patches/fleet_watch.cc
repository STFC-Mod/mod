#include "patches/fleet_watch.h"

#include "errormsg.h"
#include "patches/patches.h"

#include <prime/fleet.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <vector>

namespace
{
constexpr int     kFleetSlotCount             = 10;
constexpr int64_t kManagerProbeIntervalMs     = 250;
constexpr int64_t kManagerDiscoveryIntervalMs = 5'000;
constexpr int64_t kSeedIntervalMs             = 1'000;
constexpr int64_t kSeedBackoffIntervalMs      = 5'000;
constexpr int64_t kSeedFastPeriodMs           = 30'000;
constexpr int64_t kSeedLifetimeMs             = 120'000;
constexpr int64_t kSeedStabilizationMs        = 5'000;
constexpr int64_t kSeedMaxStabilizationMs     = 30'000;

struct ObservedFleet {
  uint64_t   fleet_id = 0;
  FleetState state    = FleetState::Unknown;
  bool       occupied = false;
};

std::array<ObservedFleet, kFleetSlotCount> s_slots{};
std::vector<fleet_watch::Subscription>     s_subscriptions;
bool                                       s_observer_installed        = false;
bool                                       s_seed_pending              = false;
bool                                       s_seed_has_observation      = false;
bool                                       s_seed_manager_backed       = false;
bool                                       s_seed_finalize_candidate   = false;
int64_t                                    s_seed_started_ms           = 0;
int64_t                                    s_seed_first_observed_ms    = 0;
int64_t                                    s_seed_last_change_ms       = 0;
int64_t                                    s_last_seed_attempt_ms      = 0;
int64_t                                    s_last_manager_probe_ms     = 0;
int64_t                                    s_last_manager_discovery_ms = 0;
int                                        s_manager_probe_slot        = 0;
int                                        s_dispatch_depth            = 0;

class CallbackScope
{
public:
  CallbackScope()
  { ++s_dispatch_depth; }

  ~CallbackScope()
  { --s_dispatch_depth; }
};

int64_t now_milliseconds()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void reset_observation()
{
  s_slots                     = {};
  s_seed_pending              = true;
  s_seed_has_observation      = false;
  s_seed_manager_backed       = false;
  s_seed_finalize_candidate   = false;
  s_seed_started_ms           = now_milliseconds();
  s_seed_first_observed_ms    = 0;
  s_seed_last_change_ms       = 0;
  s_last_seed_attempt_ms      = 0;
  s_last_manager_probe_ms     = 0;
  s_last_manager_discovery_ms = 0;
  s_manager_probe_slot        = 0;
}

void restart_seed(int slot, uint64_t fleet_id)
{
  spdlog::debug("[FleetWatch] fleet topology changed; restarting baseline slot={} fleet={}", slot, fleet_id);
  reset_observation();
}

int occupied_slot_count()
{
  return static_cast<int>(
      std::count_if(s_slots.begin(), s_slots.end(), [](const auto& slot) { return slot.occupied; }));
}

void dispatch_transition(int slot, FleetPlayerData* fleet, FleetState before, FleetState after)
{
  const fleet_watch::Transition transition{
      .before = fleet_watch::Snapshot{slot, fleet->Id(), before},
      .after  = fleet_watch::Snapshot{slot, fleet->Id(), after},
      .fleet  = fleet,
  };
  CallbackScope callback_scope;
  for (const auto& subscription : s_subscriptions) {
    try {
      subscription.on_transition(transition);
    } catch (const std::exception& error) {
      spdlog::warn("[FleetWatch] transition callback failed: {}", error.what());
    } catch (...) {
      spdlog::warn("[FleetWatch] transition callback failed with an unknown exception");
    }
  }
}

void dispatch_baseline()
{
  CallbackScope callback_scope;
  for (const auto& subscription : s_subscriptions) {
    if (!subscription.on_baseline) {
      continue;
    }
    try {
      subscription.on_baseline();
    } catch (const std::exception& error) {
      spdlog::warn("[FleetWatch] baseline callback failed: {}", error.what());
    } catch (...) {
      spdlog::warn("[FleetWatch] baseline callback failed with an unknown exception");
    }
  }
}

void observe_fleet(FleetPlayerData* fleet, int requested_slot, bool publish)
{
  if (!fleet || requested_slot < 0 || requested_slot >= kFleetSlotCount) {
    return;
  }
  const auto slot = requested_slot;

  auto&      previous   = s_slots[slot];
  const auto fleet_id   = fleet->Id();
  const auto state      = fleet->CurrentState();
  const bool same_fleet = previous.occupied && previous.fleet_id == fleet_id;
  const bool same_state = same_fleet && previous.state == state;
  if (!same_fleet && !s_seed_pending) {
    restart_seed(slot, fleet_id);
  }

  const bool changed        = !same_state;
  const auto previous_state = previous.state;
  previous                  = ObservedFleet{fleet_id, state, true};

  if (s_seed_pending && changed) {
    const auto now_ms = now_milliseconds();
    if (!s_seed_has_observation) {
      s_seed_has_observation   = true;
      s_seed_first_observed_ms = now_ms;
    }
    s_seed_last_change_ms     = now_ms;
    s_seed_finalize_candidate = false;
  }
  if (same_fleet && publish && !s_seed_pending && previous_state != state) {
    dispatch_transition(slot, fleet, previous_state, state);
  }
}

struct ScanResult {
  int  observed_count    = 0;
  bool manager_available = false;
};

// Reads every slot while a baseline is being established. Transitions are not published from here.
ScanResult scan_manager()
{
  ScanResult result;
  auto*      manager = FleetsManager::Instance();
  if (!manager || !manager->HasFleetService()) {
    return result;
  }
  result.manager_available = true;

  std::array<FleetPlayerData*, kFleetSlotCount> fleets{};
  bool                                          topology_changed = false;
  for (int slot = 0; slot < kFleetSlotCount; ++slot) {
    fleets[slot] = manager->GetFleetPlayerData(slot);
    if (fleets[slot]) {
      ++result.observed_count;
    }
    const bool occupied = fleets[slot] != nullptr;
    const auto fleet_id = occupied ? fleets[slot]->Id() : 0;
    if (s_slots[slot].occupied != occupied || (occupied && s_slots[slot].fleet_id != fleet_id)) {
      topology_changed = true;
    }
  }

  if (topology_changed && s_seed_pending && s_seed_has_observation) {
    s_seed_last_change_ms     = now_milliseconds();
    s_seed_finalize_candidate = false;
  }

  for (int slot = 0; slot < kFleetSlotCount; ++slot) {
    if (fleets[slot]) {
      observe_fleet(fleets[slot], slot, false);
    } else {
      s_slots[slot] = {};
    }
  }
  s_seed_manager_backed = true;
  return result;
}
} // namespace

namespace fleet_watch
{
bool Subscribe(Subscription subscription)
{
  if (!subscription.on_transition || s_dispatch_depth != 0) {
    return false;
  }
  const auto duplicate = std::find_if(s_subscriptions.begin(), s_subscriptions.end(), [&](const auto& existing) {
    return existing.on_transition == subscription.on_transition;
  });
  if (duplicate != s_subscriptions.end()) {
    return false;
  }

  if (!s_observer_installed) {
    if (!InstallScreenManagerUpdateHook()) {
      return false;
    }
    s_observer_installed = true;
    reset_observation();
  }
  s_subscriptions.push_back(subscription);
  return true;
}

void Tick()
{
  if (s_subscriptions.empty()) {
    return;
  }

  const auto now_ms = now_milliseconds();
  if (s_seed_pending) {
    if (s_seed_started_ms == 0) {
      s_seed_started_ms = now_ms;
    }
    const auto lifetime = now_ms - s_seed_started_ms;
    const auto interval = lifetime < kSeedFastPeriodMs ? kSeedIntervalMs : kSeedBackoffIntervalMs;
    if (s_last_seed_attempt_ms == 0 || now_ms - s_last_seed_attempt_ms >= interval) {
      s_last_seed_attempt_ms = now_ms;
      const auto scan        = scan_manager();
      if (s_seed_manager_backed && !scan.manager_available) {
        reset_observation();
        return;
      }
      if (scan.manager_available && !s_seed_has_observation) {
        s_seed_has_observation   = true;
        s_seed_first_observed_ms = now_ms;
        s_seed_last_change_ms    = now_ms;
      }
    }

    if (s_seed_has_observation) {
      const bool quiet  = now_ms - s_seed_last_change_ms >= kSeedStabilizationMs;
      const bool forced = now_ms - s_seed_first_observed_ms >= kSeedMaxStabilizationMs;
      if (quiet || forced) {
        if (!s_seed_finalize_candidate) {
          s_seed_finalize_candidate = true;
          return;
        }
        const auto final_scan = scan_manager();
        if (!forced && now_ms - s_seed_last_change_ms < kSeedStabilizationMs) {
          s_seed_finalize_candidate = false;
          return;
        }
        if (final_scan.manager_available || !s_seed_manager_backed) {
          const auto baseline_count = final_scan.observed_count > 0 ? final_scan.observed_count : occupied_slot_count();
          s_seed_pending            = false;
          s_last_manager_probe_ms   = now_ms;
          spdlog::debug("[FleetWatch] established stable baseline for {} fleet slots", baseline_count);
          if (final_scan.manager_available) {
            dispatch_baseline();
          }
        }
      }
    } else if (lifetime >= kSeedLifetimeMs) {
      s_seed_pending              = false;
      s_last_manager_discovery_ms = now_ms;
      spdlog::warn("[FleetWatch] baseline stopped after {}ms without an available fleet service", kSeedLifetimeMs);
    }
    return;
  }

  if (!s_seed_manager_backed
      && (s_last_manager_discovery_ms == 0 || now_ms - s_last_manager_discovery_ms >= kManagerDiscoveryIntervalMs)) {
    s_last_manager_discovery_ms = now_ms;
    auto* manager               = FleetsManager::Instance();
    if (manager && manager->HasFleetService()) {
      restart_seed(-1, 0);
      return;
    }
  }

  if (s_seed_manager_backed
      && (s_last_manager_probe_ms == 0 || now_ms - s_last_manager_probe_ms >= kManagerProbeIntervalMs)) {
    s_last_manager_probe_ms = now_ms;
    auto* manager           = FleetsManager::Instance();
    if (!manager || !manager->HasFleetService()) {
      restart_seed(-1, 0);
      return;
    }
    const int slot       = s_manager_probe_slot;
    s_manager_probe_slot = (slot + 1) % kFleetSlotCount;
    auto*      fleet     = manager->GetFleetPlayerData(slot);
    const bool occupied  = fleet != nullptr;
    const auto fleet_id  = occupied ? fleet->Id() : 0;
    if (s_slots[slot].occupied != occupied || (occupied && s_slots[slot].fleet_id != fleet_id)) {
      restart_seed(slot, fleet_id);
      return;
    }
    if (fleet) {
      observe_fleet(fleet, slot, true);
    }
  }
}
} // namespace fleet_watch

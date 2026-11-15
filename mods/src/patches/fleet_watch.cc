#include "patches/fleet_watch.h"

#include "errormsg.h"
#include "patches/screen_update_hook.h"

#include <prime/FleetsManager.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <vector>

namespace
{
constexpr int     kFleetSlotCount             = 10;
constexpr int64_t kManagerProbeIntervalMs     = 250;
constexpr int64_t kManagerDiscoveryIntervalMs = 5'000;
constexpr int64_t kFastPollIntervalMs         = 250;
constexpr int64_t kFastPollBackoffMs          = 5'000;
constexpr int64_t kFastPollPeriodMs           = 30'000;
constexpr int64_t kFastPollLifetimeMs         = 24LL * 60 * 60 * 1'000;
constexpr int64_t kSeedIntervalMs             = 1'000;
constexpr int64_t kSeedBackoffIntervalMs      = 5'000;
constexpr int64_t kSeedFastPeriodMs           = 30'000;
constexpr int64_t kSeedLifetimeMs             = 120'000;
constexpr int64_t kSeedStabilizationMs        = 5'000;
constexpr int64_t kSeedMaxStabilizationMs     = 30'000;

struct ObservedFleet {
  uint64_t   fleet_id             = 0;
  FleetState state                = FleetState::Unknown;
  bool       occupied             = false;
  int64_t    fast_poll_started_ms = 0;
  bool       fast_poll_exhausted  = false;
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
int64_t                                    s_last_fast_poll_ms         = 0;
int                                        s_manager_probe_slot        = 0;
int                                        s_fast_poll_count           = 0;
int                                        s_dispatch_depth            = 0;
bool                                       s_evaluating_fast_poll      = false;
#ifdef _MODDBG
bool s_runtime_probe_enabled = false;
#endif

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

bool needs_fast_poll(FleetState state)
{
  if (s_evaluating_fast_poll) {
    spdlog::warn("[FleetWatch] suppressed recursive fast-poll predicate evaluation");
    return false;
  }
  s_evaluating_fast_poll = true;
  struct PredicateScope {
    ~PredicateScope()
    { s_evaluating_fast_poll = false; }
  } predicate_scope;
  CallbackScope callback_scope;

  for (const auto& subscription : s_subscriptions) {
    if (!subscription.needs_fast_poll) {
      continue;
    }
    try {
      if (subscription.needs_fast_poll(state)) {
        return true;
      }
    } catch (const std::exception& error) {
      spdlog::warn("[FleetWatch] fast-poll predicate failed: {}", error.what());
    } catch (...) {
      spdlog::warn("[FleetWatch] fast-poll predicate failed with an unknown exception");
    }
  }
  return false;
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
  s_last_fast_poll_ms         = 0;
  s_manager_probe_slot        = 0;
  s_fast_poll_count           = 0;
}

void restart_seed(int slot, uint64_t fleet_id)
{
  spdlog::debug("[FleetWatch] fleet topology changed; restarting baseline slot={} fleet={}", slot, fleet_id);
  reset_observation();
}

void clear_slot(int slot)
{
  if (s_slots[slot].occupied && s_slots[slot].fast_poll_started_ms != 0) {
    --s_fast_poll_count;
  }
  s_slots[slot] = {};
}

int occupied_slot_count()
{
  return static_cast<int>(
      std::count_if(s_slots.begin(), s_slots.end(), [](const auto& slot) { return slot.occupied; }));
}

void dispatch_transition(int slot, FleetPlayerData* fleet, FleetState before, FleetState after)
{
  const fleet_watch::Transition transition{
      .before = fleet_watch::Snapshot{slot, fleet->Id, before},
      .after  = fleet_watch::Snapshot{slot, fleet->Id, after},
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
  const auto fleet_id   = fleet->Id;
  const auto state      = fleet->CurrentState;
  const bool same_fleet = previous.occupied && previous.fleet_id == fleet_id;
  const bool same_state = same_fleet && previous.state == state;
  if (!same_fleet && !s_seed_pending) {
    restart_seed(slot, fleet_id);
  }

  const bool changed = !same_state;

  const bool was_fast_polling = previous.occupied && previous.fast_poll_started_ms != 0;
  int64_t    fast_started_ms  = 0;
  const bool fast_exhausted   = same_state && previous.fast_poll_exhausted;
  if (!fast_exhausted && needs_fast_poll(state)) {
    fast_started_ms = same_fleet && was_fast_polling ? previous.fast_poll_started_ms : now_milliseconds();
  }
  const auto previous_state = previous.state;
  previous                  = ObservedFleet{fleet_id, state, true, fast_started_ms, fast_exhausted};

  const bool is_fast_polling = fast_started_ms != 0;
  if (was_fast_polling != is_fast_polling) {
    s_fast_poll_count += is_fast_polling ? 1 : -1;
    if (is_fast_polling && s_fast_poll_count == 1) {
      s_last_fast_poll_ms = 0;
    }
  }
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
  int      observed_count    = 0;
  bool     manager_available = false;
  bool     topology_changed  = false;
  int      changed_slot      = -1;
  uint64_t changed_fleet     = 0;
};

ScanResult scan_manager(bool publish, bool only_fast_polling)
{
  ScanResult result;
  auto*      manager = FleetsManager::Instance();
  if (!manager || !manager->HasFleetService()) {
    return result;
  }
  result.manager_available = true;

  std::array<FleetPlayerData*, kFleetSlotCount> fleets{};
  for (int slot = 0; slot < kFleetSlotCount; ++slot) {
    if (only_fast_polling && (!s_slots[slot].occupied || s_slots[slot].fast_poll_started_ms == 0)) {
      continue;
    }
    fleets[slot] = manager->GetFleetPlayerData(slot);
    if (fleets[slot]) {
      ++result.observed_count;
    }
    if (only_fast_polling) {
      continue;
    }
    const bool occupied = fleets[slot] != nullptr;
    const auto fleet_id = occupied ? fleets[slot]->Id : 0;
    if (s_slots[slot].occupied != occupied || (occupied && s_slots[slot].fleet_id != fleet_id)) {
      result.topology_changed = true;
      if (result.changed_slot < 0) {
        result.changed_slot  = slot;
        result.changed_fleet = fleet_id;
      }
    }
  }

  if (result.topology_changed && publish && !s_seed_pending) {
    restart_seed(result.changed_slot, result.changed_fleet);
    publish = false;
  } else if (result.topology_changed && s_seed_pending && s_seed_has_observation) {
    s_seed_last_change_ms     = now_milliseconds();
    s_seed_finalize_candidate = false;
  }

  for (int slot = 0; slot < kFleetSlotCount; ++slot) {
    if (only_fast_polling && (!s_slots[slot].occupied || s_slots[slot].fast_poll_started_ms == 0)) {
      continue;
    }
    if (fleets[slot]) {
      observe_fleet(fleets[slot], slot, publish);
    } else if (!only_fast_polling) {
      clear_slot(slot);
    }
  }
  s_seed_manager_backed = true;
  return result;
}

bool install_observer()
{
  if (!install_screen_manager_update_hook()) {
    return false;
  }
  return register_screen_manager_update_callback(fleet_watch::Tick);
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
    if (!install_observer()) {
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
      const auto scan        = scan_manager(false, false);
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
        const auto final_scan = scan_manager(false, false);
        if (!forced && now_ms - s_seed_last_change_ms < kSeedStabilizationMs) {
          s_seed_finalize_candidate = false;
          return;
        }
        if (final_scan.manager_available || !s_seed_manager_backed) {
          const auto baseline_count = final_scan.observed_count > 0 ? final_scan.observed_count : occupied_slot_count();
          s_seed_pending            = false;
          s_last_manager_probe_ms   = now_ms;
          s_last_fast_poll_ms       = s_fast_poll_count > 0 ? now_ms : 0;
          spdlog::debug("[FleetWatch] established stable baseline for {} fleet slots", baseline_count);
#ifdef _MODDBG
          if (s_runtime_probe_enabled) {
            spdlog::info("[FleetWatchProbe] established stable baseline for {} fleet slots", baseline_count);
          }
#endif
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
    const auto fleet_id  = occupied ? fleet->Id : 0;
    if (s_slots[slot].occupied != occupied || (occupied && s_slots[slot].fleet_id != fleet_id)) {
      restart_seed(slot, fleet_id);
      return;
    }
    if (fleet) {
      observe_fleet(fleet, slot, true);
    }
  }

  if (s_fast_poll_count <= 0) {
    return;
  }
  bool fast_period = false;
  for (auto& slot : s_slots) {
    if (!slot.occupied || slot.fast_poll_started_ms == 0) {
      continue;
    }
    const auto lifetime = now_ms - slot.fast_poll_started_ms;
    if (lifetime >= kFastPollLifetimeMs) {
      slot.fast_poll_started_ms = 0;
      slot.fast_poll_exhausted  = true;
      --s_fast_poll_count;
    } else if (lifetime < kFastPollPeriodMs) {
      fast_period = true;
    }
  }
  const auto interval = fast_period ? kFastPollIntervalMs : kFastPollBackoffMs;
  if (s_fast_poll_count > 0 && (s_last_fast_poll_ms == 0 || now_ms - s_last_fast_poll_ms >= interval)) {
    s_last_fast_poll_ms = now_ms;
    scan_manager(true, true);
  }
}

#ifdef _MODDBG
namespace
{
  void runtime_probe_transition(const Transition& transition)
  {
    spdlog::info("[FleetWatchProbe] slot={} fleet={} oldState={} newState={}", transition.after.slot,
                 transition.after.fleet_id, static_cast<int>(transition.before.state),
                 static_cast<int>(transition.after.state));
  }

  bool runtime_probe_fast_poll(FleetState state)
  { return state != FleetState::Unknown; }
} // namespace

void InstallRuntimeProbe()
{
  const auto* enabled = std::getenv("STFC_MOD_FLEET_WATCH_PROBE");
  if (!enabled || std::string_view{enabled} != "1") {
    return;
  }
  s_runtime_probe_enabled = true;
  if (Subscribe({runtime_probe_transition, runtime_probe_fast_poll})) {
    spdlog::info("[FleetWatchProbe] runtime subscriber installed");
  } else {
    s_runtime_probe_enabled = false;
    spdlog::warn("[FleetWatchProbe] runtime subscriber installation failed");
  }
}
#endif
} // namespace fleet_watch

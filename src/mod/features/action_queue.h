#pragma once

// The decisions behind the Kir'Shara queue fixes in action_queue.cc, kept free of game types so tests can drive
// them. Each fix acts only on narrow, observed evidence and otherwise leaves the game's own handling alone.

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace action_queue
{
// --- Faster queue recovery ---
//
// A target removed while its course request is outstanding can make the failed response retry nothing, which leaves
// the next target waiting for the watchdog. A failed response may hand straight over to the native planner when it
// answers the latest attempt this queue made.

// A queue as the course response sees it. `contains_target` is relative to the failed target.
struct RetryState {
  bool         valid{};
  std::int64_t fleet{}, front{};
  float        attempt{};
  int          count{};
  bool         engaging{}, contains_target{};
};

// The queue the failure belongs to has moved past the failed target and is idle. The pending-target field describes
// the last successful response, not necessarily the outstanding request, so it is deliberately not an input.
inline bool CanAdvance(const RetryState& state, std::int64_t failed_target)
{
  return state.valid && failed_target != 0 && state.count > 0 && state.front != failed_target && !state.engaging
         && !state.contains_target;
}

// The latest engagement attempt per dock slot: the fleet, the head it went for, and the queue's attempt time when it
// went. Fleet, target and attempt time together name one attempt, so a stale failure for an older target or attempt,
// or for a queue from an earlier session, matches nothing and leaves the native retry alone.
class Attempts
{
public:
  // FleetsManager's ten dock slots. A fleet in a slot past them keeps the native retry.
  static constexpr int kSlots = 10;

  // An attempt without a readable head still clears the slot, so an older attempt cannot answer for it.
  void Remember(int slot, const RetryState& state)
  {
    if (slot < 0 || slot >= kSlots)
      return;
    attempts_[slot] = {};
    if (state.valid && state.count > 0 && state.front != 0)
      attempts_[slot] = {state.fleet, state.front, state.attempt};
  }

  // An attempt that did not dispatch must not authorize a later failure response.
  void Forget(int slot)
  {
    if (slot >= 0 && slot < kSlots)
      attempts_[slot] = {};
  }

  // True once for a failure that answers the remembered attempt of the same fleet.
  bool Consume(const RetryState& state, std::int64_t target)
  {
    for (auto& attempt : attempts_) {
      if (!attempt.fleet || attempt.fleet != state.fleet)
        continue;
      if (attempt.target != target || attempt.attempt != state.attempt)
        return false;
      attempt = {};
      return true;
    }
    return false;
  }

private:
  struct Attempt {
    std::int64_t fleet{}, target{};
    float        attempt{};
  };
  std::array<Attempt, kSlots> attempts_{};
};

// --- Thin queue protection ---
//
// The native planner and watchdog can drop a prefix of targets and then leave the fleet idle with the rest still
// queued. When that is exactly what happened, the fleet is asked to engage its new head.

// A queue's targets and engagement latches.
struct QueueSnapshot {
  bool                      present         = false;
  std::int64_t              player_fleet_id = 0;
  std::vector<std::int64_t> targets;
  bool                      is_engaging            = false;
  std::int64_t              last_engaged_target_id = 0;
  std::int64_t              pending_target_id      = 0;

  std::int64_t Head() const
  { return targets.empty() ? 0 : targets.front(); }
};

// A destroyed target the native disposal left at the head of an idle queue.
inline bool ShouldProcessDestroyedHead(std::int64_t target_id, const QueueSnapshot& after_native)
{
  return target_id != 0 && after_native.present && after_native.Head() == target_id && !after_native.is_engaging
         && after_native.last_engaged_target_id != target_id && after_native.pending_target_id != target_id;
}

// The native call removed an exact prefix and left the same fleet idle with an unchanged remainder, and neither
// latch names a target that is still queued.
inline bool IsNativePruneResumeCandidate(bool player_fleet_idle, const QueueSnapshot& before_native,
                                         const QueueSnapshot& after_native)
{
  const auto before = std::span(before_native.targets);
  const auto after  = std::span(after_native.targets);
  if (!player_fleet_idle || !before_native.present || !after_native.present || before_native.player_fleet_id == 0
      || before_native.player_fleet_id != after_native.player_fleet_id || after.empty() || after.size() >= before.size()
      || after.front() == before.front() || after_native.is_engaging)
    return false;

  // A latch that names nothing, or only a target the native call removed, is stale rather than live.
  const auto removed = before.first(before.size() - after.size());
  const auto stale   = [&](std::int64_t target) { return target <= 0 || std::ranges::contains(removed, target); };
  return std::ranges::equal(after, before.subspan(removed.size())) && stale(after_native.last_engaged_target_id)
         && stale(after_native.pending_target_id);
}

// --- Queue address guard ---
//
// Moving or recalling another ship can briefly report a deployed ship's address as its starbase planet. The next
// player-state event then sees that ship's same-system targets as foreign and clears its queue.

struct Address {
  bool         valid{};
  std::int64_t galaxy{}, system{}, planet{};
  int          instance{};
  bool         operator==(const Address&) const = default;
};

// FleetState and DeployedFleetState values, as the game defines them.
constexpr int kFleetIdleInSpace = 1, kFleetBattling = 64, kFleetImpulsing = 512;
constexpr int kDeployedIdle = 0, kDeployedMoving = 1, kDeployedBattling = 3, kDeployedPreBattle = 6;

// What the queue's address check saw for the fleet, its own deployment, and the target being compared.
struct AddressEvidence {
  bool    planning_recall{};
  int     player_state{-1}, deployed_state{-1}, removal_reason{-1};
  Address reported, deployed, target;
};

// Only the observed substitution is corrected: an active ship reported at a planet in another system while its own
// deployment, and its target, are both still at the deployment's address.
inline bool UseDeployedAddress(const AddressEvidence& e)
{
  const bool player_active =
      e.player_state == kFleetIdleInSpace || e.player_state == kFleetBattling || e.player_state == kFleetImpulsing;
  const bool deployed_active = e.deployed_state == kDeployedIdle || e.deployed_state == kDeployedMoving
                               || e.deployed_state == kDeployedBattling || e.deployed_state == kDeployedPreBattle;
  return !e.planning_recall && e.removal_reason == 0 && player_active && deployed_active && e.reported.valid
         && e.deployed.valid && e.target.valid && e.reported.planet > 0 && e.deployed.planet == 0
         && e.reported.system != e.deployed.system && e.reported.galaxy == e.deployed.galaxy
         && e.reported.instance == e.deployed.instance && e.deployed == e.target;
}
} // namespace action_queue

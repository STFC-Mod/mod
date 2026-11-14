#pragma once

// The decisions behind the Kir'Shara queue fixes in action_queue.cc, kept free of game types so tests can drive
// them. Each fix acts only on narrow, observed evidence and otherwise leaves the game's own handling alone.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace action_queue
{
using Clock = std::chrono::steady_clock;

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

// The pending-target field describes the last successful response, not necessarily the outstanding request, so
// it is deliberately not an input.
inline bool CanAdvance(const RetryState& state, std::int64_t failed_fleet, std::int64_t failed_target,
                       bool outstanding_at_entry, bool same_queue, bool absent_from_all_queues)
{
  return state.valid && same_queue && outstanding_at_entry && failed_target != 0 && state.fleet == failed_fleet
         && state.count > 0 && state.front != failed_target && !state.engaging && !state.contains_target
         && absent_from_all_queues;
}

// The latest engagement attempt per fleet. Queues are held weakly, so a collected queue cannot be confused with a
// new one at the same address. `Handle` is il2cpp::GcHandle in the game: move-only, `Weak(object)`, `Get()`.
// Callers serialize access.
template <class Handle> class Requests
{
public:
  using Object = decltype(std::declval<const Handle&>().Get());

  // Records an attempt and returns its serial, for Cancel.
  std::uint64_t Remember(Object queue, const RetryState& state, Clock::time_point now)
  {
    // Reuse this fleet's record, else the oldest one; expired records are dropped on the way.
    auto* slot = &records_.front();
    for (auto& record : records_) {
      if (record.handle && (now - record.started >= kLifetime || !record.handle.Get()))
        record = {};
      if (record.fleet == state.fleet) {
        slot = &record;
        break;
      }
      if (record.started < slot->started)
        slot = &record;
    }
    // An attempt without a readable head still clears the slot, so an older attempt cannot answer for it.
    *slot             = {};
    const auto serial = ++serial_;
    if (state.valid && state.count > 0 && state.front != 0)
      *slot = {Handle::Weak(queue), state.fleet, state.front, state.attempt, now, serial};
    return serial;
  }

  // An attempt that did not dispatch must not authorize a later failure response. Only that attempt's own record
  // goes, so an older reentrant call cannot cancel a newer one.
  void Cancel(std::uint64_t serial)
  {
    for (auto& record : records_)
      if (record.serial == serial)
        record = {};
  }

  // True once for a failure that answers the remembered attempt of the same live queue.
  bool Consume(Object queue, const RetryState& state, std::int64_t target, Clock::time_point now)
  {
    for (auto& record : records_) {
      if (record.fleet != state.fleet)
        continue;
      if (!record.handle || now - record.started >= kLifetime || !record.handle.Get()) {
        record = {};
        return false;
      }
      if (record.handle.Get() != queue || record.target != target || record.attempt != state.attempt)
        return false;
      record = {};
      return true;
    }
    return false;
  }

  void Clear()
  { records_ = {}; }

private:
  struct Record {
    Handle            handle{};
    std::int64_t      fleet{}, target{};
    float             attempt{};
    Clock::time_point started{};
    std::uint64_t     serial{};
  };
  // A response later than this belongs to some other attempt.
  static constexpr auto kLifetime = std::chrono::seconds(30);
  // One per dock slot, with room to spare.
  std::array<Record, 8> records_{};
  std::uint64_t         serial_{};
};

// --- Thin queue protection ---
//
// The native planner and watchdog can drop a prefix of targets and then leave the fleet idle with the rest still
// queued. When that is exactly what happened, the fleet is asked to engage its new head.

// Digit.Prime.Combat.EngageResult.
constexpr int kEngageSuccess    = 0;
constexpr int kEngageSkipTarget = 1;
constexpr int kEngageStop       = 2;

constexpr std::string_view EngageResultName(int result)
{
  switch (result) {
    case kEngageSuccess:
      return "success";
    case kEngageSkipTarget:
      return "skip-target";
    case kEngageStop:
      return "stop";
    default:
      return result < 0 ? "not-attempted" : "unknown";
  }
}

// A queue's targets and engagement latches. Only the first eight targets are captured; a longer queue is marked
// truncated and never treated as evidence.
struct QueueSnapshot {
  bool                        present         = false;
  std::int64_t                player_fleet_id = 0;
  int                         count           = -1;
  std::int64_t                head_target_id  = 0;
  std::array<std::int64_t, 8> target_ids{};
  int                         captured_target_count  = 0;
  bool                        targets_truncated      = false;
  bool                        is_engaging            = false;
  std::int64_t                last_engaged_target_id = 0;
  std::int64_t                pending_target_id      = 0;

  constexpr std::span<const std::int64_t> Targets() const
  { return std::span(target_ids).first(static_cast<std::size_t>(captured_target_count)); }
};

// A destroyed target the native disposal left at the head of an idle queue.
constexpr bool ShouldProcessDestroyedHead(bool enabled, bool target_destroyed, std::int64_t target_id,
                                          const QueueSnapshot& after_native)
{
  return enabled && target_destroyed && target_id != 0 && after_native.present && after_native.count > 0
         && after_native.head_target_id == target_id && !after_native.is_engaging
         && after_native.last_engaged_target_id != target_id && after_native.pending_target_id != target_id;
}

// The native call removed an exact prefix and left the same fleet idle with an unchanged remainder, and neither
// latch names a target that is still queued.
constexpr bool IsNativePruneResumeCandidate(bool enabled, bool player_fleet_idle, const QueueSnapshot& before_native,
                                            const QueueSnapshot& after_native)
{
  if (!enabled || !player_fleet_idle || !before_native.present || !after_native.present
      || before_native.player_fleet_id == 0 || before_native.player_fleet_id != after_native.player_fleet_id
      || before_native.count <= 0 || after_native.count <= 0 || after_native.count >= before_native.count
      || before_native.head_target_id == 0 || after_native.head_target_id == 0
      || after_native.head_target_id == before_native.head_target_id || after_native.is_engaging
      || before_native.targets_truncated || after_native.targets_truncated
      || before_native.captured_target_count != before_native.count
      || after_native.captured_target_count != after_native.count)
    return false;

  // A latch that names nothing, or only a target the native call removed, is stale rather than live.
  const auto removed =
      before_native.Targets().first(static_cast<std::size_t>(before_native.count - after_native.count));
  const auto stale = [&](std::int64_t target) { return target <= 0 || std::ranges::contains(removed, target); };
  return std::ranges::equal(after_native.Targets(), before_native.Targets().subspan(removed.size()))
         && stale(after_native.last_engaged_target_id) && stale(after_native.pending_target_id);
}

// The queue read again just before resuming still matches what the decision was made on.
constexpr bool IsStableResumePostcondition(const QueueSnapshot& expected, const QueueSnapshot& confirmed)
{
  if (!expected.present || !confirmed.present || expected.player_fleet_id == 0
      || confirmed.player_fleet_id != expected.player_fleet_id || expected.count <= 0
      || confirmed.count != expected.count || expected.head_target_id == 0
      || confirmed.head_target_id != expected.head_target_id || expected.targets_truncated
      || confirmed.targets_truncated || expected.captured_target_count != expected.count
      || confirmed.captured_target_count != confirmed.count || confirmed.is_engaging || expected.is_engaging
      || confirmed.last_engaged_target_id != expected.last_engaged_target_id
      || confirmed.pending_target_id != expected.pending_target_id)
    return false;

  // Both queues are fully captured by now, so Targets() is the whole queue.
  const auto queued = [&](std::int64_t target) {
    return target > 0 && std::ranges::contains(confirmed.Targets(), target);
  };
  return std::ranges::equal(confirmed.Targets(), expected.Targets()) && !queued(confirmed.last_engaged_target_id)
         && !queued(confirmed.pending_target_id);
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

struct AddressEvidence {
  bool    enabled{}, player_state_event{}, owned_queue{}, owned_deployment{}, planning_recall{};
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
  return e.enabled && e.player_state_event && e.owned_queue && e.owned_deployment && !e.planning_recall
         && e.removal_reason == 0 && player_active && deployed_active && e.reported.valid && e.deployed.valid
         && e.target.valid && e.reported.planet > 0 && e.deployed.planet == 0 && e.reported.system != e.deployed.system
         && e.reported.galaxy == e.deployed.galaxy && e.reported.instance == e.deployed.instance
         && e.deployed == e.target;
}
} // namespace action_queue

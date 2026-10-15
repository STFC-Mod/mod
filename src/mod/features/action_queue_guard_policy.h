#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace action_queue_guard
{
constexpr int kEngageResultSuccess    = 0;
constexpr int kEngageResultSkipTarget = 1;
constexpr int kEngageResultStop       = 2;

constexpr std::string_view EngageResultName(int result)
{
  switch (result) {
    case kEngageResultSuccess:
      return "success";
    case kEngageResultSkipTarget:
      return "skip-target";
    case kEngageResultStop:
      return "stop";
    default:
      return result < 0 ? "not-attempted" : "unknown";
  }
}

struct QueueState {
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
};

constexpr bool ShouldProcessDestroyedHead(bool enabled, bool target_destroyed, std::int64_t target_id,
                                          const QueueState& after_native)
{
  return enabled && target_destroyed && target_id != 0 && after_native.present && after_native.count > 0
         && after_native.head_target_id == target_id && !after_native.is_engaging
         && after_native.last_engaged_target_id != target_id && after_native.pending_target_id != target_id;
}

constexpr bool IsNoTargetOrRemovedPrefix(std::int64_t target_id, const QueueState& before_native, int removed_prefix)
{
  if (target_id <= 0) {
    return true;
  }

  for (int index = 0; index < removed_prefix; ++index) {
    if (before_native.target_ids[index] == target_id) {
      return true;
    }
  }
  return false;
}

constexpr bool LatchNamesSurvivingTarget(std::int64_t target_id, const QueueState& queue)
{
  if (target_id <= 0) {
    return false;
  }

  for (int index = 0; index < queue.count; ++index) {
    if (queue.target_ids[index] == target_id) {
      return true;
    }
  }
  return false;
}

constexpr bool IsNativePruneResumeCandidate(bool enabled, bool player_fleet_idle, const QueueState& before_native,
                                            const QueueState& after_native)
{
  if (!enabled || !player_fleet_idle || !before_native.present || !after_native.present
      || before_native.player_fleet_id == 0 || before_native.player_fleet_id != after_native.player_fleet_id
      || before_native.count <= 0 || after_native.count <= 0 || after_native.count >= before_native.count
      || before_native.head_target_id == 0 || after_native.head_target_id == 0
      || after_native.head_target_id == before_native.head_target_id || after_native.is_engaging
      || before_native.targets_truncated || after_native.targets_truncated
      || before_native.captured_target_count != before_native.count
      || after_native.captured_target_count != after_native.count) {
    return false;
  }

  const auto removed_prefix = before_native.count - after_native.count;
  for (int index = 0; index < after_native.count; ++index) {
    if (after_native.target_ids[index] != before_native.target_ids[index + removed_prefix]) {
      return false;
    }
  }

  return IsNoTargetOrRemovedPrefix(after_native.last_engaged_target_id, before_native, removed_prefix)
         && IsNoTargetOrRemovedPrefix(after_native.pending_target_id, before_native, removed_prefix);
}

constexpr bool IsStableResumePostcondition(const QueueState& expected, const QueueState& confirmed)
{
  if (!expected.present || !confirmed.present || expected.player_fleet_id == 0
      || confirmed.player_fleet_id != expected.player_fleet_id || expected.count <= 0
      || confirmed.count != expected.count || expected.head_target_id == 0
      || confirmed.head_target_id != expected.head_target_id || expected.targets_truncated
      || confirmed.targets_truncated || expected.captured_target_count != expected.count
      || confirmed.captured_target_count != confirmed.count || confirmed.is_engaging || expected.is_engaging
      || confirmed.last_engaged_target_id != expected.last_engaged_target_id
      || confirmed.pending_target_id != expected.pending_target_id
      || LatchNamesSurvivingTarget(confirmed.last_engaged_target_id, confirmed)
      || LatchNamesSurvivingTarget(confirmed.pending_target_id, confirmed)) {
    return false;
  }

  for (int index = 0; index < expected.count; ++index) {
    if (confirmed.target_ids[index] != expected.target_ids[index]) {
      return false;
    }
  }
  return true;
}
} // namespace action_queue_guard

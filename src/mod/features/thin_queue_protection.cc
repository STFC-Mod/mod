#include "features/action_queue_guard_policy.h"
#include "features/features.h"
#include <config.h>

// Port of the active v2.1.0-guffa.10 guard, not the dormant completion repair.
// Shared 64-bit managed layout; incompatible runtime contracts leave the feature unavailable.
#include "game/queues.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <il2cpp/hook.h>
#include <spdlog/spdlog.h>
#include <utility>

using namespace queue_bindings::thin;

namespace
{
using Object = Il2CppObject;
using action_queue_guard::QueueState;
std::atomic_bool ready{false};

using Id = std::int64_t;

bool Enabled()
{ return ready.load() && Config::Get().control.kirshara_fixes && Config::Get().control.queue_enabled; }

bool Resume(Object* manager, Object* queue, Object* player, bool idle, const QueueState& before,
            const QueueState& after, const char* source)
{
  if (!action_queue_guard::IsNativePruneResumeCandidate(Enabled(), idle, before, after))
    return false;
  const auto confirmed = Snapshot(queue);
  if (!action_queue_guard::IsStableResumePostcondition(after, confirmed))
    return false;
  // Call the existing native engage entry, including the Faster Queue Recovery detour.
  // Never edit queue contents, engagement flags, retry counts or the native watchdog.
  const auto result = tryEngage(manager, player, queue);
  spdlog::info("[ThinQueueProtection] resume source={} fleet={} old_head={} head={} count={} result={}", source,
               confirmed.player_fleet_id, before.head_target_id, confirmed.head_target_id, confirmed.count, result);
  return result == action_queue_guard::kEngageResultSuccess;
}

bool ActionQueueManager_DoPlanPathAndEngageTarget_Hook(auto original, Object* manager, Object* player)
{
  if (!Enabled() || !Is(player, playerClass))
    return original(manager, player);
  const auto id     = playerId(player);
  auto*      queue  = FindQueue(manager, id);
  const auto before = Snapshot(queue);
  const bool result = original(manager, player);
  // Session/queue replacement is not evidence that native pruning removed a prefix.
  if (FindQueue(manager, id) != queue)
    return result;
  const auto after = Snapshot(queue);
  const bool idle  = !result && before.player_fleet_id == id && playerState(player) == 1;
  return Resume(manager, queue, player, idle, before, after, "planner") || result;
}

void ActionQueueManager_HandleStall_Hook(auto original, Object* manager, Object* queue, Object* player,
                                         Object* deployed)
{
  if (!Enabled() || !Is(player, playerClass) || !Is(deployed, deployedClass)) {
    original(manager, queue, player, deployed);
    return;
  }
  const auto before = Snapshot(queue);
  original(manager, queue, player, deployed);
  if (FindQueue(manager, before.player_fleet_id) != queue)
    return;
  const auto after = Snapshot(queue);
  const bool idle  = before.player_fleet_id == playerId(player) && before.player_fleet_id == deployedId(deployed)
                     && playerState(player) == 1 && deployedState(deployed) == 0 && !destroyed(deployed)
                     && !battling(deployed);
  Resume(manager, queue, player, idle, before, after, "watchdog");
}

void ActionQueueManager_OnFleetsDisposedEventHandler_Hook(auto original, Object* manager, Object* fleets)
{
  std::array<Id, 64> targets{};
  unsigned           captured{};
  if (Enabled()) {
    int count{};
    if (auto* items = List(fleets, deployedClass, 4096, count)) {
      for (int i = 0; i < std::min(count, static_cast<int>(targets.size())); ++i) {
        auto* fleet = static_cast<Object*>(items->vector[i]);
        if (Is(fleet, deployedClass) && destroyed(fleet) && removalReason(fleet) == 1) {
          const auto id = deployedId(fleet);
          if (id && std::find(targets.begin(), targets.begin() + captured, id) == targets.begin() + captured)
            targets[captured++] = id;
        }
      }
    }
  }
  original(manager, fleets);
  for (unsigned i = 0; i < captured && Enabled(); ++i) {
    const auto target = targets[i];
    auto*      queue  = FindQueue(manager, 0, target);
    const auto before = Snapshot(queue);
    if (!action_queue_guard::ShouldProcessDestroyedHead(true, true, target, before))
      continue;
    if (FindQueue(manager, 0, target) != queue)
      continue;
    const auto confirmed = Snapshot(queue);
    if (!action_queue_guard::ShouldProcessDestroyedHead(true, true, target, confirmed)
        || confirmed.player_fleet_id != before.player_fleet_id || confirmed.count != before.count)
      continue;
    processTarget(manager, target, false);
    spdlog::info("[ThinQueueProtection] process-destroyed-head fleet={} target={}", before.player_fleet_id, target);
  }
}

} // namespace

namespace
{
void InstallThinQueueProtection()
{
  if (!ResolveBindings())
    return;
  const bool a = IL2CPP_DETOUR(plan, ActionQueueManager_DoPlanPathAndEngageTarget_Hook,
                               "ActionQueueManager_DoPlanPathAndEngageTarget_Hook")
                 != nullptr;
  const bool b =
      IL2CPP_DETOUR(stall, ActionQueueManager_HandleStall_Hook, "ActionQueueManager_HandleStall_Hook") != nullptr;
  const bool c = IL2CPP_DETOUR(disposed, ActionQueueManager_OnFleetsDisposedEventHandler_Hook,
                               "ActionQueueManager_OnFleetsDisposedEventHandler_Hook")
                 != nullptr;
  for (const auto& [installed, name] : std::array{std::pair{a, "ActionQueueManager.DoPlanPathAndEngageTarget"},
                                                  std::pair{b, "ActionQueueManager.HandleStall"},
                                                  std::pair{c, "ActionQueueManager.OnFleetsDisposedEventHandler"}})
    if (!installed)
      spdlog::warn("[ThinQueueProtection] hook installation failed: {}; feature unavailable", name);
  ready.store(a && b && c);
  spdlog::info("[ThinQueueProtection] ready={}", ready.load());
}
} // namespace

const Feature kThinQueueProtection{"ThinQueueProtection", &Config::Patches::thinqueueprotectionhooks,
                                   InstallThinQueueProtection};

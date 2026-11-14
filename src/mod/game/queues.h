#pragma once

// The action queue (Kir'Shara): the manager, one queue of target fleets per dock slot, and the course responses
// that drive it.

#include "fleet.h"

#include <il2cpp/il2cpp.h>

#include <cstddef>
#include <cstdint>

enum class EngageResult : int32_t {
  Success    = 0,
  SkipTarget = 1,
  Stop       = 2,
};

struct QueueableAction : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "QueueableAction">;

  // The queued target's fleet id.
  int64_t FleetId()
  { return Field<"<FleetId>k__BackingField", int64_t>(); }
};

struct ActionQueueInstance : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueInstance">;

  bool IsEngaging()
  { return Field<"IsEngaging", bool>(); }
  float LastEngageAttemptTime()
  { return Field<"LastEngageAttemptTime", float>(); }
  int64_t LastEngagedTargetId()
  { return Field<"LastEngagedTargetId", int64_t>(); }
  int64_t PendingEngageTargetId()
  { return Field<"PendingEngageTargetId", int64_t>(); }
  int64_t PlayerFleetId()
  { return Field<"<PlayerFleetId>k__BackingField", int64_t>(); }
  // IList<QueueableAction>, a List`1 in practice.
  il2cpp::List<QueueableAction*>* actionQueue()
  { return Field<"_actionQueue", il2cpp::List<QueueableAction*>*>(); }
};

// Digit.PrimeServer.Events.SetCourseResponseEventArgs, passed by value. The method lookup checks its size; the
// field order follows the client's dump.
struct SetCourseResponse {
  int64_t fleet_id;
  bool    success;
  bool    is_recall;
  // A boxed Int64.
  il2cpp::Object* target_id;
};
static_assert(sizeof(SetCourseResponse) == 24 && offsetof(SetCourseResponse, target_id) == 16);

struct ActionQueueManager : MonoSingleton<ActionQueueManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager">;

  // One queue per dock slot, indexed by FleetPlayerData.Index; empty slots are null.
  il2cpp::Array<ActionQueueInstance*>* battleQueue()
  { return Field<"_battleQueue", il2cpp::Array<ActionQueueInstance*>*>(); }

  void ClearQueue(FleetPlayerData* playerData)
  { Call<"ClearQueue">(playerData); }
  bool IsQueueFull(FleetPlayerData* playerData)
  { return Call<"IsQueueFull", bool>(playerData); }
  bool IsQueueUnlocked()
  { return Call<"IsQueueUnlocked", bool>(); }
  EngageResult TryPlanPathAndEngageTarget(FleetPlayerData* fleet, ActionQueueInstance* queue)
  { return Call<"TryPlanPathAndEngageTarget", EngageResult(FleetPlayerData*, ActionQueueInstance*)>(fleet, queue); }
  // Removes the target fleet from every queue that holds it, as the game does when a target goes away.
  void ProcessQueue(int64_t target_fleet_id, bool can_select_new_target)
  { Call<"ProcessQueue", void(int64_t, bool)>(target_fleet_id, can_select_new_target); }
};

struct DeploymentService : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "DeploymentService">;
};

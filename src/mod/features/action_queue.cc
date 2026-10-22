// [control] kirshara_fixes: three fixes for the Kir'Shara action queue, each with its own [patches] group.
//
// - ActionQueueRecovery hands a failed course response straight to the native planner when its target is gone.
// - ThinQueueProtection resumes a fleet the native planner or watchdog left idle after pruning its queue.
// - QueueAddressGuard keeps a queue from being cleared over a misreported fleet address.
//
// The decisions live in action_queue.h. The hooks here gather evidence around the game's own handlers, and only
// ever call back into the game's own entry points; they never edit queues, flags or retry counts.

#include "features/action_queue.h"
#include "config.h"
#include "features/features.h"

#include "game/queues.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <mutex>
#include <utility>

namespace
{
using action_queue::Clock;

// The only check a hook needs: each fix installs all of its hooks or none, so a hook that runs has its partners.
bool QueueFixesEnabled()
{
  const auto& control = Config::Get().control;
  return control.kirshara_fixes && control.queue_enabled;
}

// Sets a thread-local for the length of one native call, so hooks the game reaches from inside it can see it.
template <typename T> class Scoped
{
public:
  Scoped(T& slot, T value)
      : slot_(slot)
      , previous_(std::exchange(slot, value))
  {
  }
  ~Scoped()
  { slot_ = previous_; }
  Scoped(const Scoped&)            = delete;
  Scoped& operator=(const Scoped&) = delete;

private:
  T& slot_;
  T  previous_;
};

// A queue's actions and their count, or null when the queue has no list.
il2cpp::List<QueueableAction*>* Actions(ActionQueueInstance* queue, int32_t& count)
{
  auto* list = queue ? queue->actionQueue() : nullptr;
  count      = list ? list->Count() : 0;
  return list;
}

ActionQueueInstance* FindQueue(ActionQueueManager* manager, int64_t fleet_id)
{
  if (auto* queues = manager->battleQueue())
    for (auto* queue : *queues)
      if (queue && queue->PlayerFleetId() == fleet_id)
        return queue;
  return nullptr;
}

action_queue::QueueSnapshot SnapshotOf(ActionQueueInstance* queue)
{
  action_queue::QueueSnapshot snapshot;
  int32_t                     count   = 0;
  auto*                       actions = Actions(queue, count);
  if (!actions)
    return {};
  snapshot.count                  = count;
  snapshot.player_fleet_id        = queue->PlayerFleetId();
  snapshot.is_engaging            = queue->IsEngaging();
  snapshot.last_engaged_target_id = queue->LastEngagedTargetId();
  snapshot.pending_target_id      = queue->PendingEngageTargetId();
  snapshot.captured_target_count  = std::min(count, static_cast<int32_t>(snapshot.target_ids.size()));
  snapshot.targets_truncated      = count > snapshot.captured_target_count;
  for (int32_t i = 0; i < snapshot.captured_target_count; ++i) {
    auto* action = (*actions)[i];
    if (!action || !(snapshot.target_ids[i] = action->FleetId()))
      return {};
  }
  snapshot.head_target_id = count ? snapshot.target_ids[0] : 0;
  snapshot.present        = snapshot.player_fleet_id != 0;
  return snapshot;
}

// --- Faster queue recovery ---

// Shared by four hooks; locked rather than assuming they all run on the game thread.
std::mutex requests_mutex;
// Never destroyed: freeing a GC handle in a static destructor can run after the runtime has shut down.
auto& requests = *new action_queue::Requests<il2cpp::GcHandle>;

// What the course response handler knew on entry, for the retry decision it makes inside.
struct CourseContext {
  int64_t              fleet_id{}, target_id{};
  ActionQueueInstance* queue{};
  bool                 engaging_at_entry{};
};
thread_local CourseContext* current_course = nullptr;

void ClearRequests()
{
  std::lock_guard lock(requests_mutex);
  requests.Clear();
}

// `target`, when given, is checked against every queued action; otherwise only the head is read.
action_queue::RetryState RetryStateOf(ActionQueueInstance* queue, int64_t target = 0)
{
  action_queue::RetryState state;
  if (!queue)
    return state;
  state.fleet     = queue->PlayerFleetId();
  state.attempt   = queue->LastEngageAttemptTime();
  state.engaging  = queue->IsEngaging();
  int32_t count   = 0;
  auto*   actions = Actions(queue, count);
  if (!actions)
    return state;
  state.count = count;
  for (int32_t i = 0; i < (target ? count : std::min(count, 1)); ++i) {
    auto* action = (*actions)[i];
    if (!action)
      return state;
    const auto id = action->FleetId();
    if (i == 0)
      state.front = id;
    state.contains_target |= target && id == target;
  }
  state.valid = state.fleet != 0;
  return state;
}

// An unreadable queue might hold the target, so it counts as holding it.
bool AbsentFromAllQueues(ActionQueueManager* manager, int64_t target)
{
  auto* queues = manager->battleQueue();
  if (!queues)
    return false;
  for (auto* queue : *queues) {
    if (!queue)
      continue;
    const auto state = RetryStateOf(queue, target);
    if (!state.valid || state.contains_target)
      return false;
  }
  return true;
}

EngageResult ActionQueueManager_TryPlanPathAndEngageTarget_Hook(auto original, ActionQueueManager* _this,
                                                                FleetPlayerData* fleet, ActionQueueInstance* queue)
{
  uint64_t serial = 0;
  if (QueueFixesEnabled()) {
    const auto      state = RetryStateOf(queue);
    std::lock_guard lock(requests_mutex);
    serial = requests.Remember(queue, state, Clock::now());
  } else {
    ClearRequests();
  }
  const auto result = original(_this, fleet, queue);
  // Only a dispatched attempt can be answered by a course response.
  if (serial && result != EngageResult::Success) {
    std::lock_guard lock(requests_mutex);
    requests.Cancel(serial);
  }
  return result;
}

// Called by the game from inside OnSetCourseResponseEventHandler.
bool ActionQueueManager_ShouldRetryFailedSetCourse_Hook(auto original, ActionQueueManager* _this, int64_t target,
                                                        ActionQueueInstance* queue)
{
  const bool enabled = QueueFixesEnabled();
  if (!enabled)
    ClearRequests();
  const bool retry = original(_this, target, queue);
  if (retry || !enabled || !current_course)
    return retry;
  const auto& course = *current_course;
  const auto  state  = RetryStateOf(queue, target);
  if (course.target_id != target
      || !action_queue::CanAdvance(state, course.fleet_id, target, course.engaging_at_entry, course.queue == queue,
                                   true))
    return retry;
  // The native false branch removes the target from every fleet's queue. Leave that to it while any queue might
  // still hold the target.
  if (!AbsentFromAllQueues(_this, target))
    return retry;
  std::lock_guard lock(requests_mutex);
  if (!requests.Consume(queue, state, target, Clock::now()))
    return retry;
  // True takes the handler's retry branch, which runs the native planner for this fleet with all of its checks.
  return true;
}

void ActionQueueManager_OnSetCourseResponseEventHandler_Hook(auto original, ActionQueueManager* _this,
                                                             SetCourseResponse response)
{
  const bool enabled = QueueFixesEnabled();
  if (!enabled)
    ClearRequests();
  CourseContext course{response.fleet_id};
  if (enabled && !response.success && !response.is_recall && il2cpp::Unbox(response.target_id, course.target_id)) {
    course.queue             = FindQueue(_this, response.fleet_id);
    course.engaging_at_entry = course.queue && course.queue->IsEngaging();
  }
  Scoped scope(current_course, &course);
  original(_this, response);
}

// Session end, invalidation and quit all pass through here, while the runtime can still free the weak handles.
void ActionQueueManager_StopWatchdogAndClearAllQueues_Hook(auto original, ActionQueueManager* _this)
{
  ClearRequests();
  original(_this);
}

// --- Thin queue protection ---

ActionQueueInstance* FindQueueWithHead(ActionQueueManager* manager, int64_t target)
{
  if (auto* queues = manager->battleQueue())
    for (auto* queue : *queues)
      if (const auto snapshot = SnapshotOf(queue); snapshot.present && snapshot.head_target_id == target)
        return queue;
  return nullptr;
}

// Asks the fleet to engage its new head after a native call pruned its queue, when the evidence allows it.
bool ResumeAfterPrune(ActionQueueManager* manager, ActionQueueInstance* queue, FleetPlayerData* fleet, bool idle,
                      const action_queue::QueueSnapshot& before, const action_queue::QueueSnapshot& after,
                      const char* source)
{
  if (!action_queue::IsNativePruneResumeCandidate(QueueFixesEnabled(), idle, before, after))
    return false;
  const auto confirmed = SnapshotOf(queue);
  if (!action_queue::IsStableResumePostcondition(after, confirmed))
    return false;
  // The game's own engage entry, so faster recovery sees this attempt like any other. If it throws, the binding
  // contains the exception and reports it, but the result reads as Success.
  // TODO(tashcan): let the binding tell a contained exception apart, so a throw is not taken for an engage.
  const auto result = manager->TryPlanPathAndEngageTarget(fleet, queue);
  spdlog::info("[ThinQueueProtection] resumed fleet {} after the {} pruned it: head {} -> {} of {}, {}",
               confirmed.player_fleet_id, source, before.head_target_id, confirmed.head_target_id, confirmed.count,
               action_queue::EngageResultName(static_cast<int>(result)));
  return result == EngageResult::Success;
}

bool ActionQueueManager_DoPlanPathAndEngageTarget_Hook(auto original, ActionQueueManager* _this, FleetPlayerData* fleet)
{
  if (!QueueFixesEnabled() || !fleet)
    return original(_this, fleet);
  const auto id     = fleet->Id();
  auto*      queue  = FindQueue(_this, id);
  const auto before = SnapshotOf(queue);
  const bool result = original(_this, fleet);
  // A replaced queue (a new session) is not evidence of pruning.
  if (FindQueue(_this, id) != queue)
    return result;
  const auto after = SnapshotOf(queue);
  const bool idle  = !result && before.player_fleet_id == id && fleet->CurrentState() == FleetState::IdleInSpace;
  return ResumeAfterPrune(_this, queue, fleet, idle, before, after, "planner") || result;
}

void ActionQueueManager_HandleStall_Hook(auto original, ActionQueueManager* _this, ActionQueueInstance* queue,
                                         FleetPlayerData* fleet, FleetDeployedData* deployed)
{
  if (!QueueFixesEnabled() || !fleet || !deployed)
    return original(_this, queue, fleet, deployed);
  const auto before = SnapshotOf(queue);
  original(_this, queue, fleet, deployed);
  if (FindQueue(_this, before.player_fleet_id) != queue)
    return;
  const auto after = SnapshotOf(queue);
  const bool idle  = before.player_fleet_id == fleet->Id() && before.player_fleet_id == deployed->Id()
                     && fleet->CurrentState() == FleetState::IdleInSpace
                     && deployed->CurrentState() == DeployedFleetState::Idle && !deployed->IsDestroyed()
                     && !deployed->CurrentlyBattling();
  ResumeAfterPrune(_this, queue, fleet, idle, before, after, "watchdog");
}

// A destroyed target can stay at the head of an idle queue after disposal; the game's ProcessQueue removes it.
void ActionQueueManager_OnFleetsDisposedEventHandler_Hook(auto original, ActionQueueManager* _this,
                                                          il2cpp::List<FleetDeployedData*>* fleets)
{
  // Read before the game disposes of them. A wave destroys a handful at a time; 64 is plenty.
  std::array<int64_t, 64> destroyed{};
  size_t                  count = 0;
  const auto              total = QueueFixesEnabled() && fleets ? fleets->Count() : 0;
  for (int32_t i = 0; i < std::min<int32_t>(total, destroyed.size()); ++i) {
    auto* fleet = (*fleets)[i];
    if (!fleet || !fleet->IsDestroyed() || fleet->RemovalReason() != FleetRemovalReason::Destroyed)
      continue;
    const auto id = fleet->Id();
    if (id && std::find(destroyed.begin(), destroyed.begin() + count, id) == destroyed.begin() + count)
      destroyed[count++] = id;
  }
  original(_this, fleets);
  for (size_t i = 0; i < count; ++i) {
    const auto target = destroyed[i];
    const auto queue  = SnapshotOf(FindQueueWithHead(_this, target));
    if (!action_queue::ShouldProcessDestroyedHead(true, true, target, queue))
      continue;
    _this->ProcessQueue(target, false);
    spdlog::info("[ThinQueueProtection] removed destroyed target {} from the head of fleet {}'s queue", target,
                 queue.player_fleet_id);
  }
}

// --- Queue address guard ---

// Set while the game handles a player-state event, the only time a queue is cleared over a misreported address.
thread_local bool in_player_state_event = false;

// What the queue's address check has seen so far: the fleet's own deployment, and the target being compared.
struct AddressCheck {
  FleetPlayerData*   fleet{};
  int64_t            fleet_id{};
  bool               own_resolved{};
  FleetDeployedData* own_deployment{};
  FleetDeployedData* target{};
};
// Set only while the fixes are on, so the hooks that read it need no setting check of their own.
thread_local AddressCheck* current_address_check = nullptr;

action_queue::Address AddressOf(NodeAddress* address)
{
  if (!address)
    return {};
  action_queue::Address value{true, address->Galaxy(), address->System(), address->Planet(), address->Instance()};
  value.valid = value.galaxy >= 0 && value.system > 0 && value.planet >= 0 && value.instance >= 0;
  return value;
}

// The fleet's own slot holds its queue, and every queued action names a target.
bool OwnsNonemptyQueue(ActionQueueManager* manager, FleetPlayerData* fleet, int64_t fleet_id)
{
  auto*      queues = manager->battleQueue();
  const auto index  = fleet->Index();
  if (!queues || !fleet_id || index < 0 || static_cast<size_t>(index) >= queues->size())
    return false;
  auto* queue = (*queues)[index];
  if (!queue || queue->PlayerFleetId() != fleet_id)
    return false;
  int32_t count   = 0;
  auto*   actions = Actions(queue, count);
  if (!actions || count == 0)
    return false;
  for (auto* action : *actions)
    if (!action || !action->FleetId())
      return false;
  return true;
}

bool IsOwnDeployment(FleetDeployedData* deployment, int64_t fleet_id)
{
  auto* model = deployment->deploymentFleet();
  return deployment->localPlayerFleet() && model && model->FleetId() == fleet_id;
}

void ActionQueueManager_OnPlayerFleetStateChangedEventHandler_Hook(auto original, ActionQueueManager* _this,
                                                                   il2cpp::List<FleetPlayerData*>* fleets)
{
  if (!QueueFixesEnabled())
    return original(_this, fleets);
  Scoped scope(in_player_state_event, true);
  original(_this, fleets);
}

bool ActionQueueManager_DoesFleetQueueContainMismatchedAddressTargets_Hook(auto original, ActionQueueManager* _this,
                                                                           FleetPlayerData* fleet)
{
  if (!QueueFixesEnabled() || !in_player_state_event || !fleet)
    return original(_this, fleet);
  const auto fleet_id = fleet->Id();
  if (!OwnsNonemptyQueue(_this, fleet, fleet_id))
    return original(_this, fleet);
  AddressCheck check{fleet, fleet_id};
  Scoped       scope(current_address_check, &check);
  return original(_this, fleet);
}

// The check reads the fleet's address once per target. Inside it, and only there, a fleet whose evidence matches
// the observed fault reports its own deployment's address. Nothing is written or remembered.
NodeAddress* FleetPlayerData_get_Address_Hook(auto original, FleetPlayerData* _this)
{
  auto* reported = original(_this);
  auto* check    = current_address_check;
  if (!check || _this != check->fleet || !check->own_deployment || !check->target)
    return reported;
  auto*                               own      = check->own_deployment;
  auto*                               deployed = own->address();
  const action_queue::AddressEvidence evidence{
      .enabled            = true,
      .player_state_event = in_player_state_event,
      .owned_queue        = true,
      .owned_deployment   = IsOwnDeployment(own, check->fleet_id),
      .planning_recall    = own->IsPlanningRecallCourse(),
      .player_state       = static_cast<int>(_this->CurrentState()),
      .deployed_state     = static_cast<int>(own->CurrentState()),
      .removal_reason     = static_cast<int>(own->RemovalReason()),
      .reported           = AddressOf(reported),
      .deployed           = AddressOf(deployed),
      .target             = AddressOf(check->target->address()),
  };
  if (!action_queue::UseDeployedAddress(evidence))
    return reported;
  spdlog::debug("[QueueAddressGuard] fleet {} kept its queue: reported in system {}, deployed in system {}",
                check->fleet_id, evidence.reported.system, evidence.deployed.system);
  return deployed;
}

// The check looks up each queued target here. The fleet's own deployment is looked up once, alongside.
FleetDeployedData* DeploymentService_GetDeployedFleet_Hook(auto original, DeploymentService* _this, int64_t fleet_id)
{
  auto* result = original(_this, fleet_id);
  if (auto* check = current_address_check; check) {
    check->target = result;
    if (!std::exchange(check->own_resolved, true))
      check->own_deployment = original(_this, check->fleet_id);
  }
  return result;
}

// --- Installation ---
//
// Each fix needs all of its hooks, so each installs as one HookSet: every hook or none.

bool InstallActionQueueRecovery()
{
  using Manager = ActionQueueManager::Class;
  il2cpp::HookSet hooks("ActionQueueRecovery");
  IL2CPP_STAGE(hooks,
               Manager::Method<EngageResult(FleetPlayerData*, ActionQueueInstance*)>("TryPlanPathAndEngageTarget"),
               ActionQueueManager_TryPlanPathAndEngageTarget_Hook);
  IL2CPP_STAGE(hooks, Manager::Method<bool(int64_t, ActionQueueInstance*)>("ShouldRetryFailedSetCourse"),
               ActionQueueManager_ShouldRetryFailedSetCourse_Hook);
  IL2CPP_STAGE(hooks, Manager::Method<void(SetCourseResponse)>("OnSetCourseResponseEventHandler"),
               ActionQueueManager_OnSetCourseResponseEventHandler_Hook);
  IL2CPP_STAGE(hooks, Manager::Method<void()>("StopWatchdogAndClearAllQueues"),
               ActionQueueManager_StopWatchdogAndClearAllQueues_Hook);
  return hooks.Commit();
}

bool InstallThinQueueProtection()
{
  using Manager = ActionQueueManager::Class;
  // Called rather than hooked: a missing one would read as a successful engage.
  if (!Manager::Method<EngageResult(FleetPlayerData*, ActionQueueInstance*)>("TryPlanPathAndEngageTarget"))
    return false;
  il2cpp::HookSet hooks("ThinQueueProtection");
  IL2CPP_STAGE(hooks, Manager::Method<bool(FleetPlayerData*)>("DoPlanPathAndEngageTarget"),
               ActionQueueManager_DoPlanPathAndEngageTarget_Hook);
  IL2CPP_STAGE(hooks, Manager::Method<void(ActionQueueInstance*, FleetPlayerData*, FleetDeployedData*)>("HandleStall"),
               ActionQueueManager_HandleStall_Hook);
  IL2CPP_STAGE(hooks, Manager::Method<void(il2cpp::List<FleetDeployedData*>*)>("OnFleetsDisposedEventHandler"),
               ActionQueueManager_OnFleetsDisposedEventHandler_Hook);
  return hooks.Commit();
}

bool InstallQueueAddressGuard()
{
  using Manager = ActionQueueManager::Class;
  il2cpp::HookSet hooks("QueueAddressGuard");
  IL2CPP_STAGE(hooks, Manager::Method<void(il2cpp::List<FleetPlayerData*>*)>("OnPlayerFleetStateChangedEventHandler"),
               ActionQueueManager_OnPlayerFleetStateChangedEventHandler_Hook);
  IL2CPP_STAGE(hooks, Manager::Method<bool(FleetPlayerData*)>("DoesFleetQueueContainMismatchedAddressTargets"),
               ActionQueueManager_DoesFleetQueueContainMismatchedAddressTargets_Hook);
  IL2CPP_STAGE(hooks, FleetPlayerData::Class::Method<NodeAddress*()>("get_Address"), FleetPlayerData_get_Address_Hook);
  IL2CPP_STAGE(hooks, DeploymentService::Class::Method<FleetDeployedData*(int64_t)>("GetDeployedFleet"),
               DeploymentService_GetDeployedFleet_Hook);
  return hooks.Commit();
}
} // namespace

void ResetQueueRecoveryTracking()
{ ClearRequests(); }

// Recovery and thin protection run on Windows and both Mac architectures: the host ABI passes the 24-byte
// SetCourseResponse as the game does, and the method lookups reject a client whose managed layout differs.
const Feature kActionQueueRecovery{"ActionQueueRecovery", &Config::Patches::actionqueuerecoveryhooks,
                                   InstallActionQueueRecovery};
const Feature kThinQueueProtection{"ThinQueueProtection", &Config::Patches::thinqueueprotectionhooks,
                                   InstallThinQueueProtection};
const Feature kQueueAddressGuard{"QueueAddressGuard", &Config::Patches::queueaddressguardhooks,
                                 InstallQueueAddressGuard};

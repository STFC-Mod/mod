#include "patches/patches.h"
#include "patches/queue_address_guard_policy.h"
#include "prime/queues.h"
#include <array>
#include <atomic>
#include <config.h>
#include <cstring>
#include <il2cpp/hook.h>
#include <spdlog/spdlog.h>

using namespace queue_bindings::address;

namespace
{
using Object = Il2CppObject;
using Id     = std::int64_t;
std::atomic_bool ready{false};

thread_local bool playerStateEvent{};
struct AddressContext {
  Object* player{};
  Id      fleet{};
  bool    ownResolved{};
  Object* ownDeployment{};
  Object* target{};
};
thread_local AddressContext* context{};

bool Enabled()
{ return ready.load() && Config::Get().control.kirshara_fixes && Config::Get().control.queue_enabled; }

void ActionQueueManager_OnPlayerFleetStateChangedEventHandler_Hook(auto original, Object* manager, Object* fleets)
{
  if (!Enabled()) {
    original(manager, fleets);
    return;
  }
  struct EventScope {
    bool previous{playerStateEvent};
    EventScope()
    { playerStateEvent = true; }
    ~EventScope()
    { playerStateEvent = previous; }
  } event;
  original(manager, fleets);
}
bool ActionQueueManager_DoesFleetQueueContainMismatchedAddressTargets_Hook(auto original, Object* manager,
                                                                           Object* player)
{
  if (!Enabled() || !playerStateEvent || !Is(manager, managerClass) || !Is(player, playerClass))
    return original(manager, player);
  const Id fleet = Read<Id>(player, idField);
  if (!OwnsNonemptyQueue(manager, player, fleet))
    return original(manager, player);
  AddressContext evidence{player, fleet};
  struct ContextScope {
    AddressContext* previous{context};
    ContextScope(AddressContext& evidence)
    { context = &evidence; }
    ~ContextScope()
    { context = previous; }
  } scope(evidence);
  return original(manager, player);
}
Object* FleetPlayerData_get_Address_Hook(auto original, Object* player)
{
  auto* result = original(player);
  auto* e      = context;
  if (!Enabled() || !e || player != e->player || !Is(e->ownDeployment, deployedClass) || !Is(e->target, deployedClass))
    return result;
  auto*                               own     = e->ownDeployment;
  auto*                               correct = Reference(own, "_address");
  const queue_address_guard::Evidence proof{true,
                                            playerStateEvent,
                                            true,
                                            IsLocalDeployment(own, e->fleet),
                                            PlanningRecall(own),
                                            State(player, "_fleetStateContainer"),
                                            State(own, "_stateContainer"),
                                            Enum(own, "<RemovalReason>k__BackingField"),
                                            Address(result),
                                            Address(correct),
                                            Address(Reference(e->target, "_address"))};
  // A borrowed current address is returned only during this native comparison.
  // No fleet fields, queues, or previous-frame address caches are changed.
  return queue_address_guard::UseDeployedAddress(proof) ? correct : result;
}
Object* DeploymentService_GetDeployedFleet_Hook(auto original, Object* service, Id target)
{
  auto* result = original(service, target);
  if (Enabled() && context) {
    auto* e   = context;
    e->target = result;
    if (!e->ownResolved) {
      e->ownResolved   = true;
      e->ownDeployment = original(service, e->fleet);
    }
  }
  return result;
}
} // namespace

namespace
{
void InstallQueueAddressGuard()
{
  if (!ResolveBindings())
    return;
  const bool a = IL2CPP_DETOUR(il2cpp::contract::Pointer(methods[0]),
                               ActionQueueManager_OnPlayerFleetStateChangedEventHandler_Hook,
                               "ActionQueueManager_OnPlayerFleetStateChangedEventHandler_Hook")
                 != nullptr;
  const bool b = IL2CPP_DETOUR(il2cpp::contract::Pointer(methods[1]),
                               ActionQueueManager_DoesFleetQueueContainMismatchedAddressTargets_Hook,
                               "ActionQueueManager_DoesFleetQueueContainMismatchedAddressTargets_Hook")
                 != nullptr;
  const bool c = IL2CPP_DETOUR(il2cpp::contract::Pointer(methods[2]), FleetPlayerData_get_Address_Hook,
                               "FleetPlayerData_get_Address_Hook")
                 != nullptr;
  const bool d = IL2CPP_DETOUR(il2cpp::contract::Pointer(methods[3]), DeploymentService_GetDeployedFleet_Hook,
                               "DeploymentService_GetDeployedFleet_Hook")
                 != nullptr;
  ready.store(a && b && c && d);
  spdlog::info("[QueueAddressGuard] ready={}", ready.load());
}
} // namespace

const Feature kQueueAddressGuard{"QueueAddressGuard", &Config::Patches::queueaddressguardhooks,
                                 InstallQueueAddressGuard};

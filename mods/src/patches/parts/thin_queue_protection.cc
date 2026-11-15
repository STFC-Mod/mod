#include "patches/action_queue_guard_policy.h"
#include <config.h>

// Port of the active v2.1.0-guffa.10 guard, not the dormant completion repair.
// Shared 64-bit managed layout; incompatible runtime contracts leave the feature unavailable.
#include <algorithm>
#include <atomic>
#include <array>
#include <utility>
#include <cstring>
#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace
{
using Object = Il2CppObject;
using action_queue_guard::QueueState;
std::atomic_bool ready{false};
Il2CppClass *    queueClass{}, *actionClass{}, *playerClass{}, *deployedClass{};
using Id = std::int64_t;
Id (*playerId)(Object*);
Id (*deployedId)(Object*);
int (*playerState)(Object*);
int (*deployedState)(Object*);
int (*removalReason)(Object*);
bool (*destroyed)(Object*);
bool (*battling)(Object*);
int (*tryEngage)(Object*, Object*, Object*);
void (*processTarget)(Object*, Id, bool);

bool Enabled()
{ return ready.load() && Config::Get().kirshara_fixes && Config::Get().queue_enabled; }

bool Is(Object* object, Il2CppClass* cls)
{ return object && il2cpp_object_get_class(object) == cls; }

template <typename T> T Read(const void* object, std::size_t offset)
{
  T value{};
  if (object)
    std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof(value));
  return value;
}

// Inspect storage without invoking List getters/enumerators. Unknown layouts fail closed.
Il2CppArraySize* List(Object* list, Il2CppClass* element, int limit, int& count)
{
  count = -1;
  if (!list)
    return nullptr;
  auto* cls     = il2cpp_object_get_class(list);
  auto* size    = il2cpp_class_get_field_from_name(cls, "_size");
  auto* storage = il2cpp_class_get_field_from_name(cls, "_items");
  if (!size || !storage || !size->type || size->type->type != IL2CPP_TYPE_I4 || !storage->type
      || storage->type->type != IL2CPP_TYPE_SZARRAY || size->type->byref || storage->type->byref
      || (il2cpp_field_get_flags(size) & FIELD_ATTRIBUTE_STATIC)
      || (il2cpp_field_get_flags(storage) & FIELD_ATTRIBUTE_STATIC))
    return nullptr;
  Il2CppArray* items{};
  il2cpp_field_get_value(list, size, &count);
  il2cpp_field_get_value(list, storage, &items);
  if (count < 0 || count > limit || !items || il2cpp_array_length(items) < static_cast<unsigned>(count)
      || il2cpp_class_get_element_class(il2cpp_object_get_class(reinterpret_cast<Object*>(items))) != element)
    return nullptr;
  return reinterpret_cast<Il2CppArraySize*>(items);
}

QueueState Snapshot(Object* queue)
{
  QueueState s;
  if (!Is(queue, queueClass))
    return s;
  auto* items = List(Read<Object*>(queue, 0x28), actionClass, 128, s.count);
  if (!items)
    return {};
  s.player_fleet_id        = Read<Id>(queue, 0x30);
  s.is_engaging            = Read<bool>(queue, 0x10);
  s.last_engaged_target_id = Read<Id>(queue, 0x18);
  s.pending_target_id      = Read<Id>(queue, 0x20);
  s.captured_target_count  = std::min(s.count, static_cast<int>(s.target_ids.size()));
  s.targets_truncated      = s.count > s.captured_target_count;
  for (int i = 0; i < s.captured_target_count; ++i) {
    auto* action = static_cast<Object*>(items->vector[i]);
    if (!Is(action, actionClass))
      return {};
    s.target_ids[i] = Read<Id>(action, 0x10);
    if (!s.target_ids[i])
      return {};
  }
  s.head_target_id = s.count ? s.target_ids[0] : 0;
  s.present        = s.player_fleet_id != 0;
  return s;
}

Object* FindQueue(Object* manager, Id fleet, Id head = 0)
{
  auto* array = Read<Il2CppArray*>(manager, 0x48);
  if (!array || il2cpp_array_length(array) > 64
      || il2cpp_class_get_element_class(il2cpp_object_get_class(reinterpret_cast<Object*>(array))) != queueClass)
    return nullptr;
  auto* items = reinterpret_cast<Il2CppArraySize*>(array);
  for (unsigned i = 0; i < items->max_length; ++i) {
    auto*      queue = static_cast<Object*>(items->vector[i]);
    const auto s     = Snapshot(queue);
    if (s.present && (head ? s.head_target_id == head : s.player_fleet_id == fleet))
      return queue;
  }
  return nullptr;
}

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

bool Plan(auto original, Object* manager, Object* player)
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

void Stall(auto original, Object* manager, Object* queue, Object* player, Object* deployed)
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

void Disposed(auto original, Object* manager, Object* fleets)
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

bool Field(Il2CppClass* cls, const char* name, std::ptrdiff_t offset, Il2CppTypeEnum type)
{
  auto* f = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  const bool valid = f && f->offset == offset && f->type && !f->type->byref
                     && !(il2cpp_field_get_flags(f) & FIELD_ATTRIBUTE_STATIC) && f->type->type == type;
  if (!valid)
    spdlog::warn("[ThinQueueProtection] {}.{}: expected instance field offset=0x{:X} type={}; "
                   "actual offset={} type={} static={} byref={}",
                   cls ? cls->name : "<missing class>", name, offset, static_cast<int>(type),
                   f ? f->offset : -1, f && f->type ? static_cast<int>(f->type->type) : -1,
                   f && f->type && bool(il2cpp_field_get_flags(f) & FIELD_ATTRIBUTE_STATIC),
                   f && f->type && bool(f->type->byref));
  return valid;
}

template <typename T> bool Getter(T& out, Il2CppClass* cls, const char* name, const char* result)
{
  const auto* method = method_contract::Resolve(cls, name, false, result, {});
  out                = method && !method->has_full_generic_sharing_signature
                           ? reinterpret_cast<T>(method_contract::Pointer(method)) : nullptr;
  if (method && method->return_type->type == IL2CPP_TYPE_VALUETYPE) {
    auto*       type       = il2cpp_class_from_type(method->return_type);
    const auto* underlying = type && il2cpp_class_is_enum(type) ? il2cpp_class_enum_basetype(type) : nullptr;
    if (!underlying || underlying->type != IL2CPP_TYPE_I4)
      out = nullptr;
  }
  if (!out)
    spdlog::warn("[ThinQueueProtection] {}.{}: expected instance {}(); {}", cls ? cls->name : "<missing class>",
                   name, result, !method ? "method unavailable"
                                 : method->has_full_generic_sharing_signature ? "unsupported generic sharing"
                                 : !method->methodPointer ? "native pointer unavailable" : "expected Int32 enum result");
  return out != nullptr;
}
} // namespace

void InstallThinQueueProtection()
{
  auto* manager = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager").get_cls();
  queueClass    = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "ActionQueueInstance").get_cls();
  actionClass   = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "QueueableAction").get_cls();
  playerClass   = il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetPlayerData").get_cls();
  deployedClass = il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData").get_cls();
  using method_contract::Pointer;
  using method_contract::Resolve;
  const auto* plan_info = Resolve(manager, "DoPlanPathAndEngageTarget", false, "System.Boolean",
                                   {"Digit.PrimeServer.Models.FleetPlayerData"});
  auto* plan = Pointer(plan_info);
  const auto* stall_info = Resolve(manager, "HandleStall", false, "System.Void",
                                   {"Prime.ActionQueue.ActionQueueInstance", "Digit.PrimeServer.Models.FleetPlayerData",
                                    "Digit.PrimeServer.Models.FleetDeployedData"});
  auto* stall = Pointer(stall_info);
  const auto* disposed_info = Resolve(manager, "OnFleetsDisposedEventHandler", false, "System.Void",
                                   {"System.Collections.Generic.List<Digit.PrimeServer.Models.FleetDeployedData>"});
  auto* disposed = Pointer(disposed_info);
  const auto* engage = Resolve(manager, "TryPlanPathAndEngageTarget", false, "Digit.Prime.Combat.EngageResult",
                               {"Digit.PrimeServer.Models.FleetPlayerData", "Prime.ActionQueue.ActionQueueInstance"});
  tryEngage          = reinterpret_cast<decltype(tryEngage)>(Pointer(engage));
  const auto* process_info = Resolve(manager, "ProcessQueue", false, "System.Void", {"System.Int64", "System.Boolean"});
  processTarget = reinterpret_cast<decltype(processTarget)>(Pointer(process_info));
  auto*       result     = engage ? il2cpp_class_from_type(engage->return_type) : nullptr;
  const auto* underlying = result && il2cpp_class_is_enum(result) ? il2cpp_class_enum_basetype(result) : nullptr;
  const bool  valid =
      manager && queueClass && actionClass && playerClass && deployedClass && plan && stall && disposed && tryEngage
      && processTarget && underlying && underlying->type == IL2CPP_TYPE_I4
      && !plan_info->has_full_generic_sharing_signature && !stall_info->has_full_generic_sharing_signature
      && !disposed_info->has_full_generic_sharing_signature && !engage->has_full_generic_sharing_signature
      && !process_info->has_full_generic_sharing_signature
      && plan != stall && plan != disposed && stall != disposed
      && Field(manager, "_battleQueue", 0x48, IL2CPP_TYPE_SZARRAY)
      && Field(queueClass, "IsEngaging", 0x10, IL2CPP_TYPE_BOOLEAN)
      && Field(queueClass, "LastEngagedTargetId", 0x18, IL2CPP_TYPE_I8)
      && Field(queueClass, "PendingEngageTargetId", 0x20, IL2CPP_TYPE_I8)
      && Field(queueClass, "_actionQueue", 0x28, IL2CPP_TYPE_GENERICINST)
      && Field(queueClass, "<PlayerFleetId>k__BackingField", 0x30, IL2CPP_TYPE_I8)
      && Field(actionClass, "<FleetId>k__BackingField", 0x10, IL2CPP_TYPE_I8)
      && Getter(playerId, playerClass, "get_Id", "System.Int64")
      && Getter(playerState, playerClass, "get_CurrentState", "Digit.PrimeServer.Models.FleetState")
      && Getter(deployedId, deployedClass, "get_ID", "System.Int64")
      && Getter(deployedState, deployedClass, "get_CurrentState", "Digit.PrimeServer.Models.DeployedFleetState")
      && Getter(removalReason, deployedClass, "get_RemovalReason", "Digit.PrimeServer.Models.FleetRemovalReason")
      && Getter(destroyed, deployedClass, "get_IsDestroyed", "System.Boolean")
      && Getter(battling, deployedClass, "get_CurrentlyBattling", "System.Boolean");
  if (!valid) {
    for (const auto& [type, name] : std::array{
             std::pair{manager, "Prime.ActionQueue.ActionQueueManager"},
             std::pair{queueClass, "Prime.ActionQueue.ActionQueueInstance"},
             std::pair{actionClass, "Prime.ActionQueue.QueueableAction"},
             std::pair{playerClass, "Digit.PrimeServer.Models.FleetPlayerData"},
             std::pair{deployedClass, "Digit.PrimeServer.Models.FleetDeployedData"}})
      if (!type) spdlog::warn("[ThinQueueProtection] unavailable: missing class {}", name);
    const std::array methods{
        std::pair{plan_info, "ActionQueueManager.DoPlanPathAndEngageTarget(FleetPlayerData) -> Boolean"},
        std::pair{stall_info, "ActionQueueManager.HandleStall(ActionQueueInstance, FleetPlayerData, FleetDeployedData) -> Void"},
        std::pair{disposed_info, "ActionQueueManager.OnFleetsDisposedEventHandler(List<FleetDeployedData>) -> Void"},
        std::pair{engage, "ActionQueueManager.TryPlanPathAndEngageTarget(FleetPlayerData, ActionQueueInstance) -> EngageResult"},
        std::pair{process_info, "ActionQueueManager.ProcessQueue(Int64, Boolean) -> Void"}};
    for (const auto& [method, name] : methods)
      if (!method || !method->methodPointer || method->has_full_generic_sharing_signature)
        spdlog::warn("[ThinQueueProtection] unavailable: {}: {}", name,
                       method && method->has_full_generic_sharing_signature ? "unsupported generic sharing"
                                                                           : "instance method unavailable");
    if (!underlying || underlying->type != IL2CPP_TYPE_I4)
      spdlog::warn("[ThinQueueProtection] unavailable: EngageResult must be an Int32 enum");
    // Only these three methods are detoured; the other two are native calls.
    for (size_t i = 0; i < 3; ++i)
      for (size_t j = 0; j < i; ++j)
        if (methods[i].first && methods[j].first && methods[i].first->methodPointer
            && methods[i].first->methodPointer == methods[j].first->methodPointer)
          spdlog::warn("[ThinQueueProtection] unavailable: {} and {} share a native hook target",
                         methods[i].second, methods[j].second);
    return;
  }
  const bool a = SPUD_STATIC_DETOUR(plan, Plan) != nullptr;
  const bool b = SPUD_STATIC_DETOUR(stall, Stall) != nullptr;
  const bool c = SPUD_STATIC_DETOUR(disposed, Disposed) != nullptr;
  for (const auto& [installed, name] : std::array{
           std::pair{a, "ActionQueueManager.DoPlanPathAndEngageTarget"},
           std::pair{b, "ActionQueueManager.HandleStall"}, std::pair{c, "ActionQueueManager.OnFleetsDisposedEventHandler"}})
    if (!installed) spdlog::warn("[ThinQueueProtection] hook installation failed: {}; feature unavailable", name);
  ready.store(a && b && c);
  spdlog::info("[ThinQueueProtection] ready={}", ready.load());
}

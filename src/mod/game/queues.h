#pragma once
// Checked queue metadata and storage access. Unknown layouts retain native behavior.
#include "features/action_queue.h"
#include "features/action_queue_guard_policy.h"
#include "features/queue_address_guard_policy.h"
#include <array>
#include <cstring>
#include <il2cpp/il2cpp.h>
#include <il2cpp/method_contract.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace queue_bindings::recovery
{
using Object = Il2CppObject;
using Id     = std::int64_t;
using action_queue::QueueState;
inline Il2CppClass *queueClass{}, *actionClass{}, *int64Class{};

struct WeakQueues {
  using Object = Il2CppObject*;
  using Handle = Il2CppGCHandle;
  static Handle New(Object value)
  { return il2cpp_gchandle_new_weakref(value, false); }
  static Object Get(Handle handle)
  { return il2cpp_gchandle_get_target(handle); }
  static void Free(Handle handle)
  { il2cpp_gchandle_free(handle); }
};

template <typename T> T Read(const void* object, std::size_t offset)
{
  T value{};
  if (object)
    std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof(value));
  return value;
}
// Inspect actual List<QueueableAction> storage without invoking game properties or enumerators.
// Unknown storage/layout is ineligible, never equivalent to an empty queue or absent target.
inline QueueState Inspect(Il2CppObject* queue, std::int64_t target = 0)
{
  QueueState s;
  if (!queue || il2cpp_object_get_class(queue) != queueClass)
    return s;
  s.fleet    = Read<std::int64_t>(queue, 0x30);
  s.attempt  = Read<float>(queue, 0x14);
  s.engaging = Read<bool>(queue, 0x10);
  auto* list = Read<Il2CppObject*>(queue, 0x28);
  if (!list)
    return s;
  auto* cls     = il2cpp_object_get_class(list);
  auto* size    = il2cpp_class_get_field_from_name(cls, "_size");
  auto* storage = il2cpp_class_get_field_from_name(cls, "_items");
  if (!size || !storage || !size->type || !storage->type || size->type->type != IL2CPP_TYPE_I4
      || storage->type->type != IL2CPP_TYPE_SZARRAY || size->type->byref || storage->type->byref
      || (il2cpp_field_get_flags(size) & FIELD_ATTRIBUTE_STATIC)
      || (il2cpp_field_get_flags(storage) & FIELD_ATTRIBUTE_STATIC))
    return s;
  Il2CppArray* items{};
  il2cpp_field_get_value(list, size, &s.count);
  il2cpp_field_get_value(list, storage, &items);
  if (s.count < 0 || s.count > 128 || !items || il2cpp_array_length(items) < static_cast<unsigned>(s.count)
      || il2cpp_class_get_element_class(il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(items))) != actionClass)
    return s;
  auto*      array        = reinterpret_cast<Il2CppArraySize*>(items);
  const auto inspectCount = target ? s.count : (s.count > 0 ? 1 : 0);
  for (int i = 0; i < inspectCount; ++i) {
    auto* action = static_cast<Il2CppObject*>(array->vector[i]);
    if (!action || il2cpp_object_get_class(action) != actionClass)
      return s;
    const auto id = Read<std::int64_t>(action, 0x10);
    if (i == 0)
      s.front = id;
    if (target && id == target)
      s.containsTarget = true;
  }
  s.valid = s.fleet != 0;
  return s;
}
inline Il2CppArraySize* Queues(Il2CppObject* manager)
{
  auto* array = Read<Il2CppArray*>(manager, 0x48);
  if (!array || il2cpp_array_length(array) > 64
      || il2cpp_class_get_element_class(il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(array))) != queueClass)
    return nullptr;
  return reinterpret_cast<Il2CppArraySize*>(array);
}
inline Il2CppObject* FindQueue(Il2CppObject* manager, std::int64_t fleet)
{
  auto* array = Queues(manager);
  if (!array)
    return nullptr;
  for (unsigned i = 0; i < array->max_length; ++i) {
    auto* queue = static_cast<Il2CppObject*>(array->vector[i]);
    if (queue && il2cpp_object_get_class(queue) == queueClass && Read<std::int64_t>(queue, 0x30) == fleet)
      return queue;
  }
  return nullptr;
}
inline bool AbsentFromAllQueues(Il2CppObject* manager, std::int64_t target)
{
  auto* array = Queues(manager);
  if (!array)
    return false;
  for (unsigned i = 0; i < array->max_length; ++i) {
    auto* queue = static_cast<Il2CppObject*>(array->vector[i]);
    if (!queue)
      continue;
    const auto s = Inspect(queue, target);
    if (!s.valid || s.containsTarget)
      return false;
  }
  return true;
}

inline bool Field(Il2CppClass* cls, const char* name, std::ptrdiff_t offset, Il2CppTypeEnum type)
{
  auto*      field = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  const bool valid = field && field->offset == offset && field->type && !field->type->byref
                     && !(il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC) && field->type->type == type;
  if (!valid)
    spdlog::warn("[FasterQueueRecovery] {}.{}: expected instance field offset=0x{:X} type={}; "
                 "actual offset={} type={} static={} byref={}",
                 cls ? cls->name : "<missing class>", name, offset, static_cast<int>(type), field ? field->offset : -1,
                 field && field->type ? static_cast<int>(field->type->type) : -1,
                 field && field->type && bool(il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC),
                 field && field->type && bool(field->type->byref));
  return valid;
}

struct CourseResponse {
  std::int64_t  fleet;
  bool          success, recall;
  unsigned char padding[6];
  Il2CppObject* target;
};
static_assert(sizeof(CourseResponse) == 24 && offsetof(CourseResponse, target) == 16);

inline void* engage{};
inline void* retry{};
inline void* course{};
inline void* clear{};
inline bool  ResolveBindings()
{
  auto* cls   = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager">::Raw();
  queueClass  = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueInstance">::Raw();
  actionClass = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "QueueableAction">::Raw();
  int64Class  = il2cpp::Class<"mscorlib", "System", "Int64">::Raw();
  if (!cls || !queueClass || !actionClass || !int64Class) {
    for (const auto& [type, name] :
         std::array{std::pair{cls, "Prime.ActionQueue.ActionQueueManager"},
                    std::pair{queueClass, "Prime.ActionQueue.ActionQueueInstance"},
                    std::pair{actionClass, "Prime.ActionQueue.QueueableAction"}, std::pair{int64Class, "System.Int64"}})
      if (!type)
        spdlog::warn("[FasterQueueRecovery] unavailable: missing class {}", name);
    return false;
  }
  using il2cpp::contract::Pointer;
  using il2cpp::contract::Resolve;
  const auto* engage_info =
      Resolve(cls, "TryPlanPathAndEngageTarget", false, "Digit.Prime.Combat.EngageResult",
              {"Digit.PrimeServer.Models.FleetPlayerData", "Prime.ActionQueue.ActionQueueInstance"});
  engage                   = Pointer(engage_info);
  const auto* retry_info   = Resolve(cls, "ShouldRetryFailedSetCourse", false, "System.Boolean",
                                     {"System.Int64", "Prime.ActionQueue.ActionQueueInstance"});
  retry                    = Pointer(retry_info);
  const auto* clear_info   = Resolve(cls, "StopWatchdogAndClearAllQueues", false, "System.Void", {});
  clear                    = Pointer(clear_info);
  const auto* info         = Resolve(cls, "OnSetCourseResponseEventHandler", false, "System.Void",
                                     {"Digit.PrimeServer.Events.SetCourseResponseEventArgs"});
  course                   = Pointer(info);
  auto*         event      = info ? il2cpp_class_from_type(info->parameters[0]) : nullptr;
  auto*         result     = engage_info ? il2cpp_class_from_type(engage_info->return_type) : nullptr;
  const auto*   underlying = result && il2cpp_class_is_enum(result) ? il2cpp_class_enum_basetype(result) : nullptr;
  std::uint32_t alignment{};
  const bool    valid =
      underlying && underlying->type == IL2CPP_TYPE_I4 && event && info->parameters[0]->type == IL2CPP_TYPE_VALUETYPE
      && il2cpp_class_value_size(event, &alignment) == sizeof(CourseResponse)
      && Field(event, "<FleetId>k__BackingField", 0x10, IL2CPP_TYPE_I8)
      && Field(event, "<Success>k__BackingField", 0x18, IL2CPP_TYPE_BOOLEAN)
      && Field(event, "<IsRecall>k__BackingField", 0x19, IL2CPP_TYPE_BOOLEAN)
      && Field(event, "<TargetId>k__BackingField", 0x20, IL2CPP_TYPE_OBJECT)
      && Field(cls, "_battleQueue", 0x48, IL2CPP_TYPE_SZARRAY)
      && Field(queueClass, "IsEngaging", 0x10, IL2CPP_TYPE_BOOLEAN)
      && Field(queueClass, "LastEngageAttemptTime", 0x14, IL2CPP_TYPE_R4)
      && Field(queueClass, "<PlayerFleetId>k__BackingField", 0x30, IL2CPP_TYPE_I8)
      && Field(queueClass, "_actionQueue", 0x28, IL2CPP_TYPE_GENERICINST)
      && Field(actionClass, "<FleetId>k__BackingField", 0x10, IL2CPP_TYPE_I8) && engage && retry && course && clear
      && !engage_info->has_full_generic_sharing_signature && !retry_info->has_full_generic_sharing_signature
      && !info->has_full_generic_sharing_signature && !clear_info->has_full_generic_sharing_signature && engage != retry
      && engage != course && engage != clear && retry != course && retry != clear && course != clear;
  if (!valid) {
    const std::array methods{
        std::pair{
            engage_info,
            "ActionQueueManager.TryPlanPathAndEngageTarget(FleetPlayerData, ActionQueueInstance) -> EngageResult"},
        std::pair{retry_info, "ActionQueueManager.ShouldRetryFailedSetCourse(Int64, ActionQueueInstance) -> Boolean"},
        std::pair{info, "ActionQueueManager.OnSetCourseResponseEventHandler(SetCourseResponseEventArgs) -> Void"},
        std::pair{clear_info, "ActionQueueManager.StopWatchdogAndClearAllQueues() -> Void"}};
    for (const auto& [method, name] : methods)
      if (!method || !method->methodPointer || method->has_full_generic_sharing_signature)
        spdlog::warn("[FasterQueueRecovery] unavailable: {}: {}", name,
                     method && method->has_full_generic_sharing_signature ? "unsupported generic sharing"
                                                                          : "instance method unavailable");
    if (!underlying || underlying->type != IL2CPP_TYPE_I4)
      spdlog::warn("[FasterQueueRecovery] unavailable: EngageResult must be an Int32 enum");
    const auto event_size =
        event && info->parameters[0]->type == IL2CPP_TYPE_VALUETYPE ? il2cpp_class_value_size(event, &alignment) : -1;
    if (event_size != sizeof(CourseResponse))
      spdlog::warn(
          "[FasterQueueRecovery] unavailable: SetCourseResponseEventArgs expected value type size={}; actual={}",
          sizeof(CourseResponse), event_size);
    for (size_t i = 0; i < methods.size(); ++i)
      for (size_t j = 0; j < i; ++j)
        if (methods[i].first && methods[j].first && methods[i].first->methodPointer
            && methods[i].first->methodPointer == methods[j].first->methodPointer)
          spdlog::warn("[FasterQueueRecovery] unavailable: {} and {} share a native hook target", methods[i].second,
                       methods[j].second);
    return false;
  }
  return true;
}

inline bool ReadCourseTarget(Il2CppObject* value, int64_t& target)
{
  if (!value || il2cpp_object_get_class(value) != int64Class)
    return false;
  std::memcpy(&target, il2cpp_object_unbox(value), sizeof(target));
  return true;
}

} // namespace queue_bindings::recovery
namespace queue_bindings::thin
{
using Object = Il2CppObject;
using Id     = std::int64_t;
using action_queue_guard::QueueState;
inline Il2CppClass *queueClass{}, *actionClass{}, *playerClass{}, *deployedClass{};

inline Id (*playerId)(Object*);

inline Id (*deployedId)(Object*);

inline int (*playerState)(Object*);

inline int (*deployedState)(Object*);

inline int (*removalReason)(Object*);

inline bool (*destroyed)(Object*);

inline bool (*battling)(Object*);

inline int (*tryEngage)(Object*, Object*, Object*);

inline void (*processTarget)(Object*, Id, bool);

inline bool Is(Object* object, Il2CppClass* cls)
{ return object && il2cpp_object_get_class(object) == cls; }

template <typename T> T Read(const void* object, std::size_t offset)
{
  T value{};
  if (object)
    std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof(value));
  return value;
}

// Inspect storage without invoking List getters/enumerators. Unknown layouts fail closed.
inline Il2CppArraySize* List(Object* list, Il2CppClass* element, int limit, int& count)
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

inline QueueState Snapshot(Object* queue)
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

inline Object* FindQueue(Object* manager, Id fleet, Id head = 0)
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

inline bool Field(Il2CppClass* cls, const char* name, std::ptrdiff_t offset, Il2CppTypeEnum type)
{
  auto*      f     = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  const bool valid = f && f->offset == offset && f->type && !f->type->byref
                     && !(il2cpp_field_get_flags(f) & FIELD_ATTRIBUTE_STATIC) && f->type->type == type;
  if (!valid)
    spdlog::warn("[ThinQueueProtection] {}.{}: expected instance field offset=0x{:X} type={}; "
                 "actual offset={} type={} static={} byref={}",
                 cls ? cls->name : "<missing class>", name, offset, static_cast<int>(type), f ? f->offset : -1,
                 f && f->type ? static_cast<int>(f->type->type) : -1,
                 f && f->type && bool(il2cpp_field_get_flags(f) & FIELD_ATTRIBUTE_STATIC),
                 f && f->type && bool(f->type->byref));
  return valid;
}

template <typename T> bool Getter(T& out, Il2CppClass* cls, const char* name, const char* result)
{
  const auto* method = il2cpp::contract::Resolve(cls, name, false, result, {});
  out = method && !method->has_full_generic_sharing_signature ? reinterpret_cast<T>(il2cpp::contract::Pointer(method))
                                                              : nullptr;
  if (method && method->return_type->type == IL2CPP_TYPE_VALUETYPE) {
    auto*       type       = il2cpp_class_from_type(method->return_type);
    const auto* underlying = type && il2cpp_class_is_enum(type) ? il2cpp_class_enum_basetype(type) : nullptr;
    if (!underlying || underlying->type != IL2CPP_TYPE_I4)
      out = nullptr;
  }
  if (!out)
    spdlog::warn("[ThinQueueProtection] {}.{}: expected instance {}(); {}", cls ? cls->name : "<missing class>", name,
                 result,
                 !method                                      ? "method unavailable"
                 : method->has_full_generic_sharing_signature ? "unsupported generic sharing"
                 : !method->methodPointer                     ? "native pointer unavailable"
                                                              : "expected Int32 enum result");
  return out != nullptr;
}

inline void* plan{};
inline void* stall{};
inline void* disposed{};
inline bool  ResolveBindings()
{
  auto* manager = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager">::Raw();
  queueClass    = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueInstance">::Raw();
  actionClass   = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "QueueableAction">::Raw();
  playerClass   = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetPlayerData">::Raw();
  deployedClass =
      il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData">::Raw();
  using il2cpp::contract::Pointer;
  using il2cpp::contract::Resolve;
  const auto* plan_info  = Resolve(manager, "DoPlanPathAndEngageTarget", false, "System.Boolean",
                                   {"Digit.PrimeServer.Models.FleetPlayerData"});
  plan                   = Pointer(plan_info);
  const auto* stall_info = Resolve(manager, "HandleStall", false, "System.Void",
                                   {"Prime.ActionQueue.ActionQueueInstance", "Digit.PrimeServer.Models.FleetPlayerData",
                                    "Digit.PrimeServer.Models.FleetDeployedData"});
  stall                  = Pointer(stall_info);
  const auto* disposed_info = Resolve(manager, "OnFleetsDisposedEventHandler", false, "System.Void",
                                      {"System.Collections.Generic.List<Digit.PrimeServer.Models.FleetDeployedData>"});
  disposed                  = Pointer(disposed_info);
  const auto* engage = Resolve(manager, "TryPlanPathAndEngageTarget", false, "Digit.Prime.Combat.EngageResult",
                               {"Digit.PrimeServer.Models.FleetPlayerData", "Prime.ActionQueue.ActionQueueInstance"});
  tryEngage          = reinterpret_cast<decltype(tryEngage)>(Pointer(engage));
  const auto* process_info = Resolve(manager, "ProcessQueue", false, "System.Void", {"System.Int64", "System.Boolean"});
  processTarget            = reinterpret_cast<decltype(processTarget)>(Pointer(process_info));
  auto*       result       = engage ? il2cpp_class_from_type(engage->return_type) : nullptr;
  const auto* underlying   = result && il2cpp_class_is_enum(result) ? il2cpp_class_enum_basetype(result) : nullptr;
  const bool  valid =
      manager && queueClass && actionClass && playerClass && deployedClass && plan && stall && disposed && tryEngage
      && processTarget && underlying && underlying->type == IL2CPP_TYPE_I4
      && !plan_info->has_full_generic_sharing_signature && !stall_info->has_full_generic_sharing_signature
      && !disposed_info->has_full_generic_sharing_signature && !engage->has_full_generic_sharing_signature
      && !process_info->has_full_generic_sharing_signature && plan != stall && plan != disposed && stall != disposed
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
    for (const auto& [type, name] : std::array{std::pair{manager, "Prime.ActionQueue.ActionQueueManager"},
                                               std::pair{queueClass, "Prime.ActionQueue.ActionQueueInstance"},
                                               std::pair{actionClass, "Prime.ActionQueue.QueueableAction"},
                                               std::pair{playerClass, "Digit.PrimeServer.Models.FleetPlayerData"},
                                               std::pair{deployedClass, "Digit.PrimeServer.Models.FleetDeployedData"}})
      if (!type)
        spdlog::warn("[ThinQueueProtection] unavailable: missing class {}", name);
    const std::array methods{
        std::pair{plan_info, "ActionQueueManager.DoPlanPathAndEngageTarget(FleetPlayerData) -> Boolean"},
        std::pair{stall_info,
                  "ActionQueueManager.HandleStall(ActionQueueInstance, FleetPlayerData, FleetDeployedData) -> Void"},
        std::pair{disposed_info, "ActionQueueManager.OnFleetsDisposedEventHandler(List<FleetDeployedData>) -> Void"},
        std::pair{
            engage,
            "ActionQueueManager.TryPlanPathAndEngageTarget(FleetPlayerData, ActionQueueInstance) -> EngageResult"},
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
          spdlog::warn("[ThinQueueProtection] unavailable: {} and {} share a native hook target", methods[i].second,
                       methods[j].second);
    return false;
  }
  return true;
}

} // namespace queue_bindings::thin
namespace queue_bindings::address
{
using Object = Il2CppObject;
using Id     = std::int64_t;
inline Il2CppClass *managerClass{}, *queueClass{}, *actionClass{}, *playerClass{}, *deployedClass{};

inline FieldInfo *queuesField{}, *fleetField{}, *actionsField{}, *targetField{}, *idField{}, *indexField{};

inline bool Is(Object* object, Il2CppClass* cls)
{ return object && il2cpp_object_get_class(object) == cls; }

inline FieldInfo* Field(Il2CppClass* cls, const char* name, Il2CppTypeEnum type)
{
  auto* field = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;

  return field && field->type && !(field->type->attrs & FIELD_ATTRIBUTE_STATIC) && field->offset >= sizeof(Object)
                 && field->type->type == type
             ? field
             : nullptr;
}
template <typename T> T Read(Object* object, FieldInfo* field)
{
  T value{};
  if (object && field)
    il2cpp_field_get_value(object, field, &value);
  return value;
}
inline Object* Reference(Object* object, const char* name)
{
  auto* cls   = object ? il2cpp_object_get_class(object) : nullptr;
  auto* field = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  auto* type  = field && field->type ? il2cpp_class_from_type(field->type) : nullptr;
  return type && !il2cpp_class_is_valuetype(type) && !(field->type->attrs & FIELD_ATTRIBUTE_STATIC)
                 && field->offset >= sizeof(Object)
             ? Read<Object*>(object, field)
             : nullptr;
}
inline int Enum(Object* object, const char* name)
{
  auto* cls   = object ? il2cpp_object_get_class(object) : nullptr;
  auto* field = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  auto* type  = field && field->type ? il2cpp_class_from_type(field->type) : nullptr;
  auto* base  = type && il2cpp_class_is_enum(type) ? il2cpp_class_enum_basetype(type) : nullptr;
  return base && base->type == IL2CPP_TYPE_I4 && !(field->type->attrs & FIELD_ATTRIBUTE_STATIC)
                 && field->offset >= sizeof(Object)
             ? Read<int>(object, field)
             : -1;
}
inline int State(Object* object, const char* name)
{
  auto* cls       = object ? il2cpp_object_get_class(object) : nullptr;
  auto* field     = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  auto* container = field && field->type ? il2cpp_class_from_type(field->type) : nullptr;
  if (!container || !il2cpp_class_is_valuetype(container) || field->offset < sizeof(Object)
      || (field->type->attrs & FIELD_ATTRIBUTE_STATIC))
    return -1;
  uint32_t  alignment{};
  const int size   = il2cpp_class_value_size(container, &alignment);
  auto*     inner  = il2cpp_class_get_field_from_name(container, "_currentState");
  auto*     type   = inner && inner->type ? il2cpp_class_from_type(inner->type) : nullptr;
  auto*     base   = type && il2cpp_class_is_enum(type) ? il2cpp_class_enum_basetype(type) : nullptr;
  const int offset = inner ? inner->offset - static_cast<int>(sizeof(Object)) : -1;
  if (!base || base->type != IL2CPP_TYPE_I4 || (inner->type->attrs & FIELD_ATTRIBUTE_STATIC) || offset < 0
      || offset + sizeof(int) > size)
    return -1;
  int value{};
  std::memcpy(&value, reinterpret_cast<const char*>(object) + field->offset + offset, sizeof(value));
  return value;
}
inline queue_address_guard::Address Address(Object* object)
{
  auto* cls      = object ? il2cpp_object_get_class(object) : nullptr;
  auto* galaxy   = Field(cls, "galaxy_", IL2CPP_TYPE_I8);
  auto* system   = Field(cls, "system_", IL2CPP_TYPE_I8);
  auto* planet   = Field(cls, "planet_", IL2CPP_TYPE_I8);
  auto* instance = Field(cls, "instance_", IL2CPP_TYPE_I4);
  if (!galaxy || !system || !planet || !instance)
    return {};
  queue_address_guard::Address value{true, Read<Id>(object, galaxy), Read<Id>(object, system), Read<Id>(object, planet),
                                     Read<int>(object, instance)};
  value.valid = value.galaxy >= 0 && value.system > 0 && value.planet >= 0 && value.instance >= 0;
  return value;
}

// Existing queue snapshots use fixed Windows offsets. This guard also runs on macOS
// and resolves metadata instead; unknown or incomplete storage retains native behavior.
inline bool OwnsNonemptyQueue(Object* manager, Object* player, Id fleet)
{
  auto*     array = Read<Il2CppArray*>(manager, queuesField);
  const int index = Read<int>(player, indexField);
  if (!array || !fleet || index < 0 || il2cpp_array_length(array) > 64
      || static_cast<unsigned>(index) >= il2cpp_array_length(array)
      || il2cpp_class_get_element_class(il2cpp_object_get_class(reinterpret_cast<Object*>(array))) != queueClass)
    return false;
  auto* queue = static_cast<Object*>(reinterpret_cast<Il2CppArraySize*>(array)->vector[index]);
  if (!Is(queue, queueClass) || Read<Id>(queue, fleetField) != fleet)
    return false;
  auto* list    = Read<Object*>(queue, actionsField);
  auto* cls     = list ? il2cpp_object_get_class(list) : nullptr;
  auto* size    = Field(cls, "_size", IL2CPP_TYPE_I4);
  auto* storage = Field(cls, "_items", IL2CPP_TYPE_SZARRAY);
  if (!size || !storage)
    return false;
  const int count = Read<int>(list, size);
  auto*     items = Read<Il2CppArray*>(list, storage);
  if (count <= 0 || count > 128 || !items || il2cpp_array_length(items) < static_cast<unsigned>(count)
      || il2cpp_class_get_element_class(il2cpp_object_get_class(reinterpret_cast<Object*>(items))) != actionClass)
    return false;
  for (int i = 0; i < count; ++i) {
    auto* action = static_cast<Object*>(reinterpret_cast<Il2CppArraySize*>(items)->vector[i]);
    if (!Is(action, actionClass) || !Read<Id>(action, targetField))
      return false;
  }
  return true;
}

inline std::array<const MethodInfo*, 4> methods{};
inline bool                             ResolveBindings()
{
  managerClass = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager">::Raw();
  queueClass   = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueInstance">::Raw();
  actionClass  = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "QueueableAction">::Raw();
  playerClass  = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetPlayerData">::Raw();
  deployedClass =
      il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData">::Raw();
  auto* serviceClass =
      il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "DeploymentService">::Raw();
  queuesField  = Field(managerClass, "_battleQueue", IL2CPP_TYPE_SZARRAY);
  fleetField   = Field(queueClass, "<PlayerFleetId>k__BackingField", IL2CPP_TYPE_I8);
  actionsField = Field(queueClass, "_actionQueue", IL2CPP_TYPE_GENERICINST);
  targetField  = Field(actionClass, "<FleetId>k__BackingField", IL2CPP_TYPE_I8);
  idField      = Field(playerClass, "<ID>k__BackingField", IL2CPP_TYPE_I8);
  indexField   = Field(playerClass, "<Index>k__BackingField", IL2CPP_TYPE_I4);
  using il2cpp::contract::Pointer;
  using il2cpp::contract::Resolve;
  methods = {
      Resolve(managerClass, "OnPlayerFleetStateChangedEventHandler", false, "System.Void",
              {"System.Collections.Generic.List<Digit.PrimeServer.Models.FleetPlayerData>"}),
      Resolve(managerClass, "DoesFleetQueueContainMismatchedAddressTargets", false, "System.Boolean",
              {"Digit.PrimeServer.Models.FleetPlayerData"}),
      Resolve(playerClass, "get_Address", false, "Digit.PrimeServer.Models.NodeAddress", {}),
      Resolve(serviceClass, "GetDeployedFleet", false, "Digit.PrimeServer.Models.FleetDeployedData", {"System.Int64"})};
  bool valid = managerClass && queueClass && actionClass && playerClass && deployedClass && serviceClass && queuesField
               && fleetField && actionsField && targetField && idField && indexField;
  for (unsigned i = 0; i < methods.size(); ++i) {
    valid = valid && methods[i] && !methods[i]->has_full_generic_sharing_signature;
    for (unsigned j = 0; j < i; ++j)
      valid = valid && Pointer(methods[i]) != Pointer(methods[j]);
  }
  if (!valid) {
    spdlog::warn("[QueueAddressGuard] native method/field contract unavailable; retaining native behavior");
    return false;
  }
  return true;
}

inline bool IsLocalDeployment(Object* own, Id fleet)
{
  auto* cls         = own ? il2cpp_object_get_class(own) : nullptr;
  auto* local       = Field(cls, "_localPlayerFleet", IL2CPP_TYPE_BOOLEAN);
  auto* model       = Reference(own, "_deploymentFleet");
  auto* model_class = model ? il2cpp_object_get_class(model) : nullptr;
  auto* id          = Field(model_class, "fleetId_", IL2CPP_TYPE_I8);
  if (!id)
    id = Field(model_class, "<FleetId>k__BackingField", IL2CPP_TYPE_I8);
  return local && Read<bool>(own, local) && id && Read<Id>(model, id) == fleet;
}
inline bool PlanningRecall(Object* own)
{
  auto* field = Field(own ? il2cpp_object_get_class(own) : nullptr, "<IsPlanningRecallCourse>k__BackingField",
                      IL2CPP_TYPE_BOOLEAN);
  return !field || Read<bool>(own, field);
}

} // namespace queue_bindings::address

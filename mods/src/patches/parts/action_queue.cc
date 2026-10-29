#include "action_queue.h"
#include <config.h>

// Use the host ABI for the 24-byte value-type callback on Windows and both Mac architectures.
// Validate the managed layout before installing.
#include <atomic>
#include <array>
#include <utility>
#include <cstring>
#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace
{
using action_queue::Clock;
using action_queue::QueueState;
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
action_queue::Requests<WeakQueues> requests;
std::mutex                         requestsMutex;
std::atomic_bool                   ready{false};
Il2CppClass *                      queueClass{}, *actionClass{}, *int64Class{};

bool Enabled()
{ return ready.load() && Config::Get().kirshara_fixes && Config::Get().queue_enabled; }
void ClearRequests()
{
  std::lock_guard lock(requestsMutex);
  requests.Clear();
}
template <typename T> T Read(const void* object, std::size_t offset)
{
  T value{};
  if (object)
    std::memcpy(&value, static_cast<const char*>(object) + offset, sizeof(value));
  return value;
}
// Inspect actual List<QueueableAction> storage without invoking game properties or enumerators.
// Unknown storage/layout is ineligible, never equivalent to an empty queue or absent target.
QueueState Inspect(Il2CppObject* queue, std::int64_t target = 0)
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
Il2CppArraySize* Queues(Il2CppObject* manager)
{
  auto* array = Read<Il2CppArray*>(manager, 0x48);
  if (!array || il2cpp_array_length(array) > 64
      || il2cpp_class_get_element_class(il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(array))) != queueClass)
    return nullptr;
  return reinterpret_cast<Il2CppArraySize*>(array);
}
Il2CppObject* FindQueue(Il2CppObject* manager, std::int64_t fleet)
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
bool AbsentFromAllQueues(Il2CppObject* manager, std::int64_t target)
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
struct CourseContext {
  std::int64_t  fleet{}, target{};
  Il2CppObject* queue{}; // Borrowed only inside the synchronous native Course call.
  bool          outstandingAtEntry{};
};
thread_local CourseContext* currentCourse{};

int Engage(auto original, Il2CppObject* manager, Il2CppObject* player, Il2CppObject* queue)
{
  std::uint64_t serial{};
  if (Enabled()) {
    try {
      const auto      s = Inspect(queue);
      std::lock_guard lock(requestsMutex);
      serial = requests.Remember(queue, s, Clock::now());
    } catch (...) {
      ClearRequests();
    }
  } else {
    ClearRequests();
  }
  const auto result = original(manager, player, queue);
  // Result zero means native dispatch succeeded. Do not cancel a newer reentrant request.
  if (serial && result != 0) {
    std::lock_guard lock(requestsMutex);
    requests.Cancel(serial);
  }
  return result;
}
bool Retry(auto original, Il2CppObject* manager, std::int64_t target, Il2CppObject* queue)
{
  if (!Enabled())
    ClearRequests();
  const bool retry = original(manager, target, queue);
  if (retry || !Enabled() || !currentCourse)
    return retry;
  try {
    const auto& c = *currentCourse;
    const auto  s = Inspect(queue, target);
    if (c.target != target
        || !action_queue::CanAdvance(s, c.fleet, target, c.outstandingAtEntry, c.queue == queue, true))
      return retry;
    // The native false branch processes this target across all fleets. Never suppress that work
    // while any other queue still contains it or has contents we cannot verify.
    if (!AbsentFromAllQueues(manager, target))
      return retry;
    std::lock_guard lock(requestsMutex);
    if (!requests.Consume(queue, s, target, Clock::now()))
      return retry;
    // The enclosing Course handler's true branch calls its normal planner for this same fleet.
    // That planner retains all eligibility checks. No flags, targets or retry counters are changed here.
    return true;
  } catch (...) {
    return retry;
  }
}
struct CourseResponse {
  std::int64_t  fleet;
  bool          success, recall;
  unsigned char padding[6];
  Il2CppObject* target;
};
static_assert(sizeof(CourseResponse) == 24 && offsetof(CourseResponse, target) == 16);
void Course(auto original, Il2CppObject* manager, CourseResponse args)
{
  if (!Enabled())
    ClearRequests();
  CourseContext context{args.fleet};
  if (Enabled() && !args.success && !args.recall) {
    try {
      if (args.target && il2cpp_object_get_class(args.target) == int64Class) {
        std::memcpy(&context.target, il2cpp_object_unbox(args.target), sizeof(context.target));
        context.queue              = FindQueue(manager, args.fleet);
        context.outstandingAtEntry = context.queue && Read<bool>(context.queue, 0x10);
      }
    } catch (...) {
      context = {};
    }
  }
  struct Scope {
    CourseContext* previous{currentCourse};
    explicit Scope(CourseContext* value)
    { currentCourse = value; }
    ~Scope()
    { currentCourse = previous; }
  } scope(&context);
  original(manager, args);
}
void ClearAll(auto original, Il2CppObject* manager)
{
  // Native session end/invalidation/quit share this seam; release weak handles while IL2CPP is alive.
  ClearRequests();
  original(manager);
}
bool Field(Il2CppClass* cls, const char* name, std::ptrdiff_t offset, Il2CppTypeEnum type)
{
  auto* field = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  const bool valid = field && field->offset == offset && field->type && !field->type->byref
                     && !(il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC) && field->type->type == type;
  if (!valid)
    spdlog::warn("[FasterQueueRecovery] {}.{}: expected instance field offset=0x{:X} type={}; "
                   "actual offset={} type={} static={} byref={}",
                   cls ? cls->name : "<missing class>", name, offset, static_cast<int>(type),
                   field ? field->offset : -1, field && field->type ? static_cast<int>(field->type->type) : -1,
                   field && field->type && bool(il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC),
                   field && field->type && bool(field->type->byref));
  return valid;
}
} // namespace

void ResetQueueRecoveryTracking()
{ ClearRequests(); }

void InstallActionQueueRecovery()
{
  auto* cls   = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager").get_cls();
  queueClass  = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "ActionQueueInstance").get_cls();
  actionClass = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "QueueableAction").get_cls();
  int64Class  = il2cpp_get_class_helper("mscorlib", "System", "Int64").get_cls();
  if (!cls || !queueClass || !actionClass || !int64Class) {
    for (const auto& [type, name] : std::array{
             std::pair{cls, "Prime.ActionQueue.ActionQueueManager"},
             std::pair{queueClass, "Prime.ActionQueue.ActionQueueInstance"},
             std::pair{actionClass, "Prime.ActionQueue.QueueableAction"}, std::pair{int64Class, "System.Int64"}})
      if (!type) spdlog::warn("[FasterQueueRecovery] unavailable: missing class {}", name);
    return;
  }
  using method_contract::Resolve;
  using method_contract::Pointer;
  const auto* engage_info = Resolve(cls, "TryPlanPathAndEngageTarget", false, "Digit.Prime.Combat.EngageResult",
      {"Digit.PrimeServer.Models.FleetPlayerData", "Prime.ActionQueue.ActionQueueInstance"});
  auto* engage = Pointer(engage_info);
  const auto* retry_info = Resolve(cls, "ShouldRetryFailedSetCourse", false, "System.Boolean",
      {"System.Int64", "Prime.ActionQueue.ActionQueueInstance"});
  auto* retry = Pointer(retry_info);
  const auto* clear_info = Resolve(cls, "StopWatchdogAndClearAllQueues", false, "System.Void", {});
  auto* clear = Pointer(clear_info);
  const auto* info = Resolve(cls, "OnSetCourseResponseEventHandler", false, "System.Void",
      {"Digit.PrimeServer.Events.SetCourseResponseEventArgs"});
  auto* course = Pointer(info);
  auto* event = info ? il2cpp_class_from_type(info->parameters[0]) : nullptr;
  auto* result = engage_info ? il2cpp_class_from_type(engage_info->return_type) : nullptr;
  const auto* underlying = result && il2cpp_class_is_enum(result) ? il2cpp_class_enum_basetype(result) : nullptr;
  std::uint32_t alignment{};
  const bool    valid =
      underlying && underlying->type == IL2CPP_TYPE_I4
      && event && info->parameters[0]->type == IL2CPP_TYPE_VALUETYPE
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
      && Field(actionClass, "<FleetId>k__BackingField", 0x10, IL2CPP_TYPE_I8)
      && engage && retry && course && clear
      && !engage_info->has_full_generic_sharing_signature && !retry_info->has_full_generic_sharing_signature
      && !info->has_full_generic_sharing_signature && !clear_info->has_full_generic_sharing_signature
      && engage != retry && engage != course && engage != clear
      && retry != course && retry != clear && course != clear;
  if (!valid) {
    const std::array methods{
        std::pair{engage_info, "ActionQueueManager.TryPlanPathAndEngageTarget(FleetPlayerData, ActionQueueInstance) -> EngageResult"},
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
    const auto event_size = event && info->parameters[0]->type == IL2CPP_TYPE_VALUETYPE
                                ? il2cpp_class_value_size(event, &alignment) : -1;
    if (event_size != sizeof(CourseResponse))
      spdlog::warn("[FasterQueueRecovery] unavailable: SetCourseResponseEventArgs expected value type size={}; actual={}",
                     sizeof(CourseResponse), event_size);
    for (size_t i = 0; i < methods.size(); ++i)
      for (size_t j = 0; j < i; ++j)
        if (methods[i].first && methods[j].first && methods[i].first->methodPointer
            && methods[i].first->methodPointer == methods[j].first->methodPointer)
          spdlog::warn("[FasterQueueRecovery] unavailable: {} and {} share a native hook target",
                         methods[i].second, methods[j].second);
    return;
  }
  const bool a = SPUD_STATIC_DETOUR(engage, Engage) != nullptr;
  const bool b = SPUD_STATIC_DETOUR(retry, Retry) != nullptr;
  const bool c = SPUD_STATIC_DETOUR(course, Course) != nullptr;
  const bool d = SPUD_STATIC_DETOUR(clear, ClearAll) != nullptr;
  for (const auto& [installed, name] : std::array{
           std::pair{a, "ActionQueueManager.TryPlanPathAndEngageTarget"},
           std::pair{b, "ActionQueueManager.ShouldRetryFailedSetCourse"},
           std::pair{c, "ActionQueueManager.OnSetCourseResponseEventHandler"},
           std::pair{d, "ActionQueueManager.StopWatchdogAndClearAllQueues"}})
    if (!installed) spdlog::warn("[FasterQueueRecovery] hook installation failed: {}; feature unavailable", name);
  ready.store(a && b && c && d);
  spdlog::info("[FasterQueueRecovery] ready={}", ready.load());
}

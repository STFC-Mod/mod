#include "patches/queue_address_guard_policy.h"
#include <array>
#include <atomic>
#include <config.h>
#include <cstring>
#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace
{
using Object = Il2CppObject;
using Id     = std::int64_t;
std::atomic_bool  ready{false};
Il2CppClass *     managerClass{}, *queueClass{}, *actionClass{}, *playerClass{}, *deployedClass{};
FieldInfo *       queuesField{}, *fleetField{}, *actionsField{}, *targetField{}, *idField{}, *indexField{};
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
{ return ready.load() && Config::Get().kirshara_fixes && Config::Get().queue_enabled; }

bool Is(Object* object, Il2CppClass* cls)
{ return object && il2cpp_object_get_class(object) == cls; }

FieldInfo* Field(Il2CppClass* cls, const char* name, Il2CppTypeEnum type)
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
Object* Reference(Object* object, const char* name)
{
  auto* cls   = object ? il2cpp_object_get_class(object) : nullptr;
  auto* field = cls ? il2cpp_class_get_field_from_name(cls, name) : nullptr;
  auto* type  = field && field->type ? il2cpp_class_from_type(field->type) : nullptr;
  return type && !il2cpp_class_is_valuetype(type) && !(field->type->attrs & FIELD_ATTRIBUTE_STATIC)
                 && field->offset >= sizeof(Object)
             ? Read<Object*>(object, field)
             : nullptr;
}
int Enum(Object* object, const char* name)
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
int State(Object* object, const char* name)
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
queue_address_guard::Address Address(Object* object)
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
bool OwnsNonemptyQueue(Object* manager, Object* player, Id fleet)
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

void PlayerStateChange(auto original, Object* manager, Object* fleets)
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
bool AddressMismatch(auto original, Object* manager, Object* player)
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
Object* PlayerAddress(auto original, Object* player)
{
  auto* result = original(player);
  auto* e      = context;
  if (!Enabled() || !e || player != e->player || !Is(e->ownDeployment, deployedClass) || !Is(e->target, deployedClass))
    return result;
  auto* own    = e->ownDeployment;
  auto* cls    = il2cpp_object_get_class(own);
  auto* local  = Field(cls, "_localPlayerFleet", IL2CPP_TYPE_BOOLEAN);
  auto* recall = Field(cls, "<IsPlanningRecallCourse>k__BackingField", IL2CPP_TYPE_BOOLEAN);
  auto* model  = Reference(own, "_deploymentFleet");
  auto* id     = model ? Field(il2cpp_object_get_class(model), "fleetId_", IL2CPP_TYPE_I8) : nullptr;
  if (model && !id)
    id = Field(il2cpp_object_get_class(model), "<FleetId>k__BackingField", IL2CPP_TYPE_I8);
  auto*                               correct = Reference(own, "_address");
  const queue_address_guard::Evidence proof{true,
                                            playerStateEvent,
                                            true,
                                            local && Read<bool>(own, local) && id && Read<Id>(model, id) == e->fleet,
                                            !recall || Read<bool>(own, recall),
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
Object* LookupTarget(auto original, Object* service, Id target)
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

void InstallQueueAddressGuard()
{
  managerClass = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager").get_cls();
  queueClass   = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "ActionQueueInstance").get_cls();
  actionClass  = il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "QueueableAction").get_cls();
  playerClass =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetPlayerData").get_cls();
  deployedClass =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData")
          .get_cls();
  auto* serviceClass =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "DeploymentService")
          .get_cls();
  queuesField  = Field(managerClass, "_battleQueue", IL2CPP_TYPE_SZARRAY);
  fleetField   = Field(queueClass, "<PlayerFleetId>k__BackingField", IL2CPP_TYPE_I8);
  actionsField = Field(queueClass, "_actionQueue", IL2CPP_TYPE_GENERICINST);
  targetField  = Field(actionClass, "<FleetId>k__BackingField", IL2CPP_TYPE_I8);
  idField      = Field(playerClass, "<ID>k__BackingField", IL2CPP_TYPE_I8);
  indexField   = Field(playerClass, "<Index>k__BackingField", IL2CPP_TYPE_I4);
  using method_contract::Pointer;
  using method_contract::Resolve;
  const std::array methods{
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
    return;
  }
  const bool a = SPUD_STATIC_DETOUR(Pointer(methods[0]), PlayerStateChange) != nullptr;
  const bool b = SPUD_STATIC_DETOUR(Pointer(methods[1]), AddressMismatch) != nullptr;
  const bool c = SPUD_STATIC_DETOUR(Pointer(methods[2]), PlayerAddress) != nullptr;
  const bool d = SPUD_STATIC_DETOUR(Pointer(methods[3]), LookupTarget) != nullptr;
  ready.store(a && b && c && d);
  spdlog::info("[QueueAddressGuard] ready={}", ready.load());
}

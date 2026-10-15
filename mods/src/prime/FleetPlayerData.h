#pragma once

#include "BattleTargetData.h"
#include "HullSpec.h"
#include "RecallRequirement.h"
#include "CanRepairRequirement.h"
#include "FleetLiveData.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>
#include <optional>

enum class FleetState : int32_t {
  Unknown                = 0,
  IdleInSpace            = 1,
  Docked                 = 2,
  Mining                 = 4,
  Destroyed              = 8,
  TieringUp              = 16,
  CanReplaceOfficers     = 18,
  Repairing              = 32,
  CannotLaunch           = 56,
  Battling               = 64,
  WarpCharging           = 128,
  Warping                = 256,
  CanRemove              = 384,
  Impulsing              = 512,
  CanActivateAbility     = 513,
  CanDisco               = 515,
  Capturing              = 1024,
  AutoHunting            = 2048,
  CannotMove             = 2552,
  CanManage              = 2947,
  CanBeTargetedByAbility = 3589,
  CanEngage              = 3591,
  Outposting             = 4096,
  CanRecall              = 5637,
  Deployed               = 8133,
  CanLocate              = 8135
};

struct FleetPlayerData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetPlayerData">;

  int32_t Index()
  { return Get<"Index", int32_t>(); }
  HullSpec* Hull()
  { return Get<"Hull", HullSpec*>(); }
  void* Address()
  { return Get<"Address", void*>(); }
  FleetState CurrentState()
  { return Get<"CurrentState", FleetState>(); }
  FleetState PreviousState()
  { return Get<"PreviousState", FleetState>(); }
  uint64_t Id()
  { return Get<"Id", uint64_t>(); }
  int64_t Level()
  { return Get<"Level", int64_t>(); }
  bool HasShip()
  { return Get<"HasShip", bool>(); }
  int64_t HullId()
  { return Get<"HullId", int64_t>(); }
  bool HasAppliedTech()
  {
    struct BaseShip : il2cpp::Object {
      using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BaseShip">;
      int64_t Id()
      { return Get<"Id", int64_t>(); }
    };
    struct Slot : il2cpp::Object {
      using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "EntitySlot">;
      int64_t SpecId()
      { return Get<"SlotSpecId", int64_t>(); }
      bool HasItem()
      {
        static const auto* getter = Class::Method("get_SlotItemId", 0).Info();
        if (!getter || (getter->flags & METHOD_ATTRIBUTE_STATIC)
            || getter->return_type->type != IL2CPP_TYPE_GENERICINST)
          return false;
        // Runtime invocation boxes Nullable<Int64> as null or its value, without assuming its native layout.
        return il2cpp::detail::InvokeBoxed(getter, this, nullptr) != nullptr;
      }
    };
    struct Manager : il2cpp::Object {
      using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechManager">;
      bool ActiveSlots(int64_t ship_id, il2cpp::List<Slot*>*& slots)
      { return Call<"TryGetActiveForbiddenTechSlot", bool>(ship_id, std::ref(slots)); }
    };
    auto*                ship    = Get<"Ship", BaseShip*>();
    auto*                manager = Manager::Class::Get<"Instance", Manager*>();
    il2cpp::List<Slot*>* slots   = nullptr;
    if (!ship || !manager || !manager->ActiveSlots(ship->Id(), slots) || !slots)
      return false;
    for (auto* slot : *slots) {
      if (!slot)
        continue;
      const auto spec = slot->SpecId();
      if ((spec == 3502081615 || spec == 953301906) && slot->HasItem())
        return true;
    }
    return false;
  }

  // Live state, read for fleet sync.
  bool IsHome()
  { return Get<"IsHome", bool>(); }
  bool IsDeployed()
  { return Get<"IsDeployed", bool>(); }
  bool IsMining()
  { return Get<"IsMining", bool>(); }
  bool IsRepairing()
  { return Get<"IsRepairing", bool>(); }
  bool IsDamaged()
  { return Get<"IsDamaged", bool>(); }
  bool IsDestroyed()
  { return Get<"IsDestroyed", bool>(); }
  int32_t DrydockID()
  { return Get<"DrydockID", int32_t>(); }
  float CargoResourceFillLevel()
  { return Get<"CargoResourceFillLevel", float>(); }
  CargoHoldData* CargoHold()
  { return Get<"CargoHoldData", CargoHoldData*>(); }
  IMiningSlot* MiningData()
  { return Get<"MiningData", IMiningSlot*>(); }
  ITimerData* Timer()
  { return Get<"Timer", ITimerData*>(); }
  // No property for the mining timer, so read the private field.
  ITimerData* MiningSlotTimer()
  { return Field<"_miningSlotTimer", ITimerData*>(); }
  float GetMiningRate()
  { return Call<"GetMiningRate", float>(); }
  // Damage on the fleet's ship, so a docked ship's hull is known without the server. Empty without a ship.
  std::optional<float> HullDamage()
  {
    // IShipModel, read on the ship's runtime class.
    struct Ship : il2cpp::Object {
      float HullDamage()
      { return Get<"HullDamage", float>(); }
    };
    auto* ship = Get<"Ship", Ship*>();
    return ship ? std::optional(ship->HullDamage()) : std::nullopt;
  }

  int64_t GetLocaId()
  { return Call<"GetLocaId", int64_t>(); }
};

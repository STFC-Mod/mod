#pragma once

#include "errormsg.h"

#include "BattleTargetData.h"
#include "HullSpec.h"
#include "RecallRequirement.h"
#include "CanRepairRequirement.h"
#include "FleetLiveData.h"
#include "ManagedRead.h"

#include <cstdint>
#include <optional>

struct Ship;

enum class FleetState {
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
    
struct FleetPlayerData {
public:
  __declspec(property(get = __get_CurrentState)) FleetState CurrentState;
  __declspec(property(get = __get_PreviousState)) FleetState PreviousState;
  __declspec(property(get = __get_Id)) uint64_t Id;
  __declspec(property(get = __get_Index)) int Index;
  __declspec(property(get = __get_Hull)) HullSpec* Hull;
  __declspec(property(get = __get_Address)) void* Address;
  __declspec(property(get = __get_Level)) int64_t Level;
  __declspec(property(get = __get_HasShip)) bool HasShip;
  __declspec(property(get = __get_HullId)) int64_t HullId;
  __declspec(property(get = __get_Ship)) ::Ship* Ship;

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetPlayerData");
    return class_helper;
  }

public:
  int __get_Index()
  {
    static auto property = get_class_helper().GetProperty("Index");
    auto* value = property.Get<int>(this);
    return value ? *value : -1;
  }

  HullSpec* __get_Hull()
  {
    static auto field = get_class_helper().GetProperty("Hull");
    return field.GetRaw<HullSpec>(this);
  }
  void* __get_Address()
  {
    static auto field = get_class_helper().GetProperty("Address");
    return field.GetRaw<void>(this);
  }
  FleetState __get_CurrentState()
  {
    static auto field = get_class_helper().GetProperty("CurrentState");
    auto*      value  = field.Get<FleetState>(this);
    return value ? *value : FleetState::Unknown;
  }
  FleetState __get_PreviousState()
  {
    static auto field = get_class_helper().GetProperty("PreviousState");
    auto*      value  = field.Get<FleetState>(this);
    return value ? *value : FleetState::Unknown;
  }
  
  uint64_t __get_Id()
  {
    static auto field = get_class_helper().GetProperty("Id");
    auto*      value  = field.Get<uint64_t>(this);
    return value ? *value : 0;
  }
  int64_t __get_Level()
  {
    static auto field = get_class_helper().GetProperty("Level");
    auto*      value  = field.Get<int64_t>(this);
    return value ? *value : 0;
  }

  bool __get_HasShip()
  {
    static auto field = get_class_helper().GetProperty("HasShip");
    auto*      value  = field.Get<bool>(this);
    return value ? *value : false;
  }
  int64_t __get_HullId()
  {
    static auto property = get_class_helper().GetProperty("HullId");
    auto*       value    = property.Get<int64_t>(this);
    return value ? *value : 0;
  }
  ::Ship* __get_Ship()
  {
    static auto property = get_class_helper().GetProperty("Ship");
    return property.GetRaw<struct Ship>(this);
  }

  // live state, empty when missing
  std::optional<bool> IsHome()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "IsHome");
    return ManagedRead::Value<bool>(getter, this);
  }
  std::optional<bool> IsDeployed()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "IsDeployed");
    return ManagedRead::Value<bool>(getter, this);
  }
  std::optional<bool> IsMining()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "IsMining");
    return ManagedRead::Value<bool>(getter, this);
  }
  std::optional<bool> IsRepairing()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "IsRepairing");
    return ManagedRead::Value<bool>(getter, this);
  }
  std::optional<bool> IsDamaged()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "IsDamaged");
    return ManagedRead::Value<bool>(getter, this);
  }
  std::optional<bool> IsDestroyed()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "IsDestroyed");
    return ManagedRead::Value<bool>(getter, this);
  }
  std::optional<int32_t> DrydockID()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "DrydockID");
    return ManagedRead::Value<int32_t>(getter, this);
  }
  std::optional<float> CargoResourceFillLevel()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "CargoResourceFillLevel");
    return ManagedRead::Value<float>(getter, this);
  }
  CargoHoldData* GetCargoHoldData()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "CargoHoldData");
    return (CargoHoldData*)ManagedRead::Object(getter, this);
  }
  IMiningSlot* MiningData()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "MiningData");
    return (IMiningSlot*)ManagedRead::Object(getter, this);
  }
  ITimerData* Timer()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "Timer");
    return (ITimerData*)ManagedRead::Object(getter, this);
  }
  // no property for the mining timer, so read the private field
  ITimerData* MiningSlotTimer()
  {
    static auto field = get_class_helper().GetField("_miningSlotTimer");
    return (ITimerData*)ManagedRead::FieldObject(this, field.get_info());
  }
  // damage on the fleet's ship (BaseShip), so a docked ship's hull is known without the server
  std::optional<float> HullDamage()
  {
    static ShipGetter getter{"HullDamage"};
    return getter.Read(this->Ship);
  }
  std::optional<float> GetMiningRate()
  {
    static auto method = get_class_helper().GetMethodInfo("GetMiningRate", 0);
    return ManagedRead::Value<float>(method, this);
  }

private:
  // the ship's concrete class is only known at runtime; look the getter up once per class
  struct ShipGetter {
    const char*       property;
    Il2CppClass*      cls    = nullptr;
    const MethodInfo* getter = nullptr;

    std::optional<float> Read(void* ship)
    {
      if (!ship) {
        return std::nullopt;
      }
      if (auto* klass = ((Il2CppObject*)ship)->klass; klass != cls) {
        cls    = klass;
        getter = ManagedRead::Getter(klass, property);
      }
      return ManagedRead::Value<float>(getter, ship);
    }
  };

public:
  int64_t GetLocaId()
  {
    static auto GetLocaIdWarn = true;
    static auto GetLocaIdMethod = get_class_helper().GetMethod<int64_t(FleetPlayerData*)>("GetLocaId");

    if (GetLocaIdMethod) {
      return GetLocaIdMethod(this);
    }
    if (GetLocaIdWarn) {
      GetLocaIdWarn = false;
      ErrorMsg::MissingMethod("FleetPlayerData", "GetLocaId");
    }

    return 0;
  }
};

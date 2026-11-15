#pragma once

#include "ManagedRead.h"

#include <il2cpp/il2cpp_helper.h>

#include <cstdint>
#include <optional>

// Cargo, mining slot and timers off FleetPlayerData. Interface values go through the interface getter because
// FleetMiningSlot implements IMiningSlot explicitly. All accessors are null-safe.

namespace fleet_live_detail
{
inline Il2CppClass* models_class(const char* name)
{
  return il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", name).get_cls();
}

// interface getter, else the runtime class's
inline const MethodInfo* getter(Il2CppClass* iface, const char* property, void* obj)
{
  if (auto* g = ManagedRead::Getter(iface, property)) {
    return g;
  }
  return ManagedRead::RuntimeGetter(obj, property);
}
} // namespace fleet_live_detail

// Digit.PrimeServer.Models.IProgressData (concrete: ProgressData, CargoProgressData)
struct IProgressData {
  static Il2CppClass* get_class()
  {
    static auto cls = fleet_live_detail::models_class("IProgressData");
    return cls;
  }

  std::optional<double> CurrentValue()
  {
    return ManagedRead::Value<double>(fleet_live_detail::getter(get_class(), "CurrentValue", this), this);
  }
  std::optional<double> MaxValue()
  {
    return ManagedRead::Value<double>(fleet_live_detail::getter(get_class(), "MaxValue", this), this);
  }
};

// Digit.PrimeServer.Models.CargoHoldData
struct CargoHoldData {
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CargoHoldData");
    return class_helper;
  }

  IProgressData* CurrentCargo()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "CurrentCargo");
    return (IProgressData*)ManagedRead::Object(getter, this);
  }
  IProgressData* ProtectedCargoProgress()
  {
    static auto getter = ManagedRead::Getter(get_class_helper().get_cls(), "ProtectedCargoProgress");
    return (IProgressData*)ManagedRead::Object(getter, this);
  }
};

// Digit.PrimeServer.Models.IMiningNodeParams. Amount isn't on the interface, so it's read off the runtime class.
struct IMiningNodeParams {
  std::optional<int64_t> Amount()
  {
    return ManagedRead::Value<int64_t>(ManagedRead::RuntimeGetter(this, "Amount"), this);
  }
};

// Digit.PrimeServer.Models.IMiningSlot (concrete: FleetMiningSlot, MiningSlot)
struct IMiningSlot {
  static Il2CppClass* get_class()
  {
    static auto cls = fleet_live_detail::models_class("IMiningSlot");
    return cls;
  }

  std::optional<int64_t> ParentId()
  {
    return ManagedRead::Value<int64_t>(fleet_live_detail::getter(get_class(), "ParentId", this), this);
  }
  std::optional<int64_t> ResourceId()
  {
    return ManagedRead::Value<int64_t>(fleet_live_detail::getter(get_class(), "ResourceId", this), this);
  }
  std::optional<double> AmountMined()
  {
    return ManagedRead::Value<double>(fleet_live_detail::getter(get_class(), "AmountMined", this), this);
  }
  std::optional<uint64_t> OccupyTimeUtc()
  {
    return ManagedRead::Value<uint64_t>(fleet_live_detail::getter(get_class(), "OccupyTimeUtc", this), this);
  }
  IMiningNodeParams* PointData()
  {
    return (IMiningNodeParams*)ManagedRead::Object(fleet_live_detail::getter(get_class(), "PointData", this), this);
  }
};

// Digit.PrimeServer.Models.ITimerData (concrete: MiningSlotTimer and others)
struct ITimerData {
  static Il2CppClass* get_class()
  {
    static auto cls = fleet_live_detail::models_class("ITimerData");
    return cls;
  }

  // DateTime _dateData
  std::optional<uint64_t> DueTime()
  {
    return ManagedRead::Value<uint64_t>(fleet_live_detail::getter(get_class(), "DueTime", this), this);
  }
};

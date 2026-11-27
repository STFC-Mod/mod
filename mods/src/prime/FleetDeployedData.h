#pragma once

#include <il2cpp/il2cpp_helper.h>
#include "HullSpec.h"
#include "navigation_binding.h"

enum class DeployedFleetType {
  Nonexistent,
  Player,
  Marauder,
  NpcInstantiated,
  Sentinel,
  Alliance,
  Challenge,
};

struct FleetDeployedData {
public:
  __declspec(property(get = __get_ID)) long ID;
  __declspec(property(get = __get_Hull)) HullSpec* Hull;
  __declspec(property(get = __get_FleetType)) DeployedFleetType FleetType;

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData");
    return class_helper;
  }

public:
  // Managed Int64 stays 64-bit on Windows (where C++ long is 32-bit).
  int64_t GetId64()
  {
    int64_t id = 0;
    return navigation_binding::Value(reinterpret_cast<Il2CppObject*>(this), "get_ID", "System.Int64", id) ? id : 0;
  }

  long __get_ID()
  {
    static auto field = get_class_helper().GetProperty("ID");
    return *field.Get<long>(this);
  }

  HullSpec* __get_Hull()
  {
    static auto field = get_class_helper().GetProperty("Hull");
    return field.GetRaw<HullSpec>(this);
  }

  DeployedFleetType __get_FleetType()
  {
    static auto field = get_class_helper().GetProperty("FleetType");
    return *field.Get<DeployedFleetType>(this);
  }

  bool TryGetFleetType(DeployedFleetType& fleet_type)
  {
    static auto property = get_class_helper().GetProperty("FleetType");
    const auto* value    = property.Get<DeployedFleetType>(this);
    if (value == nullptr) {
      return false;
    }

    fleet_type = *value;
    return true;
  }
};

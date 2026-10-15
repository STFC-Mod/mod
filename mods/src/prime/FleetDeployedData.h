#pragma once

#include "HullSpec.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class DeployedFleetType : int32_t {
  Nonexistent,
  Player,
  Marauder,
  NpcInstantiated,
  Sentinel,
  Alliance,
  Challenge,
};

struct FleetDeployedData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData">;

  int64_t ID()
  { return Get<"ID", int64_t>(); }
  HullSpec* Hull()
  { return Get<"Hull", HullSpec*>(); }
  // Nonexistent when the property is missing.
  DeployedFleetType FleetType()
  { return Get<"FleetType", DeployedFleetType>(); }
};

#pragma once

#include "FleetDeployedData.h"

#include <il2cpp/il2cpp.h>

struct BattleTargetData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BattleTargetData">;

  FleetDeployedData* TargetFleetDeployedData()
  { return Field<"TargetFleetDeployedData", FleetDeployedData*>(); }
};

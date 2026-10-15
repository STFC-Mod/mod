#pragma once

#include "FleetPlayerData.h"

#include <il2cpp/il2cpp.h>

struct CourseData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CourseData">;

  FleetPlayerData* PlayerFleet()
  { return Get<"PlayerFleet", FleetPlayerData*>(); }
};

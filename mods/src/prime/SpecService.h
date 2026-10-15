#pragma once

#include "HullSpec.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct SpecService : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "SpecService">;

  HullSpec* GetHull(int64_t hullId)
  { return Call<"GetHull", HullSpec*>(hullId); }
};

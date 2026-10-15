#pragma once

#include "ZoomLevels.h"

#include <il2cpp/il2cpp.h>

struct NavigationLOD : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationLOD">;

  void UpdateLOD(ZoomLevels level)
  { Call<"UpdateLOD">(level); }

  // Writes the backing field directly; the managed setter is private.
  void SetTargetLevel(ZoomLevels level)
  { SetField<"<TargetLevel>k__BackingField">(level); }
};

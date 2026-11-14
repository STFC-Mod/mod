#pragma once

#include "FleetDeployedData.h"
#include "NavigationLOD.h"
#include "Widget.h"

struct NavigationFleetWidget : Widget<FleetDeployedData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationFleetWidget">;

  NavigationLOD* lod()
  { return Field<"_lod", NavigationLOD*>(); }
};

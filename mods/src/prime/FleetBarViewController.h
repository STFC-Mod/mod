#pragma once

#include "FleetLocalViewController.h"
#include "FleetPlayerData.h"
#include "ViewController.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct FleetBarContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "FleetBarContext">;

  il2cpp::Array<FleetPlayerData*>* FleetData()
  { return Get<"FleetData", il2cpp::Array<FleetPlayerData*>*>(); }

  FleetPlayerData* CurrentFleet()
  { return Get<"CurrentFleet", FleetPlayerData*>(); }
};

struct FleetBarViewController : ViewController<FleetBarContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "FleetBarViewController">;

  // The int overload; the other takes the list element Component.
  void RequestSelect(int32_t index, bool simulated = false)
  { Call<"RequestSelect">(index, simulated); }
  bool IsIndexSelected(int32_t index)
  { return Call<"IsIndexSelected", bool>(index); }

  FleetLocalViewController* fleetPanelController()
  { return Field<"_fleetPanelController", FleetLocalViewController*>(); }
};

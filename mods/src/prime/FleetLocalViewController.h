#pragma once

#include "ActionData.h"
#include "ShipActionPanelLocalViewController.h"
#include "FleetPlayerData.h"
#include "ShipBarItemLocalViewController.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct FleetLocalViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "FleetLocalViewController">;

  ShipActionPanelLocalViewController* shipActionPanelLocalViewController()
  { return Field<"_shipActionPanelLocalViewController", ShipActionPanelLocalViewController*>(); }
  FleetPlayerData* fleet()
  { return Get<"fleet", FleetPlayerData*>(); }
  ShipBarItemLocalViewController* shipBarItemLocalViewController()
  { return Field<"_shipBarItemLocalViewController", ShipBarItemLocalViewController*>(); }

  void CancelButtonClicked()
  { Call<"CancelButtonClicked">(); }

  // callback is an Action<bool, ActionBehaviour>; null is accepted.
  bool RequestAction(IActionData target, ActionType type, int64_t index, ActionBehaviour behaviourMask,
                     void* callback = nullptr)
  { return Call<"RequestAction", bool>(target, type, index, behaviourMask, callback); }
};

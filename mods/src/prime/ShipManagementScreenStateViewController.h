#pragma once

#include "Button.h"
#include "ShipManagementViewContext.h"
#include "ViewController.h"

struct ShipManagementScreenStateViewController : ViewController<ShipManagementViewContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementScreenStateViewController">;

  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  Button* officerIconButton()
  { return Field<"_officerIconButton", Button*>(); }
  Button* swapShipButton()
  { return Field<"_swapShipButton", Button*>(); }
};

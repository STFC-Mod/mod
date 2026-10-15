#pragma once

#include "ShipManagementViewContext.h"
#include "ViewController.h"

struct ShipManagementViewController : ViewController<ShipManagementViewContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementViewController">;

  // m_context lives on the generic ViewController<T> base; field lookup walks the parents.
  ShipManagementViewContext* Context()
  { return Field<"m_context", ShipManagementViewContext*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
};

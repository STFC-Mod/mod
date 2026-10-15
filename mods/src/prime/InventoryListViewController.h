#pragma once

#include "CanvasController.h"
#include "Hub.h"
#include "InputFieldWidget.h"

#include <il2cpp/il2cpp.h>

struct InventoryListViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryListViewController">;

  InputFieldWidget* inputField()
  { return Field<"_inputField", InputFieldWidget*>(); }
  SectionID targetSection()
  { return Field<"_targetSection", SectionID>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  CanvasController* canvasController()
  { return GetCanvasControllerFromComponent(this); }
};

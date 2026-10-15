#pragma once

#include "CanvasController.h"
#include "GenericButtonWidget.h"
#include "InputFieldWidget.h"

#include <il2cpp/il2cpp.h>

struct OfficerAssignmentViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.OfficerAssignment", "OfficerAssignmentViewController">;

  InputFieldWidget* inputField()
  { return Field<"_inputField", InputFieldWidget*>(); }
  GenericButtonWidget* officerPresetsButton()
  { return Field<"_officerPresetsButton", GenericButtonWidget*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  CanvasController* canvasController()
  { return GetCanvasControllerFromComponent(this); }
};

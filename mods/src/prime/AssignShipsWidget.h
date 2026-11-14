#pragma once

#include "ButtonAndContextWrapper.h"
#include "InputFieldWidget.h"
#include "SelectableList.h"

#include <il2cpp/il2cpp.h>

struct AssignShipsWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "AssignShipsWidget">;

  InputFieldWidget* inputField()
  { return Field<"_inputField", InputFieldWidget*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  ButtonAndContextWrapper* assignButton()
  { return Field<"_assignButton", ButtonAndContextWrapper*>(); }
  SelectableList* selectableList()
  { return Field<"_selectableList", SelectableList*>(); }
};

#pragma once

#include "GenericButtonWidget.h"

#include <il2cpp/il2cpp.h>

struct ButtonAndContextWrapper : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ButtonAndContextWrapper">;

  GenericButtonWidget* Widget()
  { return Field<"Widget", GenericButtonWidget*>(); }
};

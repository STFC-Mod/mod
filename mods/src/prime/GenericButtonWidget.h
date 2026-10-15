#pragma once

#include "GenericButtonContext.h"
#include "SemaphoreButtonListener.h"
#include "TextLocalizer.h"
#include "Widget.h"

struct GenericButtonWidget : Widget<GenericButtonContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "GenericButtonWidget">;

  SemaphoreButtonListener* SemaphoreListener()
  { return Field<"_semaphoreButtonListener", SemaphoreButtonListener*>(); }
  TextLocalizer* mainText()
  { return Field<"_mainText", TextLocalizer*>(); }
  bool Interactable()
  { return Get<"Interactable", bool>(); }
};

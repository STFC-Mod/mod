#pragma once

#include "Button.h"
#include "SemaphoreListenerBase.h"

struct SemaphoreButtonListener : SemaphoreListenerBase {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SemaphoreButtonListener">;

  Button* TheButton()
  { return Field<"_button", Button*>(); }
  bool Interactable()
  { return Get<"Interactable", bool>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  void SetInteractable(bool value)
  { Set<"Interactable">(value); }
};

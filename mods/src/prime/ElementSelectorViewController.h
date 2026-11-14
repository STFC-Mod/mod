#pragma once

#include "Button.h"

#include <il2cpp/il2cpp.h>

struct ElementSelectorViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.UI", "ElementSelectorViewController">;

  Button* decrementButton()
  { return Field<"_decrementButton", Button*>(); }
  Button* incrementButton()
  { return Field<"_incrementButton", Button*>(); }

  // Unity's internal call, so a missing binding never hides the selector hotkeys.
  bool isActiveAndEnabled()
  {
    static auto get_isActiveAndEnabled =
        il2cpp::ResolveICall<bool(ElementSelectorViewController*)>("UnityEngine.Behaviour::get_isActiveAndEnabled()");
    if (get_isActiveAndEnabled) {
      return get_isActiveAndEnabled(this);
    }
    return true;
  }

  void Decrement()
  { Call<"Decrement">(); }
  void Increment()
  { Call<"Increment">(); }

  void PressDecrement()
  {
    if (auto btn = decrementButton()) {
      btn->Press();
    }
  }

  void PressIncrement()
  {
    if (auto btn = incrementButton()) {
      btn->Press();
    }
  }
};

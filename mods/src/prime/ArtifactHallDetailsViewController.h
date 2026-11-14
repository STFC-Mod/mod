#pragma once

#include "SemaphoreButtonListener.h"

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

struct ArtifactHallDetailsViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ArtifactHall", "ArtifactHallDetailsViewController">;

  // Treat the controller as active if the icall is unavailable, so the arrow hotkeys still try.
  bool isActiveAndEnabled()
  {
    static auto get_isActiveAndEnabled = il2cpp::ResolveICall<bool(ArtifactHallDetailsViewController*)>(
        "UnityEngine.Behaviour::get_isActiveAndEnabled()");
    return get_isActiveAndEnabled ? get_isActiveAndEnabled(this) : true;
  }

  void PressLeftArrow()
  {
    auto* listener = leftArrowButton();
    if (!listener) {
      spdlog::trace("No left button listener");
      return;
    }
    auto* button = listener->TheButton();
    if (!button) {
      spdlog::trace("No left button");
      return;
    }
    button->Press();
  }

  void PressRightArrow()
  {
    auto* listener = rightArrowButton();
    if (!listener) {
      spdlog::info("No right button listener");
      return;
    }
    auto* button = listener->TheButton();
    if (!button) {
      spdlog::info("No right button");
      return;
    }
    button->Press();
  }

  SemaphoreButtonListener* leftArrowButton()
  { return Field<"_leftArrowButton", SemaphoreButtonListener*>(); }
  SemaphoreButtonListener* rightArrowButton()
  { return Field<"_rightArrowButton", SemaphoreButtonListener*>(); }
};

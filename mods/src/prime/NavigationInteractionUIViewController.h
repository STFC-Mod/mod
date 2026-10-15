#pragma once

#include <il2cpp/il2cpp.h>
#include "VisibilityController.h"
#include "SemaphoreButtonListener.h"

struct SetCourseWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "SetCourseWidget">;
  VisibilityController* visibilityController()
  { return Field<"_visibilityController", VisibilityController*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  SemaphoreButtonListener* joinWaveDefenseButton()
  { return Field<"_joinWaveDefenseButton", SemaphoreButtonListener*>(); }
};

struct NavigationInteractionUIViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationInteractionUIViewController">;

  bool TryPressJoinWaveDefense()
  {
    if (!IsSetCourseVisible()) {
      return false;
    }
    auto* widget   = Field<"_setCourseWidget", SetCourseWidget*>();
    auto* listener = widget ? widget->joinWaveDefenseButton() : nullptr;
    if (!listener || !listener->isActiveAndEnabled()) {
      return false;
    }
    // Button.Press preserves Unity's active/interactable checks and the native join callback.
    if (auto* button = listener->TheButton()) {
      button->Press();
    }
    return true;
  }

  bool IsSetCourseVisible()
  {
    auto* widget = Field<"_setCourseWidget", SetCourseWidget*>();
    auto* visibility = widget ? widget->visibilityController() : nullptr;
    return visibility && (visibility->state() == VisibilityState::Visible || visibility->state() == VisibilityState::Show)
           && widget->isActiveAndEnabled();
  }
  void OnSetCourseButtonClick()
  { Call<"OnSetCourseButtonClick">(); }
};

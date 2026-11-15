#pragma once

#include <il2cpp/il2cpp_helper.h>
#include "VisibilityController.h"
#include "SemaphoreButtonListener.h"

struct NavigationInteractionUIViewController {
public:
  bool TryPressJoinWaveDefense()
  {
    if (!IsSetCourseVisible()) {
      return false;
    }
    static auto widget_class = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "SetCourseWidget");
    static auto widget_field = get_class_helper().GetField("_setCourseWidget");
    static auto join_field = widget_class.GetField("_joinWaveDefenseButton");
    static auto active_property = SemaphoreButtonListener::get_class_helper().GetProperty("isActiveAndEnabled");
    if (!widget_field.get_info() || !join_field.get_info()) {
      return false;
    }
    auto* widget = *reinterpret_cast<char**>(reinterpret_cast<char*>(this) + widget_field.offset());
    if (!widget) {
      return false;
    }
    auto* listener = *reinterpret_cast<SemaphoreButtonListener**>(widget + join_field.offset());
    const auto* active = listener ? active_property.Get<bool>(listener) : nullptr;
    if (!active || !*active) {
      return false;
    }
    // Button.Press preserves Unity's active/interactable checks and the native join callback.
    if (auto* button = listener->TheButton) {
      button->Press();
    }
    return true;
  }

  bool IsSetCourseVisible()
  {
    static auto widget_class = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "SetCourseWidget");
    if (!get_class_helper().get_cls() || !widget_class.get_cls()) {
      return false;
    }
    static auto widget_field     = get_class_helper().GetField("_setCourseWidget");
    static auto visibility_field = widget_class.GetField("_visibilityController");
    static auto active_property  = widget_class.GetProperty("isActiveAndEnabled");
    if (!widget_field.get_info() || !visibility_field.get_info()) {
      return false;
    }

    auto* widget = *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(this) + widget_field.offset());
    if (!widget) {
      return false;
    }
    auto* visibility =
        *reinterpret_cast<VisibilityController**>(reinterpret_cast<char*>(widget) + visibility_field.offset());
    if (!visibility
        || (visibility->_state != VisibilityState::Visible && visibility->_state != VisibilityState::Show)) {
      return false;
    }
    const auto* active = active_property.Get<bool>(widget);
    return active && *active;
  }

  void OnSetCourseButtonClick()
  {
    static auto OnSetCourseButtonClick =
        get_class_helper().GetMethod<void(NavigationInteractionUIViewController*)>("OnSetCourseButtonClick");
    OnSetCourseButtonClick(this);
  }

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "NavigationInteractionUIViewController");
    return class_helper;
  }

private:
  friend class ObjectFinder<NavigationInteractionUIViewController>;
};

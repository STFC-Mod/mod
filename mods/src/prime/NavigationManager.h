#pragma once

#include "navigation_binding.h"

struct NavigationManager {
public:
  bool CanSelectPoi(int64_t id)
  { return navigation_binding::Select(reinterpret_cast<Il2CppObject*>(this), "CanSelectPoi", id); }

  bool SelectPoi(int64_t id)
  { return navigation_binding::Select(reinterpret_cast<Il2CppObject*>(this), "SelectPoi", id); }

  void HideInteraction()
  {
    static auto HideInteraction = get_class_helper().GetMethod<void(NavigationManager*)>("HideInteraction");
    HideInteraction(this);
  }

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "NavigationManager");
    return class_helper;
  }
};

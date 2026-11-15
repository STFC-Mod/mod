#pragma once

#include <il2cpp/il2cpp_helper.h>

#include "Button.h"

struct ShipManagementScreenStateViewController {
public:
  __declspec(property(get = __get_isActiveAndEnabled)) bool        isActiveAndEnabled;
  __declspec(property(get = __get__officerIconButton)) Button* _officerIconButton;
  __declspec(property(get = __get__swapShipButton)) Button*        _swapShipButton;

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementScreenStateViewController");
    return class_helper;
  }

  bool __get_isActiveAndEnabled()
  {
    static auto property = get_class_helper().GetProperty("isActiveAndEnabled");
    return property.Get<bool>(this);
  }

  Button* __get__officerIconButton()
  {
    static auto field = get_class_helper().GetField("_officerIconButton").offset();
    return *(Button**)((char*)this + field);
  }

  Button* __get__swapShipButton()
  {
    static auto field = get_class_helper().GetField("_swapShipButton").offset();
    return *(Button**)((char*)this + field);
  }

private:
  friend class ObjectFinder<ShipManagementScreenStateViewController>;
};

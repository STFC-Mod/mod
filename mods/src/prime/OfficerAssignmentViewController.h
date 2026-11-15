#pragma once

#include <il2cpp/il2cpp_helper.h>

#include "CanvasController.h"
#include "GenericButtonWidget.h"
#include "InputFieldWidget.h"

struct OfficerAssignmentViewController {
public:
  __declspec(property(get = __get__inputField)) InputFieldWidget*                   _inputField;
  __declspec(property(get = __get__officerPresetsButton)) GenericButtonWidget* _officerPresetsButton;
  __declspec(property(get = __get_isActiveAndEnabled)) bool                           isActiveAndEnabled;
  __declspec(property(get = __get_canvasController)) CanvasController*                canvasController;

private:
  friend class ObjectFinder<OfficerAssignmentViewController>;

public:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.OfficerAssignment", "OfficerAssignmentViewController");
    return class_helper;
  }

  InputFieldWidget* __get__inputField()
  {
    static auto field = get_class_helper().GetField("_inputField").offset();
    return *(InputFieldWidget**)((uintptr_t)this + field);
  }

  GenericButtonWidget* __get__officerPresetsButton()
  {
    static auto field = get_class_helper().GetField("_officerPresetsButton").offset();
    return *(GenericButtonWidget**)((uintptr_t)this + field);
  }

  bool __get_isActiveAndEnabled()
  {
    static auto field = get_class_helper().GetProperty("isActiveAndEnabled");
    return field.Get<bool>(this);
  }

  CanvasController* __get_canvasController()
  {
    return GetCanvasControllerFromComponent(this);
  }
};

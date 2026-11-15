#pragma once

#include <il2cpp/il2cpp_helper.h>

#include "errormsg.h"
#include "FleetPlayerData.h"

struct ShipManagementViewContext {
public:
  FleetPlayerData* CurrentFleet()
  {
    static auto method = get_class_helper().GetMethod<FleetPlayerData*(ShipManagementViewContext*)>("get_CurrentFleet");
    return method ? method(this) : nullptr;
  }

  int32_t CurrentIndex()
  {
    static auto CurrentIndexWarn = true;
    static auto CurrentIndexMethod =
        get_class_helper().GetMethod<int32_t(ShipManagementViewContext*)>("get_CurrentIndex");

    if (CurrentIndexMethod) {
      return CurrentIndexMethod(this);
    } else if (CurrentIndexWarn) {
      CurrentIndexWarn = false;
      ErrorMsg::MissingMethod("ShipManagementViewContext", "get_CurrentIndex");
    }

    return -1;
  }

  void SetCurrentIndex(int32_t index)
  {
    static auto SetCurrentIndexWarn = true;
    static auto SetCurrentIndexMethod =
        get_class_helper().GetMethod<void(ShipManagementViewContext*, int32_t)>("set_CurrentIndex");

    if (SetCurrentIndexMethod) {
      SetCurrentIndexMethod(this, index);
    } else if (SetCurrentIndexWarn) {
      SetCurrentIndexWarn = false;
      ErrorMsg::MissingMethod("ShipManagementViewContext", "set_CurrentIndex");
    }
  }

  void SetLastMovementWasToIncrement(bool increment)
  {
    static auto Warn    = true;
    static auto SetLast = get_class_helper().GetMethod<void(ShipManagementViewContext*, bool)>(
        "set_LastMovementWasToIncrement");

    if (SetLast) {
      SetLast(this, increment);
    } else if (Warn) {
      Warn = false;
      ErrorMsg::MissingMethod("ShipManagementViewContext", "set_LastMovementWasToIncrement");
    }
  }

  int32_t ListCount()
  {
    static auto ListCountWarn = true;
    static auto ListCountMethod =
        get_class_helper().GetMethod<int32_t(ShipManagementViewContext*)>("get_ListCount");

    if (ListCountMethod) {
      return ListCountMethod(this);
    } else if (ListCountWarn) {
      ListCountWarn = false;
      ErrorMsg::MissingMethod("ShipManagementViewContext", "get_ListCount");
    }

    return -1;
  }

  void Sort()
  {
    static auto SortWarn   = true;
    static auto SortMethod = get_class_helper().GetMethod<void(ShipManagementViewContext*)>("Sort");

    if (SortMethod) {
      SortMethod(this);
    } else if (SortWarn) {
      SortWarn = false;
      ErrorMsg::MissingMethod("ShipManagementViewContext", "Sort");
    }
  }

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementViewContext");
    return class_helper;
  }
};

#pragma once

#include <il2cpp/il2cpp_helper.h>

#include "FleetLocalViewController.h"

struct FleetBarContext {
public:
  __declspec(property(get = __get_CurrentFleet)) void* CurrentFleet;

public:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.HUD", "FleetBarContext");
    return class_helper;
  }

public:
  Il2CppArraySize* FleetData()
  {
    static auto property = get_class_helper().GetProperty("FleetData");
    return property.GetRaw<Il2CppArraySize>(this);
  }

  void* __get_CurrentFleet()
  {
    static auto field = get_class_helper().GetProperty("CurrentFleet");
    return field.Get<void>(this);
  }
};

struct FleetBarViewController {
public:
  __declspec(property(get = __get__fleetPanelController)) FleetLocalViewController* _fleetPanelController;

  void RequestSelect(int32_t index, bool simulated = false)
  {
    static auto RequestSelectWarn   = true;
    static auto RequestSelectMethod = get_class_helper().GetMethodSpecial<void(FleetBarViewController*, int32_t, bool)>(
        "RequestSelect", [](auto count, auto params) {
          if (count != 2) {
            return false;
          }
          auto p1 = params[0]->type;
          if (p1 == IL2CPP_TYPE_I4) {
            return true;
          }
          return false;
        });
    if (RequestSelectMethod) {
      RequestSelectMethod(this, index, simulated);
    } else if (RequestSelectWarn) {
      RequestSelectWarn = false;
      ErrorMsg::MissingMethod("FleetBarViewController", "RequestSelect");
    }
  }

  bool IsIndexSelected(int32_t index)
  {
    static auto IsIndexSelectedWarn = true;
    static auto IsIndexSelectedMethod =
        get_class_helper().GetMethod<bool(FleetBarViewController*, int32_t)>("IsIndexSelected");

    if (IsIndexSelectedMethod) {
      return IsIndexSelectedMethod(this, index);
    } else if (IsIndexSelectedWarn) {
      IsIndexSelectedWarn = false;
      ErrorMsg::MissingMethod("FleetBarViewController", "IsIndexSelected");
    }

    return false;
  }

  FleetBarContext* CanvasContext()
  {
    static auto n = get_class_helper().GetProperty("CanvasContext");
    return n.GetRaw<FleetBarContext>(this);
  }

private:
  friend class ObjectFinder<FleetBarViewController>;

public:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.HUD", "FleetBarViewController");
    return class_helper;
  }

public:
  FleetLocalViewController* __get__fleetPanelController()
  {
    static auto field = get_class_helper().GetField("_fleetPanelController").offset();
    return *(FleetLocalViewController**)((uintptr_t)this + field);
  }
};

#pragma once

#include "NavigationInteractionUIContext.h"
#include "ViewController.h"
#include "VisibilityController.h"

struct ParentObjectViewerViewController : ViewController<NavigationInteractionUIContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "ParentObjectViewerViewController">;

  void ActivateViewerHUDState(bool isHUDOpen = false)
  { Call<"ActivateViewerHUDState">(isHUDOpen); }
  void TriggerInstantHide()
  { Call<"TriggerInstantHide">(); }
  void TriggerShow()
  { Call<"TriggerShow">(); }

  VisibilityController* visibilityController()
  { return Field<"_visibilityController", VisibilityController*>(); }
  bool IsShowing()
  { return Get<"IsShowing", bool>(); }
};

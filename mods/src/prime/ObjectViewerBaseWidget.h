#pragma once

#include "NavigationInteractionUIContext.h"
#include "ParentObjectViewerViewController.h"
#include "VisibilityController.h"
#include "Widget.h"

struct ObjectViewerBaseWidget : Widget<NavigationInteractionUIContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "ObjectViewerBaseWidget">;

  void HideAllViewers()
  { Call<"HideAllViewers">(); }

  VisibilityController* visibilityController()
  { return Field<"_visibilityController", VisibilityController*>(); }
  ParentObjectViewerViewController* Parent()
  { return Get<"Parent", ParentObjectViewerViewController*>(); }
  bool IsInfoShown()
  { return Get<"IsInfoShown", bool>(); }
  void SetIsInfoShown(bool value)
  { Set<"IsInfoShown">(value); }
};

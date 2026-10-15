#pragma once

#include "GenericButtonWidget.h"
#include "ObjectViewerBaseWidget.h"

struct StarNodeObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "StarNodeObjectViewerWidget">;

  void OnViewButtonActivation()
  { Call<"OnViewButtonActivation">(); }
  void InitiateWarp()
  { Call<"InitiateWarp">(); }
  GenericButtonWidget* visitButton()
  { return Field<"_visitButton", GenericButtonWidget*>(); }
};

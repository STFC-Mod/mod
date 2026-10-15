#pragma once

#include "BattleTargetData.h"
#include "GenericButtonWidget.h"
#include "Widget.h"

struct ScanEngageButtonsWidget : Widget<BattleTargetData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Combat", "ScanEngageButtonsWidget">;

  void OnArmadaButtonClicked()
  { Call<"OnArmadaButtonClicked">(); }
  void OnEngageButtonClicked()
  { Call<"OnEngageButtonClicked">(); }
  void OnScanButtonClicked()
  { Call<"OnScanButtonClicked">(); }

  GenericButtonWidget* armadaButton()
  { return Field<"_armadaButton", GenericButtonWidget*>(); }
};

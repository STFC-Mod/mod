#pragma once

#include "BattleTargetData.h"
#include "GenericButtonWidget.h"
#include "ObjectViewerBaseWidget.h"
#include "RewardsButtonWidget.h"
#include "ScanEngageButtonsWidget.h"

struct PreScanTargetWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Combat", "PreScanTargetWidget">;

  void ValidateThenCreateArmada()
  { Call<"ValidateThenCreateArmada">(); }
  void OnAddToQueueClickedEventHandler()
  { Call<"OnAddToQueueClickedEventHandler">(); }
  // Declared on ObjectViewerBaseWidget.
  void OnInfoClick()
  { Call<"OnInfoClick">(); }

  BattleTargetData* battleTargetData()
  { return Field<"_battleTargetData", BattleTargetData*>(); }
  ScanEngageButtonsWidget* scanEngageButtonsWidget()
  { return Field<"_scanEngageButtonsWidget", ScanEngageButtonsWidget*>(); }
  RewardsButtonWidget* rewardsButtonWidget()
  { return Field<"_rewardsButtonWidget", RewardsButtonWidget*>(); }
  GenericButtonWidget* addToQueueButtonWidget()
  { return Field<"_addToQueueButtonWidget", GenericButtonWidget*>(); }
  GenericButtonWidget* armadaAttackButton()
  { return Field<"_armadaAttackButton", GenericButtonWidget*>(); }
};

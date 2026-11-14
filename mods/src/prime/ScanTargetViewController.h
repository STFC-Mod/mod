#pragma once

#include "BattleTargetData.h"
#include "ScanEngageButtonsWidget.h"
#include "ViewController.h"

struct ScanTargetViewController : ViewController<BattleTargetData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Combat", "ScanTargetViewController">;

  ScanEngageButtonsWidget* scanEngageButtonsWidget()
  { return Field<"_scanEngageButtonsWidget", ScanEngageButtonsWidget*>(); }
};

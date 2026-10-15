#pragma once

#include "BattleTargetData.h"
#include "VisibilityController.h"
#include "Widget.h"

struct RewardsButtonWidget : Widget<BattleTargetData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Combat", "RewardsButtonWidget">;

  void RewardsClicked()
  { Call<"RewardsClicked">(); }

  VisibilityController* rewardsController()
  { return Field<"_rewardsController", VisibilityController*>(); }
};

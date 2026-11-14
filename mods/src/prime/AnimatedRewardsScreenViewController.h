#pragma once

#include <il2cpp/il2cpp.h>

struct AnimatedRewardsScreenViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Missions.UI", "AnimatedRewardsScreenViewController">;

  void GoBackToLastSection()
  { Call<"GoBackToLastSection">(); }
  void OnCollectClicked()
  { Call<"OnCollectClicked">(); }
  bool IsActive()
  { return Call<"IsActive", bool>(); }
};

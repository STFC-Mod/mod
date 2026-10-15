#pragma once

#include <il2cpp/il2cpp.h>

struct GenericRewardsScreenViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.SharedFeatures", "GenericRewardsScreenViewController">;

  void OnCollectClicked()
  { Call<"OnCollectClicked">(); }
  bool IsActive()
  { return Call<"IsActive", bool>(); }
};

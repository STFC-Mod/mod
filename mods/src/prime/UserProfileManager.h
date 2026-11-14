#pragma once

#include "MonoSingleton.h"
#include "UserProfile.h"

struct UserProfileManager : MonoSingleton<UserProfileManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlayerProfile", "UserProfileManager">;

  [[nodiscard]] UserProfile* GetLocalUserProfile()
  { return Call<"GetLocalUserProfile", UserProfile*>(); }
};

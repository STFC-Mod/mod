#pragma once

#include "prime/server.h"

struct UserProfileManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlayerProfile", "UserProfileManager">;

  static UserProfileManager* Instance()
  { return Class::Get<"Instance", UserProfileManager*>(); }

  [[nodiscard]] UserProfile* GetLocalUserProfile()
  { return Call<"GetLocalUserProfile", UserProfile*>(); }
};

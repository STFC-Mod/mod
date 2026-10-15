#pragma once
#include <il2cpp/il2cpp.h>
struct PlayerTitleWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlayerAvatars", "PlayerTitleWidget">;
};
struct UserProfileWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlayerProfile", "UserProfileWidget">;
  PlayerTitleWidget* playerTitleWidget()
  { return Field<"_playerTitleWidget", PlayerTitleWidget*>(); }
};

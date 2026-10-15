#pragma once
#include <il2cpp/il2cpp.h>
#include "UserProfileWidget.h"
struct ChatMessageWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "ChatMessageWidget">;
  UserProfileWidget* userProfileWidget()
  { return Field<"_userProfileWidget", UserProfileWidget*>(); }
};

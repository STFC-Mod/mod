#pragma once

#include "ChatMessageListLocalViewController.h"
#include "TabBarViewController.h"

#include <il2cpp/il2cpp.h>

struct FullScreenChatViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "FullScreenChatViewController">;

  ChatMessageListLocalViewController* messageList()
  { return Field<"_messageList", ChatMessageListLocalViewController*>(); }
  TabBarViewController* categoriesTabBarViewController()
  { return Field<"_categoriesTabBarViewController", TabBarViewController*>(); }
};

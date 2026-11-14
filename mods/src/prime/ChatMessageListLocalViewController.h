#pragma once

#include "ChatManager.h"
#include "TMP_InputField.h"
#include "ViewController.h"

#include <il2cpp/il2cpp.h>

struct ChatSectionContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "ChatSectionContext">;

  ChatChannelCategory category()
  { return Field<"_category", ChatChannelCategory>(); }
};

struct ChatMessageListLocalViewController : ViewController<ChatSectionContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "ChatMessageListLocalViewController">;

  TMP_InputField* inputField()
  { return Field<"_inputField", TMP_InputField*>(); }
  bool inputFieldSelected()
  { return Field<"_inputFieldSelected", bool>(); }
};

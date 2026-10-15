#pragma once

#include "MonoSingleton.h"

#include <il2cpp/il2cpp.h>

enum class ChatChannelCategory : int32_t {
  None            = -1,
  Newbie          = 0,
  Global          = 1,
  Alliance        = 2,
  Private         = 3,
  Private_Message = 4,
  Block           = 5,
  Regional        = 6,
};

enum class ChatViewMode : int32_t {
  Fullscreen = 0,
  Side       = 1,
};

struct ChatManager : MonoSingleton<ChatManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "ChatManager">;

  void OpenChannel(ChatChannelCategory category = ChatChannelCategory::Alliance)
  { Call<"OpenChannel">(category, il2cpp_string_new("")); }

  void OpenChannel(ChatChannelCategory category, ChatViewMode viewMode)
  {
    SetViewMode(viewMode);
    OpenChannel(category);
  }

  bool CanSeeNewbieChat()
  { return Call<"CanSeeNewbieChat", bool>(); }
  bool CanSeeRegionalChat()
  { return Call<"CanSeeRegionalChat", bool>(); }

  bool IsSideChatAllowed()
  { return Get<"IsSideChatAllowed", bool>(); }
  bool IsSideChatOpen()
  { return Get<"IsSideChatOpen", bool>(); }
  bool isContextInitialised()
  { return Field<"_isContextInitialised", bool>(); }
  ChatViewMode ViewMode()
  { return Get<"ViewMode", ChatViewMode>(); }
  void SetViewMode(ChatViewMode value)
  { Set<"ViewMode">(value); }
};

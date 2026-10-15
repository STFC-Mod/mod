#pragma once

// Chat manager and chat view controllers.

#include "prime/ui.h"

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
  { Call<"OpenChannel">(category, il2cpp::NewString("")); }

  void OpenChannel(ChatChannelCategory category, ChatViewMode viewMode)
  {
    SetViewMode(viewMode);
    OpenChannel(category);
  }

  bool CanSeeNewbieChat()
  { return Call<"CanSeeNewbieChat", bool>(); }
  bool CanSeeRegionalChat()
  { return Call<"CanSeeRegionalChat", bool>(); }

  bool IsSideChatOpen()
  { return Get<"IsSideChatOpen", bool>(); }
  bool IsSideChatAllowed()
  { return Get<"IsSideChatAllowed", bool>(); }
  bool isContextInitialised()
  { return Field<"_isContextInitialised", bool>(); }
  void SetViewMode(ChatViewMode value)
  { Set<"ViewMode">(value); }
};

struct SwipeScroller : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SwipeScroller">;

  int32_t currentContentIndex()
  { return Field<"_currentContentIndex", int32_t>(); }
  void FocusOnInstantly(int32_t index)
  { Call<"FocusOnInstantly">(index); }
};

struct ChatPreviewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "ChatPreviewController">;

  SwipeScroller* swipeScroller()
  { return Field<"_swipeScroller", SwipeScroller*>(); }
  void SetFocusedPanel(ChatChannelCategory value)
  { SetField<"_focusedPanel">(value); }
  // Set by the game after its visibility refresh for a section change.
  bool isSectionChangedCompleted()
  { return Field<"_isSectionChangedCompleted", bool>(); }
};

struct ChatMessageListLocalViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "ChatMessageListLocalViewController">;

  unity::TMP_InputField* inputField()
  { return Field<"_inputField", unity::TMP_InputField*>(); }
};

struct FullScreenChatViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "FullScreenChatViewController">;

  ChatMessageListLocalViewController* messageList()
  { return Field<"_messageList", ChatMessageListLocalViewController*>(); }
  TabBarViewController* categoriesTabBarViewController()
  { return Field<"_categoriesTabBarViewController", TabBarViewController*>(); }
};

struct PlayerTitleWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlayerAvatars", "PlayerTitleWidget">;
};
struct UserProfileWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlayerProfile", "UserProfileWidget">;
  PlayerTitleWidget* playerTitleWidget()
  { return Field<"_playerTitleWidget", PlayerTitleWidget*>(); }
};
struct ChatMessageWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "ChatMessageWidget">;
  UserProfileWidget* userProfileWidget()
  { return Field<"_userProfileWidget", UserProfileWidget*>(); }
};

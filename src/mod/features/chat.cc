// Chat: the shortcuts that open it and switch channel; [ui] disable_galaxy_chat and disable_veil_chat, which grey
// out those tabs in full-screen chat, keep the chat preview off them, and drop their messages from the preview;
// [ui] show_player_title, which can hide the title row under each sender's name; and [ui] default_chat_width and
// auto_open_chat, which size side chat and open alliance side chat once per login.

#include "config.h"
#include "features/chat_startup.h"
#include "features/features.h"
#include "shortcuts.h"

#include "game/app.h"
#include "game/chat.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/core.h>
#include <unity/tracker.h>

#include <chrono>
#include <utility>

namespace
{
// --- Side chat on login ---
//
// Both options act once per login: the width while side chat is open, and the open once the station and chat have
// settled. Login and AppInit start the next login over.

il2cpp::GcHandle  chat_preview; // Weak: the preview the hooks last saw.
ChatStartup::Gate chat_startup;
bool              chat_width_applied = false;

void ObserveChatPreview(ChatPreviewController* preview)
{
  if (!Config::Get().ui.auto_open_chat || chat_preview.Get() == preview)
    return;
  chat_preview             = il2cpp::GcHandle::Weak(preview);
  chat_startup.ready_since = -1;
}

void ChatFrame()
{
  const auto& ui           = Config::Get().ui;
  const bool  custom_width = ui.default_chat_width > 0;
  if (!ui.auto_open_chat && !custom_width)
    return;
  auto* sections = Hub::Sections();
  if (!sections)
    return;
  const auto section = sections->CurrentSection();
  if (section == SectionID::Login || section == SectionID::AppInit) {
    chat_startup       = {};
    chat_width_applied = false;
    // The preview may initialize while the section is still Login or AppInit. Keep its weak handle: clearing it
    // here can lose the only AboutToShow it gets. A destroyed preview reads as null.
    return;
  }
  if ((!ui.auto_open_chat || chat_startup.completed) && (!custom_width || chat_width_applied))
    return;
  auto* chat = ChatManager::Instance();
  if (!chat)
    return;
  const bool side_open = chat->IsSideChatOpen();
  // Once per login; after that, resizing is left to the player.
  if (side_open && custom_width && !chat_width_applied) {
    auto* screens = ScreenManager::Instance();
    if (auto* frames = screens ? screens->frameManager() : nullptr) {
      frames->ResizeSideFrame(ui.default_chat_width);
      chat_width_applied = true;
    }
  }
  if (!ui.auto_open_chat || chat_startup.completed)
    return;
  auto*      preview = chat_preview.Get<ChatPreviewController>();
  const bool station = section == SectionID::Starbase_Interior || section == SectionID::Starbase_Exterior;
  // The preview sets section completion after its visibility refresh.
  const bool   ready = station && preview && preview->isSectionChangedCompleted() && chat->isContextInitialised()
                       && chat->IsSideChatAllowed();
  const double now   = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  if (chat_startup.Tick(ready, side_open || Hub::IsInChat(), now)) {
    spdlog::info("[Chat] readiness settled; opening alliance side chat");
    chat->OpenChannel(ChatChannelCategory::Alliance, ChatViewMode::Side);
  }
}

// --- Disabled channels ---

// Tab positions in both the full-screen chat and the preview. Cadet chat, first, and veil chat, after galaxy,
// only exist for some players and shift the tabs after them; a missing veil tab is -1.
struct ChatTabs {
  int32_t galaxy   = 0;
  int32_t veil     = -1;
  int32_t alliance = 1;

  static ChatTabs Current()
  {
    auto* chat = ChatManager::Instance();
    if (!chat)
      return {};
    const int32_t galaxy = chat->CanSeeNewbieChat() ? 1 : 0;
    const bool    veil   = chat->CanSeeRegionalChat();
    return {.galaxy = galaxy, .veil = veil ? galaxy + 1 : -1, .alliance = galaxy + (veil ? 2 : 1)};
  }

  // Whether `index` is a tab the settings disable.
  bool Disabled(int32_t index) const
  {
    const auto& ui = Config::Get().ui;
    return (ui.disable_galaxy_chat && index == galaxy) || (ui.disable_veil_chat && veil != -1 && index == veil);
  }
};

void FullScreenChatViewController_AboutToShow_Hook(auto original, FullScreenChatViewController* _this)
{
  original(_this);
  const auto& ui = Config::Get().ui;
  if (!ui.disable_galaxy_chat && !ui.disable_veil_chat)
    return;

  auto*      tab_bar_view = _this->categoriesTabBarViewController();
  auto*      tab_bar      = tab_bar_view ? tab_bar_view->tabBar() : nullptr;
  auto*      buttons      = tab_bar ? tab_bar->data() : nullptr;
  const auto tabs         = ChatTabs::Current();
  for (const auto index : {tabs.galaxy, tabs.veil})
    if (auto* button = tabs.Disabled(index) ? il2cpp::At(buttons, index) : nullptr)
      static_cast<GenericButtonContext*>(button)->SetInteractable(false);
}

// Selecting a disabled tab (with the keyboard, say) does nothing.
void FullScreenChatViewController_OnDidChangeSelectedTab_Hook(auto original, FullScreenChatViewController* _this,
                                                              int32_t index, void* tab)
{
  if (!ChatTabs::Current().Disabled(index))
    original(_this, index, tab);
}

// Lines the preview's scroller up with the alliance panel.
void FocusAlliance(ChatPreviewController* preview, int32_t alliance)
{
  if (auto* scroller = preview->swipeScroller(); scroller && scroller->currentContentIndex() != alliance)
    scroller->FocusOnInstantly(alliance);
}

void ChatPreviewController_AboutToShow_Hook(auto original, ChatPreviewController* _this)
{
  original(_this);
  ObserveChatPreview(_this);
  // The preview reopens on the last panel; move off it only if that channel is disabled.
  auto*      scroller = _this->swipeScroller();
  const auto tabs     = ChatTabs::Current();
  if (scroller && tabs.Disabled(scroller->currentContentIndex())) {
    _this->SetFocusedPanel(ChatChannelCategory::Alliance);
    FocusAlliance(_this, tabs.alliance);
  }
}

void ChatPreviewController_OnPanelFocused_Hook(auto original, ChatPreviewController* _this, int32_t index)
{
  ObserveChatPreview(_this);
  const auto tabs = ChatTabs::Current();
  if (!tabs.Disabled(index))
    return original(_this, index);
  // Swiped onto a disabled channel: focus alliance before the game handles the change, then line the scroller up.
  _this->SetFocusedPanel(ChatChannelCategory::Alliance);
  original(_this, tabs.alliance);
  FocusAlliance(_this, tabs.alliance);
}

void ChatPreviewController_OnGlobalMessageReceived_Hook(auto original, ChatPreviewController* _this, void* message)
{
  if (!Config::Get().ui.disable_galaxy_chat)
    original(_this, message);
}

void ChatPreviewController_OnRegionalMessageReceived_Hook(auto original, ChatPreviewController* _this, void* message)
{
  if (!Config::Get().ui.disable_veil_chat)
    original(_this, message);
}

// --- Player titles ---
//
// Only chat rows lose their title: the same profile widget appears on other screens.

void HideChatTitle(UserProfileWidget* profile)
{
  auto* title     = profile ? profile->playerTitleWidget() : nullptr;
  auto* transform = title ? profile->transform() : nullptr;
  if (!transform || !transform->FindInParents<ChatMessageWidget>())
    return;
  if (auto* object = title->gameObject())
    object->SetActive(false);
}

// The game shows the title again on every update, so it is hidden after each one.
void UserProfileWidget_UpdatePlayerTitle_Hook(auto original, UserProfileWidget* _this)
{
  original(_this);
  if (!Config::Get().ui.show_player_title)
    HideChatTitle(_this);
}

// SetWidgetData and OnDidBindContext both bind a title from the title widget's side; one hook serves both.
void PlayerTitleWidget_Bind_Hook(auto original, PlayerTitleWidget* _this)
{
  original(_this);
  if (Config::Get().ui.show_player_title)
    return;
  if (auto* transform = _this->transform())
    HideChatTitle(transform->FindInParents<UserProfileWidget>());
}

bool InstallChat()
{
  il2cpp::HookSet hooks("Chat");
  IL2CPP_STAGE(hooks, FullScreenChatViewController::Class::Method("AboutToShow"),
               FullScreenChatViewController_AboutToShow_Hook);
  IL2CPP_STAGE(hooks, FullScreenChatViewController::Class::Method("OnDidChangeSelectedTab"),
               FullScreenChatViewController_OnDidChangeSelectedTab_Hook);
  IL2CPP_STAGE(hooks, ChatPreviewController::Class::Method("AboutToShow"), ChatPreviewController_AboutToShow_Hook);
  IL2CPP_STAGE(hooks, ChatPreviewController::Class::Method("OnPanelFocused"),
               ChatPreviewController_OnPanelFocused_Hook);
  IL2CPP_STAGE(hooks, ChatPreviewController::Class::Method("OnGlobalMessageReceived"),
               ChatPreviewController_OnGlobalMessageReceived_Hook);
  IL2CPP_STAGE(hooks, ChatPreviewController::Class::Method("OnRegionalMessageReceived"),
               ChatPreviewController_OnRegionalMessageReceived_Hook);
  return hooks.Commit();
}

// The feature is the update hook. The bind paths only catch titles bound without an update, so each goes in as
// a set of its own, and the feature does not depend on them.
bool InstallChatTitles()
{
  const auto      update = UserProfileWidget::Class::Method("UpdatePlayerTitle", 0);
  il2cpp::HookSet hooks("ChatTitles");
  IL2CPP_STAGE(hooks, update, UserProfileWidget_UpdatePlayerTitle_Hook);
  const bool installed = hooks.Commit();
  const auto set_data = PlayerTitleWidget::Class::Method("SetWidgetData", 0);
  const auto bind     = PlayerTitleWidget::Class::Method("OnDidBindContext", 0);
  if (!set_data || !bind)
    return installed;
  // Both, or one of them and the update, often branch to one shared body, which SPUD patches in their place. One
  // detour on it covers them all; a second would chain on it and run the hook twice, once with a receiver of the
  // wrong class.
  void* const update_body = il2cpp::DetourTarget(update);
  void* const set_body    = il2cpp::DetourTarget(set_data);
  void* const bind_body   = il2cpp::DetourTarget(bind);
  if (set_body != update_body) {
    il2cpp::HookSet set_hooks("ChatTitles");
    IL2CPP_STAGE(set_hooks, set_data, PlayerTitleWidget_Bind_Hook);
    set_hooks.Commit();
  }
  if (bind_body != update_body && bind_body != set_body) {
    il2cpp::HookSet bind_hooks("ChatTitles");
    IL2CPP_STAGE(bind_hooks, bind, PlayerTitleWidget_Bind_Hook);
    bind_hooks.Commit();
  }
  return installed;
}
} // namespace

void OpenChatShortcut()
{
  const bool side = Shortcut::IsDown(Action::kShowChatSide1) || Shortcut::IsDown(Action::kShowChatSide2);
  if (!side && !Shortcut::IsDown(Action::kShowChat))
    return;
  auto* chat = ChatManager::Instance();
  if (!chat)
    return;

  // With side chat already open, the shortcut focuses its input instead.
  if (chat->IsSideChatOpen()) {
    auto* view = unity::Newest<FullScreenChatViewController>();
    auto* list = view ? view->messageList() : nullptr;
    if (auto* input = list ? list->inputField() : nullptr)
      input->ActivateInputField();
  } else {
    chat->OpenChannel(ChatChannelCategory::Alliance, side ? ChatViewMode::Side : ChatViewMode::Fullscreen);
  }
}

bool ChatChannelShortcuts()
{
  auto* chat = ChatManager::Instance();
  if (!chat)
    return false;
  for (const auto [shortcut, channel] : {std::pair{Action::kSelectChatGlobal, ChatChannelCategory::Global},
                                         std::pair{Action::kSelectChatAlliance, ChatChannelCategory::Alliance},
                                         std::pair{Action::kSelectChatPrivate, ChatChannelCategory::Private}}) {
    if (Shortcut::IsDown(shortcut)) {
      chat->OpenChannel(channel);
      return true;
    }
  }
  return false;
}

const Feature kChat{"Chat", &Config::Patches::chatpatches, InstallChat, ChatFrame};
const Feature kChatTitles{"ChatTitles", &Config::Patches::chattitledisplayhooks, InstallChatTitles};

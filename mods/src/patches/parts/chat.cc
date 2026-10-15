// Chat: the shortcuts that open it and switch channel; disable_galaxy_chat and disable_veil_chat, which grey out
// those tabs in full-screen chat, keep the chat preview off them, and drop their messages from the preview; and
// default_chat_width and auto_open_chat, which size side chat and open alliance side chat once per login.

#include "config.h"
#include "patches/chat_startup.h"
#include "patches/patches.h"
#include "patches/shortcuts.h"

#include "prime/Hub.h"
#include "prime/chat.h"
#include "prime/ui.h"

#include <il2cpp/hook.h>
#include <spdlog/spdlog.h>
#include <unity/tracker.h>

#include <chrono>
#include <cmath>
#include <stdexcept>

namespace
{
il2cpp::GcHandle preview_handle;
ChatStartup::Gate startup;
bool width_applied = false;
bool startup_failed = false;

void ObserveChatPreview(ChatPreviewController* preview)
{
  if (!Config::Get().ui.auto_open_chat)
    return;
  if (preview_handle && preview_handle.Get() == preview)
    return;
  preview_handle = il2cpp::GcHandle::Weak(preview);
  startup.ready_since = -1;
  spdlog::info("[ChatOptions] observed chat preview");
}

void UpdateChatOptions()
{
  const float configured_width = Config::Get().ui.default_chat_width;
  const bool custom_width = std::isfinite(configured_width) && configured_width > 0;
  if (startup_failed || (!Config::Get().ui.auto_open_chat && !custom_width))
    return;
  try {
    auto* sections = Hub::Sections();
    if (!sections)
      return;
    const auto section = sections->CurrentSection();
    if (section == SectionID::Login || section == SectionID::AppInit) {
      startup = {};
      width_applied = false;
      // The preview may initialize while CurrentSection is still Login/AppInit.
      // Keep its weak handle: clearing it here can lose the only AboutToShow
      // notification. A destroyed preview naturally resolves to null.
      return;
    }
    if ((!Config::Get().ui.auto_open_chat || startup.completed) && (!custom_width || width_applied))
      return;
    auto* manager = ChatManager::Instance();
    if (!manager)
      return;
    const bool side_open = manager->IsSideChatOpen();
    float width = configured_width;
    // Apply once per login, then leave manual resizing to the game.
    if (side_open && !width_applied && std::isfinite(width) && width > 0) {
      auto* screen = ScreenManager::Instance();
      if (auto* frame = screen ? screen->frameManager() : nullptr) {
        if (!frame->ResizeSideFrame(width))
          throw std::runtime_error("side frame resize failed");
        width_applied = true;
      }
    }
    if (!Config::Get().ui.auto_open_chat || startup.completed)
      return;
    auto* preview = preview_handle.Get<ChatPreviewController>();
    const bool station = section == SectionID::Starbase_Interior || section == SectionID::Starbase_Exterior;
    // Section completion is set by the game after its visibility refresh.
    const bool section_ready = preview && preview->isSectionChangedCompleted();
    const bool context_ready = manager->isContextInitialised();
    const bool side_allowed = manager->IsSideChatAllowed();
    const bool ready = station && section_ready && context_ready && side_allowed;
    // Log changes only, so a pending startup identifies its exact blocking gate.
    const int status = (station ? 1 : 0) | (preview ? 2 : 0) | (section_ready ? 4 : 0)
                       | (context_ready ? 8 : 0) | (side_allowed ? 16 : 0) | (side_open ? 32 : 0);
    static int last_status = -1;
    static SectionID last_section = SectionID::AppInit;
    if (status != last_status || section != last_section) {
      last_status = status;
      last_section = section;
      spdlog::info("[ChatOptions] startup section={} station={} preview={} section_ready={} context_ready={} "
                   "side_allowed={} side_open={}", static_cast<int>(section), station, preview != nullptr,
                   section_ready, context_ready, side_allowed, side_open);
    }
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (startup.Tick(ready, side_open || Hub::IsInChat(), now)) {
      spdlog::info("[ChatOptions] readiness settled; opening alliance side chat");
      manager->OpenChannel(ChatChannelCategory::Alliance, ChatViewMode::Side);
    } else if (startup.completed) {
      spdlog::info("[ChatOptions] startup consumed by existing/manual chat");
    }
  } catch (const std::exception& error) {
    startup_failed = true;
    spdlog::warn("[ChatOptions] disabled after metadata/invocation failure: {}", error.what());
  }
}

// Tab positions in both the full-screen chat and the preview. Cadet and veil chat only exist for some players,
// and shift the tabs after them; a missing tab is -1.
struct ChatTabs {
  int32_t cadet    = -1;
  int32_t galaxy   = 0;
  int32_t veil     = -1;
  int32_t alliance = 1;

  static ChatTabs Current()
  {
    ChatTabs tabs;
    auto*    chat = ChatManager::Instance();
    if (!chat)
      return tabs;
    const bool cadet = chat->CanSeeNewbieChat();
    const bool veil  = chat->CanSeeRegionalChat();
    tabs.cadet       = cadet ? 0 : -1;
    tabs.galaxy      = cadet ? 1 : 0;
    tabs.veil        = veil ? tabs.galaxy + 1 : -1;
    tabs.alliance    = tabs.galaxy + 1 + (veil ? 1 : 0);
    return tabs;
  }

  // Whether `index` is a tab the settings disable.
  bool Disabled(int32_t index) const
  {
    const auto& ui = Config::Get().ui;
    return (ui.disable_galaxy_chat && index == galaxy) || (ui.disable_veil_chat && veil != -1 && index == veil);
  }
};

bool AnyDisabled()
{ return Config::Get().ui.disable_galaxy_chat || Config::Get().ui.disable_veil_chat; }

void FullScreenChatViewController_AboutToShow_Hook(auto original, FullScreenChatViewController* _this)
{
  original(_this);
  if (!AnyDisabled())
    return;

  auto* tab_bar_view = _this->categoriesTabBarViewController();
  auto* tab_bar      = tab_bar_view ? tab_bar_view->tabBar() : nullptr;
  auto* buttons      = tab_bar ? tab_bar->data() : nullptr;
  if (!buttons)
    return;
  const auto tabs = ChatTabs::Current();
  for (int32_t index : {tabs.galaxy, tabs.veil})
    if (index >= 0 && index < buttons->Count() && tabs.Disabled(index))
      if (auto* button = (*buttons)[index])
        static_cast<GenericButtonContext*>(button)->SetInteractable(false);
}

// Selecting a disabled tab (with the keyboard, say) does nothing.
void FullScreenChatViewController_OnDidChangeSelectedTab_Hook(auto original, FullScreenChatViewController* _this,
                                                              int32_t index, void* tab)
{
  if (!ChatTabs::Current().Disabled(index))
    original(_this, index, tab);
}

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
  auto*      scroller = _this ? _this->swipeScroller() : nullptr;
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

void InstallChat()
{
  spdlog::info("[ChatOptions] lifecycle v2 registered: auto_open={} width={}", Config::Get().ui.auto_open_chat,
               Config::Get().ui.default_chat_width);
  IL2CPP_HOOK(FullScreenChatViewController::Class::Method("AboutToShow"),
              FullScreenChatViewController_AboutToShow_Hook);
  IL2CPP_HOOK(FullScreenChatViewController::Class::Method("OnDidChangeSelectedTab"),
              FullScreenChatViewController_OnDidChangeSelectedTab_Hook);
  IL2CPP_HOOK(ChatPreviewController::Class::Method("AboutToShow"), ChatPreviewController_AboutToShow_Hook);
  IL2CPP_HOOK(ChatPreviewController::Class::Method("OnPanelFocused"), ChatPreviewController_OnPanelFocused_Hook);
  IL2CPP_HOOK(ChatPreviewController::Class::Method("OnGlobalMessageReceived"),
              ChatPreviewController_OnGlobalMessageReceived_Hook);
  IL2CPP_HOOK(ChatPreviewController::Class::Method("OnRegionalMessageReceived"),
              ChatPreviewController_OnRegionalMessageReceived_Hook);
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
    auto* view  = unity::Newest<FullScreenChatViewController>();
    auto* list  = view ? view->messageList() : nullptr;
    auto* input = list ? list->inputField() : nullptr;
    if (input)
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
  for (auto [shortcut, channel] : {std::pair{Action::kSelectChatGlobal, ChatChannelCategory::Global},
                                   std::pair{Action::kSelectChatAlliance, ChatChannelCategory::Alliance},
                                   std::pair{Action::kSelectChatPrivate, ChatChannelCategory::Private}}) {
    if (Shortcut::IsDown(shortcut)) {
      chat->OpenChannel(channel);
      return true;
    }
  }
  return false;
}

const Feature kChat{"Chat", &Config::Patches::chatpatches, InstallChat, UpdateChatOptions};

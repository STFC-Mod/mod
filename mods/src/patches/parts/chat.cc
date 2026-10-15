#include "prime/chat.h"
#include "prime/ui.h"

#include "config.h"

#include <spud/detour.h>
#include <tuple>
#include <chrono>
#include <cmath>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include "patches/chat_startup.h"
#include "patches/screen_update_hook.h"
#include "prime/Hub.h"

namespace
{
il2cpp::GcHandle preview_handle;
ChatStartup::Gate startup;
bool width_applied = false;
bool startup_failed = false;

void ObserveChatPreview(ChatPreviewController* preview)
{
  if (!Config::Get().auto_open_chat)
    return;
  if (preview_handle && preview_handle.Get() == preview)
    return;
  preview_handle = il2cpp::GcHandle::Weak(preview);
  startup.ready_since = -1;
  spdlog::info("[ChatOptions] observed chat preview");
}

void UpdateChatOptions()
{
  const float configured_width = Config::Get().default_chat_width;
  const bool custom_width = std::isfinite(configured_width) && configured_width > 0;
  if (startup_failed || (!Config::Get().auto_open_chat && !custom_width))
    return;
  try {
    auto* sections = Hub::get_SectionManager();
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
    if ((!Config::Get().auto_open_chat || startup.completed) && (!custom_width || width_applied))
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
    if (!Config::Get().auto_open_chat || startup.completed)
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
} // namespace

auto GetChatTabIndices()
{
  if (const auto chat_manager = ChatManager::Instance(); chat_manager) {
    const auto hasCadetChat    = chat_manager->CanSeeNewbieChat();
    const auto hasVeilChat     = chat_manager->CanSeeRegionalChat();

    const auto galaxyChatIndex = 0 + (hasCadetChat ? 1 : 0);
    const auto veilChatIndex   = galaxyChatIndex + 1;
    const auto allianceChatIndex = (hasCadetChat ? 1 : 0) + 1 + (hasVeilChat ? 1 : 0);

    return std::make_tuple(hasCadetChat ? 0 : -1, galaxyChatIndex, hasVeilChat ? veilChatIndex : -1, allianceChatIndex);
  }

  return std::make_tuple(-1, 0, -1, 1);
}

void DisableButtons(FullScreenChatViewController* _this)
{
  const auto disableGalaxyChat = Config::Get().disable_galaxy_chat;
  const auto disableVeilChat = Config::Get().disable_veil_chat;

  if (!(disableGalaxyChat || disableVeilChat))
    return;

  const auto [cadetChatIdx, galaxyChatIdx, veilChatIdx, allianceChatIdx] = GetChatTabIndices();

  const auto viewController = _this->categoriesTabBarViewController();
  if (!viewController) {
    return;
  }

  const auto tabBar = viewController->tabBar();
  if (!tabBar) {
    return;
  }

  const auto listData = tabBar->data();
  if (!listData) {
    return;
  }

  if (disableGalaxyChat) {
    if (const auto elm = (*listData)[galaxyChatIdx]) {
      static_cast<GenericButtonContext*>(elm)->SetInteractable(false);
    }
  }

  if (disableVeilChat && veilChatIdx != -1) {
    if (const auto elm = (*listData)[veilChatIdx]) {
      static_cast<GenericButtonContext*>(elm)->SetInteractable(false);
    }
  }
}

void FullScreenChatViewController_AboutToShow(auto original, FullScreenChatViewController* _this)
{
  original(_this);
  DisableButtons(_this);
}

void FullScreenChatViewController_OnDidChangeSelectedTab(auto original, FullScreenChatViewController* _this, int32_t tabIdx, void* tab)
{
  const auto [cadetChatIdx, galaxyChatIdx, veilChatIdx, allianceChatIdx] = GetChatTabIndices();
  if ((tabIdx == galaxyChatIdx && Config::Get().disable_galaxy_chat) || (tabIdx == veilChatIdx && Config::Get().disable_veil_chat)) {
    // don't show disabled chats if the associated tab was selected
    return;
  }

  original(_this, tabIdx, tab);
}

void ChatPreviewController_AboutToShow(auto original, ChatPreviewController* _this)
{
  original(_this);

  ObserveChatPreview(_this);

  if (Config::Get().disable_galaxy_chat || Config::Get().disable_veil_chat) {
    const auto allianceChatIdx = std::get<3>(GetChatTabIndices());
    _this->SetFocusedPanel(ChatChannelCategory::Alliance);

    if (_this->swipeScroller()->currentContentIndex() != allianceChatIdx) {
      _this->swipeScroller()->FocusOnInstantly(allianceChatIdx);
    }
  }
}

void ChatPreviewController_OnPanelFocused(auto original, ChatPreviewController* _this, int32_t index)
{
  ObserveChatPreview(_this);
  static const auto disableGalaxyChat = Config::Get().disable_galaxy_chat;
  static const auto disableVeilChat   = Config::Get().disable_veil_chat;

  if (!(disableGalaxyChat || disableVeilChat)) {
    original(_this, index);
    return;
  }

  const auto [cadetChatIdx, galaxyChatIdx, veilChatIdx, allianceChatIdx] = GetChatTabIndices();
  if (disableGalaxyChat || (veilChatIdx != -1 && disableVeilChat)) {
    _this->SetFocusedPanel(ChatChannelCategory::Alliance);
    original(_this, allianceChatIdx);

    if (_this->swipeScroller()->currentContentIndex() != allianceChatIdx) {
      _this->swipeScroller()->FocusOnInstantly(allianceChatIdx);
    }

    return;
  }

  original(_this, index);
}

void ChatPreviewController_OnGlobalMessageReceived(auto original, ChatPreviewController* _this, void* message)
{
  if (Config::Get().disable_galaxy_chat)
    return;

  original(_this, message);
}

void ChatPreviewController_OnRegionalMessageReceived(auto original, ChatPreviewController* _this, void* message)
{
  if (Config::Get().disable_veil_chat)
    return;

  original(_this, message);
}

void InstallChatPatches()
{
  if (install_screen_manager_update_hook() && register_screen_manager_update_callback(UpdateChatOptions))
    spdlog::info("[ChatOptions] lifecycle v2 registered: auto_open={} width={}",
                 Config::Get().auto_open_chat, Config::Get().default_chat_width);
  if (auto m = FullScreenChatViewController::Class::Method("AboutToShow"))
    SPUD_STATIC_DETOUR(m.Pointer(), FullScreenChatViewController_AboutToShow);
  if (auto m = FullScreenChatViewController::Class::Method("OnDidChangeSelectedTab"))
    SPUD_STATIC_DETOUR(m.Pointer(), FullScreenChatViewController_OnDidChangeSelectedTab);

  if (auto m = ChatPreviewController::Class::Method("AboutToShow"))
    SPUD_STATIC_DETOUR(m.Pointer(), ChatPreviewController_AboutToShow);
  if (auto m = ChatPreviewController::Class::Method("OnPanelFocused"))
    SPUD_STATIC_DETOUR(m.Pointer(), ChatPreviewController_OnPanelFocused);
  if (auto m = ChatPreviewController::Class::Method("OnGlobalMessageReceived"))
    SPUD_STATIC_DETOUR(m.Pointer(), ChatPreviewController_OnGlobalMessageReceived);
  if (auto m = ChatPreviewController::Class::Method("OnRegionalMessageReceived"))
    SPUD_STATIC_DETOUR(m.Pointer(), ChatPreviewController_OnRegionalMessageReceived);
}

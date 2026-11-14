#include "patches/patches.h"
#include "prime/chat.h"
#include <unity/core.h>

#include "config.h"
#include "patches/native_hook_extent.h"

#include <il2cpp/hook.h>

#include <spdlog/spdlog.h>

namespace
{
bool IsChatRow(void* profile_widget)
{
  auto* mb = (Il2CppObject*)profile_widget;
  if (!mb) {
    return false;
  }

  auto* transform = reinterpret_cast<il2cpp::Object*>(mb)->As<unity::Component>()->transform();
  if (!transform) {
    return false;
  }

  for (int hops = 0; hops < 32 && transform; ++hops) {
    auto* gameObject = transform->gameObject();
    if (!gameObject) {
      return false;
    }
    if (reinterpret_cast<unity::GameObject*>(gameObject)->GetComponent<ChatMessageWidget>()) {
      return true;
    }
    transform = transform->parent();
  }
  return false;
}
void HideTitleRow(void* title)
{
  auto* game_object = reinterpret_cast<unity::GameObject*>(
      reinterpret_cast<il2cpp::Object*>(title)->As<unity::Component>()->gameObject());
  if (!game_object) {
    return;
  }
  game_object->SetActive(false);
}

void ApplyTitleDisplay(void* profile_widget)
{
  auto* widget = static_cast<UserProfileWidget*>(profile_widget);
  if (!widget) {
    return;
  }

  const auto show  = Config::Get().ui.show_player_title;
  auto*      title = widget->playerTitleWidget();
  if (show || title == nullptr) {
    return;
  }

  if (!IsChatRow(widget)) {
    return;
  }

  HideTitleRow(title);
}
} // namespace

void UserProfileWidget_UpdatePlayerTitle_Hook(auto original, void* _this)
{
  original(_this);
  try {
    ApplyTitleDisplay(_this);
  } catch (...) {
  }
}
void PlayerTitleWidget_BindPath_Hook(auto original, void* _this)
{
  original(_this);
  try {
    if (Config::Get().ui.show_player_title) {
      return;
    }
    UserProfileWidget* owner     = nullptr;
    auto*              transform = reinterpret_cast<il2cpp::Object*>(_this)->As<unity::Component>()->transform();
    for (int hops = 0; hops < 32 && transform && !owner; ++hops) {
      auto* gameObject = reinterpret_cast<unity::GameObject*>(transform->gameObject());
      if (gameObject) {
        owner = gameObject->GetComponent<UserProfileWidget>();
      }
      transform = transform->parent();
    }
    if (owner) {
      ApplyTitleDisplay(owner);
    }
  } catch (...) {
  }
}

namespace
{
#if __APPLE__
void* ValidateDetourTarget(void* entry, const char* name)
{
  if (native_hooks::MacHookFits(entry, 16)) {
    return entry;
  }
#if defined(__aarch64__)
  if (const auto* downstream = native_hooks::MacFollowDirectBranch(entry)) {
    if (native_hooks::MacHookFits(downstream, 16)) {
      return const_cast<void*>(downstream);
    }
  }
#endif
  spdlog::warn("[ChatTitleDisplay] {} does not fit the macOS hook window; target skipped", name);
  return nullptr;
}
#else
void* ValidateDetourTarget(void* entry, const char* name)
{
  (void)name;
  return entry;
}
#endif
} // namespace

namespace
{
void InstallChatTitleDisplayHooks()
{
  void* update_player_title = UserProfileWidget::Class::Method("UpdatePlayerTitle", 0).Pointer();
  void* set_widget_data     = PlayerTitleWidget::Class::Method("SetWidgetData", 0).Pointer();
  void* on_did_bind_context = PlayerTitleWidget::Class::Method("OnDidBindContext", 0).Pointer();
  if (!set_widget_data || !on_did_bind_context) {
    set_widget_data     = nullptr;
    on_did_bind_context = nullptr;
  }

  if (!update_player_title && !set_widget_data && !on_did_bind_context) {
    return;
  }

  if (update_player_title) {
    update_player_title = ValidateDetourTarget(update_player_title, "UpdatePlayerTitle");
  }
  if (set_widget_data) {
    set_widget_data = ValidateDetourTarget(set_widget_data, "SetWidgetData");
  }
  if (on_did_bind_context) {
    on_did_bind_context = ValidateDetourTarget(on_did_bind_context, "OnDidBindContext");
  }

  if (!update_player_title && !set_widget_data && !on_did_bind_context) {
    spdlog::warn("Skipping ChatTitleDisplayHooks: no detour target fits; no hooks installed.");
    return;
  }

  // The PlayerTitleWidget callbacks can resolve through thunks to one shared
  // native implementation. A second detour on the same address would rebuild
  // the first detour's jump into a broken trampoline, so keep one hook per
  // target; the shared body funnels both method entries into the same hook.
  if (set_widget_data && set_widget_data == update_player_title) {
    spdlog::info("[ChatTitleDisplay] SetWidgetData shares its native implementation with UpdatePlayerTitle; "
                 "keeping one detour");
    set_widget_data = nullptr;
  }
  if (on_did_bind_context && (on_did_bind_context == update_player_title || on_did_bind_context == set_widget_data)) {
    spdlog::info("[ChatTitleDisplay] OnDidBindContext shares its native implementation with another target; "
                 "keeping one detour");
    on_did_bind_context = nullptr;
  }

  if (update_player_title) {
    IL2CPP_DETOUR(update_player_title, UserProfileWidget_UpdatePlayerTitle_Hook,
                  "UserProfileWidget_UpdatePlayerTitle_Hook");
  }
  if (set_widget_data) {
    IL2CPP_DETOUR(set_widget_data, PlayerTitleWidget_BindPath_Hook, "PlayerTitleWidget_BindPath_Hook");
  }
  if (on_did_bind_context) {
    IL2CPP_DETOUR(on_did_bind_context, PlayerTitleWidget_BindPath_Hook, "PlayerTitleWidget_BindPath_Hook");
  }

  spdlog::info("Chat title display: chat player title rows {}",
               Config::Get().ui.show_player_title ? "shown" : "hidden");
}
} // namespace

const Feature kChatTitles{"ChatTitles", &Config::Patches::chattitledisplayhooks, InstallChatTitleDisplayHooks};

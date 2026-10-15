#include "prime/ChatMessageWidget.h"
#include "prime/GameObject.h"
#include "prime/UserProfileWidget.h"

#include "config.h"
#include "errormsg.h"
#include "patches/native_hook_extent.h"

#include <il2cpp/il2cpp-functions.h>
#include <il2cpp/il2cpp_helper.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace
{
Il2CppObject* InvokeSelf(Il2CppObject* object, const char* method_name)
{
  auto* method = il2cpp_class_get_method_from_name(object ? il2cpp_object_get_class(object) : nullptr, method_name, 0);
  if (!method) {
    return nullptr;
  }
  Il2CppException* exception = nullptr;
  return (Il2CppObject*)il2cpp_runtime_invoke(method, object, nullptr, &exception);
}
bool IsChatRow(void* profile_widget)
{
  auto* mb = (Il2CppObject*)profile_widget;
  if (!mb) {
    return false;
  }

  auto* transform = InvokeSelf(mb, "get_transform");
  if (!transform) {
    return false;
  }

  for (int hops = 0; hops < 32 && transform; ++hops) {
    auto* gameObject = InvokeSelf(transform, "get_gameObject");
    if (!gameObject) {
      return false;
    }
    if (reinterpret_cast<GameObject*>(gameObject)->GetComponent<ChatMessageWidget>()) {
      return true;
    }
    transform = InvokeSelf(transform, "get_parent");
  }
  return false;
}
void HideTitleRow(void* title)
{
  auto* game_object = reinterpret_cast<GameObject*>(InvokeSelf(static_cast<Il2CppObject*>(title), "get_gameObject"));
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

  const auto show  = Config::Get().show_player_title;
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
    if (Config::Get().show_player_title) {
      return;
    }
    UserProfileWidget* owner     = nullptr;
    auto*              transform = InvokeSelf((Il2CppObject*)_this, "get_transform");
    for (int hops = 0; hops < 32 && transform && !owner; ++hops) {
      auto* gameObject = reinterpret_cast<GameObject*>(InvokeSelf(transform, "get_gameObject"));
      if (gameObject) {
        owner = gameObject->GetComponent<UserProfileWidget>();
      }
      transform = InvokeSelf(transform, "get_parent");
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

void InstallChatTitleDisplayHooks()
{
  void* update_player_title = nullptr;
  if (auto profile_widget =
          il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PlayerProfile", "UserProfileWidget");
      !profile_widget.isValidHelper()) {
    spdlog::warn("Missing class {}.{}", "Digit.Prime.PlayerProfile", "UserProfileWidget");
  } else if ((update_player_title = profile_widget.GetMethod("UpdatePlayerTitle", 0)) == nullptr) {
    spdlog::warn("Missing method {}.{}", "UserProfileWidget", "UpdatePlayerTitle");
  }

  void* set_widget_data     = nullptr;
  void* on_did_bind_context = nullptr;
  if (auto title_widget = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PlayerAvatars", "PlayerTitleWidget");
      !title_widget.isValidHelper()) {
    spdlog::warn("Missing class {}.{}", "Digit.Prime.PlayerAvatars", "PlayerTitleWidget");
  } else {
    set_widget_data     = title_widget.GetMethod("SetWidgetData", 0);
    on_did_bind_context = title_widget.GetMethod("OnDidBindContext", 0);
    if (!set_widget_data || !on_did_bind_context) {
      spdlog::warn("Missing method {}.{}", "PlayerTitleWidget", set_widget_data ? "OnDidBindContext" : "SetWidgetData");
      set_widget_data     = nullptr;
      on_did_bind_context = nullptr;
    }
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
  if (on_did_bind_context
      && (on_did_bind_context == update_player_title || on_did_bind_context == set_widget_data)) {
    spdlog::info("[ChatTitleDisplay] OnDidBindContext shares its native implementation with another target; "
                 "keeping one detour");
    on_did_bind_context = nullptr;
  }

  if (update_player_title) {
    SPUD_STATIC_DETOUR(update_player_title, UserProfileWidget_UpdatePlayerTitle_Hook);
  }
  if (set_widget_data) {
    SPUD_STATIC_DETOUR(set_widget_data, PlayerTitleWidget_BindPath_Hook);
  }
  if (on_did_bind_context) {
    SPUD_STATIC_DETOUR(on_did_bind_context, PlayerTitleWidget_BindPath_Hook);
  }

  spdlog::info("Chat title display: chat player title rows {}", Config::Get().show_player_title ? "shown" : "hidden");
}

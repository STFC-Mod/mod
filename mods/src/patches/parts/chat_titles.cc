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
    static int missing_reports = 0;
    if (missing_reports++ < 8) {
      spdlog::warn("[ChatTitleDisplay] Cannot resolve {} on {}", method_name,
                   object ? il2cpp_class_get_name(il2cpp_object_get_class(object)) : "null");
    }
    return nullptr;
  }
  Il2CppException* exception = nullptr;
  auto* result = (Il2CppObject*)il2cpp_runtime_invoke(method, object, nullptr, &exception);
  if (exception) {
    static int exception_reports = 0;
    if (exception_reports++ < 8) {
      spdlog::warn("[ChatTitleDisplay] Managed invocation failed: {}", method_name);
    }
    return nullptr;
  }
  return result;
}
Il2CppObject* FindChatRowTransform(void* profile_widget)
{
  auto* mb = (Il2CppObject*)profile_widget;
  if (!mb) {
    return nullptr;
  }

  auto* transform = InvokeSelf(mb, "get_transform");
  if (!transform) {
    return nullptr;
  }

  for (int hops = 0; hops < 32 && transform; ++hops) {
    auto* gameObject = InvokeSelf(transform, "get_gameObject");
    if (!gameObject) {
      return nullptr;
    }
    if (reinterpret_cast<GameObject*>(gameObject)->GetComponentFastPath2<ChatMessageWidget>()) {
      return transform;
    }
    transform = InvokeSelf(transform, "get_parent");
  }
  return nullptr;
}
struct TitleLayoutElement {
  static IL2CppClassHelper& get_class_helper()
  {
    static auto helper = il2cpp_get_class_helper("UnityEngine.UI", "UnityEngine.UI", "LayoutElement");
    return helper;
  }
};

void HideTitleRow(void* title, Il2CppObject* row_transform)
{
  auto* game_object = reinterpret_cast<GameObject*>(InvokeSelf(static_cast<Il2CppObject*>(title), "get_gameObject"));
  if (!game_object) {
    return;
  }
  game_object->SetActive(false);

  // In the chat prefab, _container is the enclosing PlayerTitleContainer, whose
  // LayoutElement reserves 24 units even when the title widget child is hidden.
  // Resolve the serialized reference rather than assuming a particular parent.
  static auto* container_field =
      il2cpp_class_get_field_from_name(il2cpp_object_get_class(static_cast<Il2CppObject*>(title)), "_container");
  GameObject* container = nullptr;
  if (container_field) {
    il2cpp_field_get_value(static_cast<Il2CppObject*>(title), container_field, &container);
  }
  if (container) {
    static int container_reports = 0;
    if (container_reports++ < 8) {
      spdlog::info("[ChatTitleDisplay] Hiding title '{}' and container '{}' (active={})", game_object->Name(),
                   container->Name(), container->activeInHierarchy);
    }
    // The native bind path may reactivate the container after a nested title
    // callback. Excluding its LayoutElement keeps that empty row out of layout.
    if (auto* element = container->GetComponentFastPath2<TitleLayoutElement>()) {
      static auto ignore_layout = TitleLayoutElement::get_class_helper().GetMethodInfo("set_ignoreLayout", 1);
      if (ignore_layout) {
        bool             ignore    = true;
        void*            args[]    = {&ignore};
        Il2CppException* exception = nullptr;
        il2cpp_runtime_invoke(ignore_layout, element, args, &exception);
        if (exception) {
          spdlog::warn("[ChatTitleDisplay] Cannot exclude title container from layout");
        }
      }
    }
    container->SetActive(false);
  } else {
    static bool reported = false;
    if (!reported) {
      reported = true;
      spdlog::warn("[ChatTitleDisplay] Title '{}' has no container (field found={})", game_object->Name(),
                   container_field != nullptr);
    }
  }

  // Rebuild the complete message row before the virtualized list measures it;
  // marking only the inner text layout leaves the enclosing row size cached.
  static auto layout  = il2cpp_get_class_helper("UnityEngine.UI", "UnityEngine.UI", "LayoutRebuilder");
  static auto rebuild = layout.GetMethodInfo("ForceRebuildLayoutImmediate", 1);
  static auto mark    = layout.GetMethodInfo("MarkLayoutForRebuild", 1);
  for (auto* method : {rebuild, mark}) {
    if (method) {
      void*            args[]    = {row_transform};
      Il2CppException* exception = nullptr;
      il2cpp_runtime_invoke(method, nullptr, args, &exception);
      if (exception) {
        static bool reported = false;
        if (!reported) {
          reported = true;
          spdlog::warn("[ChatTitleDisplay] Chat row layout rebuild failed");
        }
      }
    }
  }
}

void ApplyTitleDisplay(void* profile_widget)
{
  auto* widget = static_cast<UserProfileWidget*>(profile_widget);
  if (!widget) {
    return;
  }

  const auto show  = Config::Get().show_player_title;
  auto*      title = widget->_playerTitleWidget;
  static bool reported_callback = false;
  if (!reported_callback) {
    reported_callback = true;
    spdlog::info("[ChatTitleDisplay] Profile callback reached (show={}, title present={})", show, title != nullptr);
  }
  if (show || title == nullptr) {
    return;
  }

  auto* row_transform = FindChatRowTransform(widget);
  if (!row_transform) {
    static bool reported = false;
    if (!reported) {
      reported = true;
      spdlog::info("[ChatTitleDisplay] Profile callback excluded: no ChatMessageWidget ancestor");
    }
    return;
  }

  HideTitleRow(title, row_transform);
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
void PlayerTitleWidget_SetIdentifiers_Hook(auto original, void* _this)
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
        owner = gameObject->GetComponentFastPath2<UserProfileWidget>();
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
    ErrorMsg::MissingHelper("Digit.Prime.PlayerProfile", "UserProfileWidget");
  } else if ((update_player_title = profile_widget.GetMethod("UpdatePlayerTitle", 0)) == nullptr) {
    ErrorMsg::MissingMethod("UserProfileWidget", "UpdatePlayerTitle");
  }

  void* set_identifiers = nullptr;
  if (auto title_widget = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PlayerAvatars", "PlayerTitleWidget");
      !title_widget.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.PlayerAvatars", "PlayerTitleWidget");
  } else if ((set_identifiers = title_widget.GetMethod("SetIdentifiers", 0)) == nullptr) {
    ErrorMsg::MissingMethod("PlayerTitleWidget", "SetIdentifiers");
  }

  if (!update_player_title && !set_identifiers) {
    return;
  }

  if (update_player_title) {
    update_player_title = ValidateDetourTarget(update_player_title, "UpdatePlayerTitle");
  }
  if (set_identifiers) {
    set_identifiers = ValidateDetourTarget(set_identifiers, "SetIdentifiers");
  }

  if (!update_player_title && !set_identifiers) {
    spdlog::warn("Skipping ChatTitleDisplayHooks: no detour target fits; no hooks installed.");
    return;
  }

  // Both title-bind callbacks tail-call SetIdentifiers. On Windows client 272
  // their shared wrapper is only 16 bytes, less than SPUD's 24-byte overwrite,
  // so detouring it also corrupts the adjacent title-clear callback. Hook the
  // substantive implementation once instead.
  if (set_identifiers && set_identifiers == update_player_title) {
    spdlog::warn("[ChatTitleDisplay] SetIdentifiers shares its target with UpdatePlayerTitle; keeping one detour");
    set_identifiers = nullptr;
  }

  if (update_player_title) {
    SPUD_STATIC_DETOUR(update_player_title, UserProfileWidget_UpdatePlayerTitle_Hook);
  }
  if (set_identifiers) {
    SPUD_STATIC_DETOUR(set_identifiers, PlayerTitleWidget_SetIdentifiers_Hook);
  }

  spdlog::info("Chat title display: chat player title rows {} (immediate row layout rebuild enabled)",
               Config::Get().show_player_title ? "shown" : "hidden");
}

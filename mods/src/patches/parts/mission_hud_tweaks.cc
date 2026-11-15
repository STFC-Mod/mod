#include "config.h"
#include "errormsg.h"

#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
struct MissionHudButtonDefinition {
  const char*          canonical_name;
  const char*          field_name;
  MissionHudVisibility visibility   = MissionHudVisibility::Auto;
  ptrdiff_t            field_offset = 0;
  bool                 field_valid  = false;
};

using ComponentGetGameObjectFn = void* (*)(void*);
using GameObjectSetActiveFn    = void (*)(void*, bool);

std::array<MissionHudButtonDefinition, 4> g_button_definitions{{
    {"q_trials", "_challengesButton"},
    {"field_training", "_achievementsButton"},
    {"outposts", "_outpostsButton"},
    {"missions", "_missionsButton"},
}};

std::vector<MissionHudButtonDefinition*> g_configured_buttons;
ComponentGetGameObjectFn                 g_get_game_object = nullptr;
GameObjectSetActiveFn                    g_set_active      = nullptr;
bool g_ready = false;

std::string_view to_string(MissionHudVisibility visibility)
{
  switch (visibility) {
    case MissionHudVisibility::Always:
      return "always";
    case MissionHudVisibility::Never:
      return "never";
    case MissionHudVisibility::Auto:
    default:
      return "auto";
  }
}

std::vector<MissionHudButtonDefinition*> LoadConfiguredButtons()
{
  std::vector<MissionHudButtonDefinition*> configured_buttons;
  for (auto& definition : g_button_definitions) {
    definition.visibility = Config::Get().MissionHudButtonVisibility(definition.canonical_name);
    configured_buttons.emplace_back(&definition);
  }
  return configured_buttons;
}

std::string ConfiguredButtonModes()
{
  std::string modes;
  for (const auto* button : g_configured_buttons) {
    if (button->visibility == MissionHudVisibility::Auto) {
      continue;
    }
    if (!modes.empty()) {
      modes.append(", ");
    }
    modes.append(button->canonical_name);
    modes.append("=");
    modes.append(to_string(button->visibility));
  }
  return modes;
}

bool ResolveButtonFields(IL2CppClassHelper& controller_helper, Il2CppClass* component_class)
{
  auto valid_count = 0;
  for (auto* button : g_configured_buttons) {
    auto field = controller_helper.GetField(button->field_name);
    auto* info = field.get_info();
    const auto* type = info ? info->type : nullptr;
    auto* field_class = type ? il2cpp_class_from_type(type) : nullptr;
    if (!info || !type || type->byref || (il2cpp_field_get_flags(info) & FIELD_ATTRIBUTE_STATIC)
        || (type->type != IL2CPP_TYPE_CLASS && type->type != IL2CPP_TYPE_GENERICINST)
        || !field_class || !il2cpp_class_is_assignable_from(component_class, field_class)
        || info->offset < sizeof(Il2CppObject)
        || info->offset + sizeof(void*) > il2cpp_class_instance_size(controller_helper.get_cls())) {
      spdlog::error("MissionHudTweaks: unable to find MissionsHudViewController field '{}'", button->field_name);
      continue;
    }

    button->field_offset = field.offset();
    button->field_valid  = true;
    valid_count++;
    spdlog::info("MissionHudTweaks: mapped {} -> {}", button->canonical_name, button->field_name);
  }

  return valid_count > 0;
}

void ApplyButtonVisibility(void* controller, const MissionHudButtonDefinition& button)
{
  if (!controller || !button.field_valid || !g_get_game_object || !g_set_active) {
    return;
  }

  auto* component = *reinterpret_cast<void**>(reinterpret_cast<char*>(controller) + button.field_offset);
  if (!component) {
    return;
  }

  if (auto* game_object = g_get_game_object(component)) {
    g_set_active(game_object, button.visibility == MissionHudVisibility::Always);
  }
}
} // namespace

// Visibility is now refreshed through several independent paths. Keep the game's
// setup/notification work, then apply only explicitly configured overrides.
void ApplyConfiguredButtonVisibility(void* controller)
{
  if (!g_ready)
    return;
  for (auto* button : g_configured_buttons) {
    button->visibility = Config::Get().MissionHudButtonVisibility(button->canonical_name);
    if (button->visibility != MissionHudVisibility::Auto)
      ApplyButtonVisibility(controller, *button);
  }
}

void MissionsHudViewController_OnEnable_Hook(auto original, void* controller)
{
  original(controller);
  ApplyConfiguredButtonVisibility(controller);
}

void MissionsHudViewController_SetupAchievementsButton_Hook(auto original, void* controller)
{
  original(controller);
  ApplyConfiguredButtonVisibility(controller);
}

void MissionsHudViewController_SetupChallengesButton_Hook(auto original, void* controller, bool replace_with_outposts)
{
  original(controller, replace_with_outposts);
  ApplyConfiguredButtonVisibility(controller);
}

void MissionsHudViewController_SetupOutpostsButton_Hook(auto original, void* controller, bool show_outposts)
{
  original(controller, show_outposts);
  ApplyConfiguredButtonVisibility(controller);
}

void MissionsHudViewController_HandleOutpostsAndChallengesHUD_Hook(auto original, void* controller)
{
  // In planetary view this method hides challenges directly, bypassing its setup
  // method. Reapply after the whole operation, including the planetary branch.
  original(controller);
  ApplyConfiguredButtonVisibility(controller);
}

void InstallMissionHudTweaksHooks()
{
  g_configured_buttons = LoadConfiguredButtons();

  auto controller_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.HUD", "MissionsHudViewController");
  if (!controller_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.HUD", "MissionsHudViewController");
    return;
  }

  auto component_helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Component");
  if (!component_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("UnityEngine", "Component");
    return;
  }

  if (!ResolveButtonFields(controller_helper, component_helper.get_cls()))
    return;
  const auto* get_game_object = method_contract::Resolve(
      component_helper.get_cls(), "get_gameObject", false, "UnityEngine.GameObject", {});
  g_get_game_object = reinterpret_cast<ComponentGetGameObjectFn>(method_contract::Pointer(get_game_object));
  if (!g_get_game_object) {
    ErrorMsg::MissingMethod("Component", "get_gameObject");
    return;
  }

  auto game_object_helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "GameObject");
  if (!game_object_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("UnityEngine", "GameObject");
    return;
  }

  const auto* set_active = method_contract::Resolve(
      game_object_helper.get_cls(), "SetActive", false, "System.Void", {"System.Boolean"});
  g_set_active = reinterpret_cast<GameObjectSetActiveFn>(method_contract::Pointer(set_active));
  if (!g_set_active) {
    ErrorMsg::MissingMethod("GameObject", "SetActive");
    return;
  }

  // Resolve the complete surface before installing anything. Avoid the tiny
  // planetary/outpost event wrappers: these substantive methods own the work.
  const auto* on_enable = method_contract::Resolve(
      controller_helper.get_cls(), "OnEnable", false, "System.Void", {});
  const auto* achievements = method_contract::Resolve(
      controller_helper.get_cls(), "SetupAchievementsButton", false, "System.Void", {});
  const auto* challenges = method_contract::Resolve(
      controller_helper.get_cls(), "SetupChallengesButton", false, "System.Void", {"System.Boolean"});
  const auto* outposts = method_contract::Resolve(
      controller_helper.get_cls(), "SetupOutpostsButton", false, "System.Void", {"System.Boolean"});
  const auto* combined = method_contract::Resolve(
      controller_helper.get_cls(), "HandleOutpostsAndChallengesHUD", false, "System.Void", {});
  const std::array targets{on_enable, achievements, challenges, outposts, combined};
  const std::array names{"OnEnable()", "SetupAchievementsButton()", "SetupChallengesButton(Boolean)",
                         "SetupOutpostsButton(Boolean)", "HandleOutpostsAndChallengesHUD()"};
  for (std::size_t i = 0; i < targets.size(); ++i) {
    if (!targets[i]) {
      ErrorMsg::MissingMethod("MissionsHudViewController", names[i]);
      return;
    }
  }
  for (std::size_t i = 0; i < targets.size(); ++i) {
    if (targets[i]->has_full_generic_sharing_signature) {
      spdlog::error("MissionHudTweaks: MissionsHudViewController.{} uses unsupported generic sharing; overrides disabled",
                     names[i]);
      return;
    }
    for (std::size_t j = 0; j < i; ++j)
      if (targets[i]->methodPointer == targets[j]->methodPointer) {
        spdlog::error("MissionHudTweaks: MissionsHudViewController.{} and {} share a native target; overrides disabled",
                       names[i], names[j]);
        return;
      }
  }
  if (get_game_object->has_full_generic_sharing_signature || set_active->has_full_generic_sharing_signature) {
    spdlog::error("MissionHudTweaks: {} uses unsupported generic sharing; overrides disabled",
                   get_game_object->has_full_generic_sharing_signature ? "Component.get_gameObject()"
                                                                       : "GameObject.SetActive(Boolean)");
    return;
  }
  const auto modes = ConfiguredButtonModes();
  if (!modes.empty())
    spdlog::info("MissionHudTweaks: applying {}", modes);
  const bool enabled = SPUD_STATIC_DETOUR(on_enable->methodPointer, MissionsHudViewController_OnEnable_Hook);
  const bool achievement_hook = SPUD_STATIC_DETOUR(achievements->methodPointer, MissionsHudViewController_SetupAchievementsButton_Hook);
  const bool challenge_hook = SPUD_STATIC_DETOUR(challenges->methodPointer, MissionsHudViewController_SetupChallengesButton_Hook);
  const bool outpost_hook = SPUD_STATIC_DETOUR(outposts->methodPointer, MissionsHudViewController_SetupOutpostsButton_Hook);
  const bool combined_hook = SPUD_STATIC_DETOUR(combined->methodPointer, MissionsHudViewController_HandleOutpostsAndChallengesHUD_Hook);
  const std::array installed{enabled, achievement_hook, challenge_hook, outpost_hook, combined_hook};
  for (std::size_t i = 0; i < installed.size(); ++i)
    if (!installed[i])
      spdlog::error("MissionHudTweaks: failed to install MissionsHudViewController.{}; overrides disabled", names[i]);
  g_ready = enabled && achievement_hook && challenge_hook && outpost_hook && combined_hook;
  if (g_ready) {
    spdlog::info("MissionHudTweaks: installed current HUD lifecycle/setup hooks");
  } else {
    // Successfully installed detours still call through, but must not partially
    // enforce preferences when another refresh path could undo them.
    g_configured_buttons.clear();
    spdlog::error("MissionHudTweaks: hook installation incomplete; overrides disabled");
  }
}

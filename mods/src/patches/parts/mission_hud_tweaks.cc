#include "config.h"
#include "errormsg.h"
#include "patches/mission_hud.h"

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

using ObjectAliveFn                  = bool (*)(void*);
using ActiveSelfFn                   = bool (*)(void*);
using RefreshFn                      = void (*)(void*);
ObjectAliveFn g_alive                = nullptr;
ActiveSelfFn  g_active_self          = nullptr;
ActiveSelfFn  g_enabled              = nullptr;
RefreshFn     g_refresh_achievements = nullptr;
RefreshFn     g_refresh_outposts     = nullptr;
bool          g_available            = false;
bool          g_refreshing           = false;
struct HudInstance {
  Il2CppGCHandle controller          = nullptr;
  Il2CppGCHandle missions            = nullptr;
  bool           missions_default    = true;
  bool           missions_overridden = false;
};
std::vector<HudInstance> g_instances;

bool Alive(void* object)
{ return object && g_alive && g_alive(object); }
void* ButtonObject(void* controller, const MissionHudButtonDefinition& button)
{
  if (!Alive(controller) || !button.field_valid)
    return nullptr;
  auto* component = *reinterpret_cast<void**>(reinterpret_cast<char*>(controller) + button.field_offset);
  return Alive(component) ? g_get_game_object(component) : nullptr;
}
bool Track(void* controller)
{
  if (!Alive(controller))
    return false;
  if (g_refreshing)
    return true;
  for (auto it = g_instances.begin(); it != g_instances.end();) {
    auto* target = il2cpp_gchandle_get_target(it->controller);
    if (!Alive(target)) {
      il2cpp_gchandle_free(it->controller);
      if (it->missions)
        il2cpp_gchandle_free(it->missions);
      it = g_instances.erase(it);
    } else {
      if (target == controller) {
        auto* missions = ButtonObject(controller, g_button_definitions[3]);
        if (!Alive(missions))
          return false;
        if (il2cpp_gchandle_get_target(it->missions) != missions) {
          const auto replacement = il2cpp_gchandle_new_weakref(static_cast<Il2CppObject*>(missions), false);
          if (!replacement)
            return false;
          il2cpp_gchandle_free(it->missions);
          it->missions            = replacement;
          it->missions_overridden = false;
          it->missions_default    = g_active_self(missions);
        } else if (!it->missions_overridden) {
          it->missions_default = g_active_self(missions);
        }
        return true;
      }
      ++it;
    }
  }
  auto* missions = ButtonObject(controller, g_button_definitions[3]);
  if (!Alive(missions))
    return false;
  const auto owner  = il2cpp_gchandle_new_weakref(static_cast<Il2CppObject*>(controller), false);
  const auto button = il2cpp_gchandle_new_weakref(static_cast<Il2CppObject*>(missions), false);
  if (owner && button) {
    g_instances.push_back({owner, button, g_active_self(missions)});
    return true;
  } else {
    if (owner)
      il2cpp_gchandle_free(owner);
    if (button)
      il2cpp_gchandle_free(button);
  }
  return false;
}

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
    if (definition.visibility == MissionHudVisibility::Auto) {
      continue;
    }

    configured_buttons.emplace_back(&definition);
  }
  return configured_buttons;
}

std::string ConfiguredButtonModes()
{
  std::string modes;
  for (const auto* button : g_configured_buttons) {
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
  for (auto& definition : g_button_definitions) {
    auto*       button      = &definition;
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

  return valid_count == g_button_definitions.size();
}

void ApplyButtonVisibility(void* controller, const MissionHudButtonDefinition& button)
{
  if (!g_available || button.visibility == MissionHudVisibility::Auto)
    return;
  if (auto* game_object = ButtonObject(controller, button); Alive(game_object)) {
    const bool desired = button.visibility == MissionHudVisibility::Always;
    if (g_active_self(game_object) != desired)
      g_set_active(game_object, desired);
  }
}
} // namespace

// Visibility is now refreshed through several independent paths. Keep the game's
// setup/notification work, then apply only explicitly configured overrides.
void ApplyConfiguredButtonVisibility(void* controller)
{
  if (!g_available)
    return;
  if (!Track(controller))
    return;
  for (auto& instance : g_instances) {
    if (il2cpp_gchandle_get_target(instance.controller) != controller)
      continue;
    if (g_button_definitions[3].visibility == MissionHudVisibility::Auto) {
      if (instance.missions_overridden) {
        auto*      missions          = il2cpp_gchandle_get_target(instance.missions);
        const bool baseline          = instance.missions_default;
        instance.missions_overridden = false;
        if (Alive(missions) && ButtonObject(controller, g_button_definitions[3]) == missions
            && g_active_self(missions) != baseline)
          g_set_active(missions, baseline);
      }
    } else {
      instance.missions_overridden = true;
    }
    break;
  }
  for (const auto* button : g_configured_buttons) {
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

  auto        object_helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Object");
  const auto* alive =
      method_contract::Resolve(object_helper.get_cls(), "op_Implicit", true, "System.Boolean", {"UnityEngine.Object"});
  const auto* active_self =
      method_contract::Resolve(game_object_helper.get_cls(), "get_activeSelf", false, "System.Boolean", {});
  auto        behaviour_helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Behaviour");
  const auto* enabled_method =
      method_contract::Resolve(behaviour_helper.get_cls(), "get_isActiveAndEnabled", false, "System.Boolean", {});
  g_alive       = reinterpret_cast<ObjectAliveFn>(method_contract::Pointer(alive));
  g_active_self = reinterpret_cast<ActiveSelfFn>(method_contract::Pointer(active_self));
  g_enabled     = reinterpret_cast<ActiveSelfFn>(method_contract::Pointer(enabled_method));
  if (!g_alive || !g_active_self || !g_enabled) {
    if (!g_alive)
      ErrorMsg::MissingMethod("UnityEngine.Object", "op_Implicit");
    if (!g_active_self)
      ErrorMsg::MissingMethod("UnityEngine.GameObject", "get_activeSelf");
    if (!g_enabled)
      ErrorMsg::MissingMethod("UnityEngine.Behaviour", "get_isActiveAndEnabled");
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
  for (const auto* method : {get_game_object, set_active, alive, active_self, enabled_method})
    if (method->has_full_generic_sharing_signature) {
      spdlog::error("MissionHudTweaks: {}.{} uses unsupported generic sharing; overrides disabled", method->klass->name,
                    method->name);
      return;
    }
  g_refresh_achievements = reinterpret_cast<RefreshFn>(achievements->methodPointer);
  g_refresh_outposts     = reinterpret_cast<RefreshFn>(combined->methodPointer);
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
  if (enabled && achievement_hook && challenge_hook && outpost_hook && combined_hook) {
    g_available = true;
    spdlog::info("MissionHudTweaks: installed live HUD lifecycle/setup hooks");
  } else {
    // Successfully installed detours still call through, but must not partially
    // enforce preferences when another refresh path could undo them.
    g_configured_buttons.clear();
    spdlog::error("MissionHudTweaks: hook installation incomplete; overrides disabled");
  }
}

namespace mission_hud
{
bool Available()
{ return g_available; }
bool CanChange()
{ return g_available && !g_refreshing; }
void Refresh()
{
  if (!g_available || g_refreshing)
    return;
  g_configured_buttons = LoadConfiguredButtons();
  struct Guard {
    Guard()
    { g_refreshing = true; }
    ~Guard()
    { g_refreshing = false; }
  } guard;
  // Weak handles are not ownership. Recheck Unity's native lifetime after every
  // managed call; refresh callbacks cannot modify the tracking collection.
  for (const auto& instance : g_instances) {
    auto* target = il2cpp_gchandle_get_target(instance.controller);
    if (!Alive(target))
      continue;
    const auto handle = il2cpp_gchandle_new(target, false);
    if (!handle)
      continue;
    struct Root {
      Il2CppGCHandle handle;
      ~Root()
      { il2cpp_gchandle_free(handle); }
    } root{handle};
    auto current = [&]() -> void* {
      auto* target = il2cpp_gchandle_get_target(root.handle);
      return Alive(target) && g_enabled(target) ? target : nullptr;
    };
    auto* controller = current();
    if (!controller)
      continue;
    g_refresh_achievements(controller);
    if (!(controller = current()))
      continue;
    g_refresh_outposts(controller);
    if (!(controller = current()))
      continue;
    ApplyConfiguredButtonVisibility(controller);
  }
}
} // namespace mission_hud

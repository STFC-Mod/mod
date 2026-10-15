#include "config.h"

#include <il2cpp/il2cpp.h>
#include <prime/unity.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
struct MissionsHudViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "MissionsHudViewController">;
};

// The buttons are a mix of Button and GenericButtonWidget fields; only their GameObject matters here.
template <il2cpp::Name Field> Component* ButtonField(MissionsHudViewController* controller)
{ return controller->Field<Field, Component*>(); }

struct MissionHudButtonDefinition {
  const char*          canonical_name;
  Component*           (*button)(MissionsHudViewController*);
  MissionHudVisibility visibility = MissionHudVisibility::Auto;
};

std::array<MissionHudButtonDefinition, 5> g_button_definitions{{
    {"q_trials", ButtonField<"_challengesButton">},
    {"field_training", ButtonField<"_achievementsButton">},
    {"outposts", ButtonField<"_outpostsButton">},
    {"daily_goals", ButtonField<"_dailyGoalsButton">},
    {"missions", ButtonField<"_missionsButton">},
}};

std::vector<MissionHudButtonDefinition*> g_configured_buttons;

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
    definition.visibility = Config::Get().ui.MissionHudButtonVisibility(definition.canonical_name);
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

void ApplyButtonVisibility(MissionsHudViewController* controller, const MissionHudButtonDefinition& button)
{
  auto* component = button.button(controller);
  if (!component) {
    return;
  }

  if (auto* game_object = component->gameObject()) {
    game_object->SetActive(button.visibility == MissionHudVisibility::Always);
  }
}
} // namespace

int32_t MissionsHudViewController_UpdateButtons_Hook(auto original, MissionsHudViewController* controller)
{
  const auto result = original(controller);

  for (auto* button : g_configured_buttons) {
    button->visibility = Config::Get().ui.MissionHudButtonVisibility(button->canonical_name);
    if (button->visibility != MissionHudVisibility::Auto) {
      ApplyButtonVisibility(controller, *button);
    }
  }

  return result;
}

void InstallMissionHudTweaksHooks()
{
  g_configured_buttons = LoadConfiguredButtons();

  auto update_buttons = MissionsHudViewController::Class::Method("UpdateButtons", 0);
  if (!update_buttons) {
    return;
  }

  const auto configured_modes = ConfiguredButtonModes();
  if (!configured_modes.empty()) {
    spdlog::info("MissionHudTweaks: applying {}", configured_modes);
  }
  SPUD_STATIC_DETOUR(update_buttons.Pointer(), MissionsHudViewController_UpdateButtons_Hook);
  spdlog::info("MissionHudTweaks: installed MissionsHudViewController.UpdateButtons hook");
}

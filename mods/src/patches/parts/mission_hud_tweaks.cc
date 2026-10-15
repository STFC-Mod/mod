// hud_* settings: forces mission HUD buttons always shown or never shown, overriding the game's own rules.

#include "config.h"
#include "patches/patches.h"

#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace
{
struct MissionsHudViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "MissionsHudViewController">;
};

using ButtonGetter = Component* (*)(MissionsHudViewController*);

// The buttons are a mix of Button and GenericButtonWidget fields; only their GameObject matters here.
template <il2cpp::Name Field> Component* Button(MissionsHudViewController* controller)
{ return controller->Field<Field, Component*>(); }

struct ButtonSetting {
  const char*  name; // the hud_<name> setting
  ButtonGetter button;
};

constexpr ButtonSetting kButtons[] = {
    {"q_trials", Button<"_challengesButton">}, {"field_training", Button<"_achievementsButton">},
    {"outposts", Button<"_outpostsButton">},   {"daily_goals", Button<"_dailyGoalsButton">},
    {"missions", Button<"_missionsButton">},
};

// The game decides visibility here; the hud_* settings are applied on top every time it does.
int32_t MissionsHudViewController_UpdateButtons_Hook(auto original, MissionsHudViewController* _this)
{
  const auto  result = original(_this);
  const auto& ui     = Config::Get().ui;
  for (const auto& button : kButtons) {
    const auto visibility = ui.MissionHudButtonVisibility(button.name);
    if (visibility == MissionHudVisibility::Auto)
      continue;
    auto* component = button.button(_this);
    if (auto* object = component ? component->gameObject() : nullptr)
      object->SetActive(visibility == MissionHudVisibility::Always);
  }
  return result;
}
} // namespace

void InstallMissionHudTweaksHooks()
{
  if (auto method = MissionsHudViewController::Class::Method("UpdateButtons", 0))
    SPUD_STATIC_DETOUR(method.Pointer(), MissionsHudViewController_UpdateButtons_Hook);
}

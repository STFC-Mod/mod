// Missions: the [ui] hud_* settings, which force mission HUD buttons always shown or never shown, and
// daily_bulk_claim_factions and daily_bulk_claim_toggle_default_on, which limit the missions screen's Claim All to
// the chosen factions' dailies, show only their icons next to the toggle, and can switch the toggle on.

#include "config.h"
#include "features/features.h"
#include "text.h"

#include "game/hud.h"
#include "game/missions.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/core.h>
#include <unity/ui.h>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace
{
// --- Mission HUD buttons ---

using ButtonGetter = unity::Component* (*)(MissionsHudViewController*);

template <il2cpp::Name Field> unity::Component* HudButton(MissionsHudViewController* controller)
{ return controller->ButtonField<Field>(); }

struct ButtonSetting {
  MissionHudVisibility Config::Ui::* visibility;
  ButtonGetter                       button;
};

constexpr ButtonSetting kButtons[] = {
    {&Config::Ui::hud_q_trials, HudButton<"_challengesButton">},
    {&Config::Ui::hud_field_training, HudButton<"_achievementsButton">},
    {&Config::Ui::hud_outposts, HudButton<"_outpostsButton">},
    {&Config::Ui::hud_missions, HudButton<"_missionsButton">},
};

// The game decides visibility in each hooked path; the hud_* settings are applied on top every time it does. A
// missing path would let the game's choice show through, so the paths install as one set: all of them or none.
void ApplyHudVisibility(MissionsHudViewController* _this)
{
  const auto& ui = Config::Get().ui;
  for (const auto& button : kButtons) {
    const auto visibility = ui.*button.visibility;
    if (visibility == MissionHudVisibility::kAuto)
      continue;
    auto* component = button.button(_this);
    if (auto* object = component ? component->gameObject() : nullptr)
      object->SetActive(visibility == MissionHudVisibility::kAlways);
  }
}

void MissionsHudViewController_OnEnable_Hook(auto original, MissionsHudViewController* _this)
{
  original(_this);
  ApplyHudVisibility(_this);
}
void MissionsHudViewController_SetupAchievementsButton_Hook(auto original, MissionsHudViewController* _this)
{
  original(_this);
  ApplyHudVisibility(_this);
}
void MissionsHudViewController_SetupChallengesButton_Hook(auto original, MissionsHudViewController* _this, bool value)
{
  original(_this, value);
  ApplyHudVisibility(_this);
}
void MissionsHudViewController_SetupOutpostsButton_Hook(auto original, MissionsHudViewController* _this, bool value)
{
  original(_this, value);
  ApplyHudVisibility(_this);
}
void MissionsHudViewController_HandleOutpostsAndChallengesHUD_Hook(auto original, MissionsHudViewController* _this)
{
  original(_this);
  ApplyHudVisibility(_this);
}

// --- Daily faction claims ---

using DailyGoalList = il2cpp::List<DailyGoal*>;

struct Faction {
  const char* name; // as in daily_bulk_claim_factions
  const char* icon; // the GameObject next to the Claim All toggle
};

constexpr Faction kFactions[] = {
    {"federation", "Fed_Icon"},
    {"romulan", "Rom_Icon"},
    {"klingon", "Klg_Icon"},
};

bool Claimed(std::string_view faction)
{
  const auto& factions = Config::Get().ui.daily_bulk_claim_factions;
  return factions.empty() || std::ranges::find(factions, faction) != factions.end();
}

void ShowClaimedFactionIcons(MissionViewController* controller)
{
  auto* toggle = controller->claimAllToggle();
  auto* parent = toggle && toggle->transform() ? toggle->transform()->parent() : nullptr;
  if (!parent)
    return;
  for (const auto& faction : kFactions) {
    auto* icon   = parent->FindNamed(faction.icon);
    auto* object = icon ? icon->gameObject() : nullptr;
    if (object)
      object->SetActive(Claimed(faction.name));
    else
      spdlog::debug("[DailyClaimFilter] no {} icon next to Claim All", faction.icon);
  }
}

// The faction whose points a daily rewards, lower-cased ("federation"), or empty for other dailies.
std::string FactionOf(DailyGoal* daily)
{
  constexpr std::string_view kPrefix = "Resource_FactionPoint_";
  auto*                      rewards = daily ? daily->factionPointRewards() : nullptr;
  if (!rewards)
    return {};
  for (auto* item : *rewards) {
    auto* spec = item ? item->resourceSpec() : nullptr;
    auto* id   = spec ? spec->resourceId() : nullptr;
    if (auto resource = id ? il2cpp::ToUtf8(id) : std::string(); resource.starts_with(kPrefix))
      return AsciiStrToLower(resource.substr(kPrefix.size()));
  }
  return {};
}

DailyGoalList* MissionViewController_GetClaimableDailiesList_Hook(auto original, MissionViewController* _this)
{
  auto* list = original(_this);
  ShowClaimedFactionIcons(_this);
  if (!list || Config::Get().ui.daily_bulk_claim_factions.empty())
    return list;

  // Only the three main factions' dailies are filtered; anything else is always claimed.
  const auto total = list->Count();
  for (int32_t i = total - 1; i >= 0; --i) {
    const auto faction = FactionOf((*list)[i]);
    const bool main    = std::ranges::any_of(kFactions, [&](const Faction& f) { return faction == f.name; });
    if (main && !Claimed(faction))
      list->RemoveAt(i);
  }
  spdlog::debug("[DailyClaimFilter] claiming {} of {} dailies", list->Count(), total);
  return list;
}

// daily_bulk_claim_toggle_default_on flips the toggle once per session. The toggle is not interactable straight
// away, so the frame tick keeps trying while the missions screen is open, for at most kMaxToggleAttempts frames.
constexpr int kMaxToggleAttempts = 300;

// Weak, and checked before use: the screen can be destroyed without AboutToHide. Never destroyed: freeing a GC
// handle in a static destructor can run after the runtime has shut down.
auto& shown_missions_screen = *new il2cpp::GcHandle;
bool  toggle_done           = false;
int   toggle_attempts       = 0;

void MissionViewController_AboutToShow_Hook(auto original, MissionViewController* _this)
{
  original(_this);
  ShowClaimedFactionIcons(_this);
  shown_missions_screen = il2cpp::GcHandle::Weak(_this);
}

void MissionViewController_AboutToHide_Hook(auto original, MissionViewController* _this)
{
  if (shown_missions_screen.Get() == _this)
    shown_missions_screen.Reset();
  original(_this);
}

void DailyClaimFilterFrame()
{
  if (toggle_done || !shown_missions_screen || !Config::Get().ui.daily_bulk_claim_toggle_default_on)
    return;
  auto* controller = shown_missions_screen.Get<MissionViewController>();
  auto* toggle     = unity::Alive(controller) ? controller->claimAllToggle() : nullptr;
  if (!toggle)
    return;
  if (!toggle->isOn()) {
    if (++toggle_attempts > kMaxToggleAttempts) {
      spdlog::warn("[DailyClaimFilter] the Claim All toggle never became usable; leaving it off");
      toggle_done = true;
      return;
    }
    toggle->SimulateClick();
  }
  toggle_done = toggle->isOn();
}

bool InstallMissionHud()
{
  using Controller = MissionsHudViewController::Class;
  il2cpp::HookSet hooks("MissionHud");
  IL2CPP_STAGE(hooks, Controller::Method<void()>("OnEnable"), MissionsHudViewController_OnEnable_Hook);
  IL2CPP_STAGE(hooks, Controller::Method<void()>("SetupAchievementsButton"),
               MissionsHudViewController_SetupAchievementsButton_Hook);
  IL2CPP_STAGE(hooks, Controller::Method<void(bool)>("SetupChallengesButton"),
               MissionsHudViewController_SetupChallengesButton_Hook);
  IL2CPP_STAGE(hooks, Controller::Method<void(bool)>("SetupOutpostsButton"),
               MissionsHudViewController_SetupOutpostsButton_Hook);
  IL2CPP_STAGE(hooks, Controller::Method<void()>("HandleOutpostsAndChallengesHUD"),
               MissionsHudViewController_HandleOutpostsAndChallengesHUD_Hook);
  return hooks.Commit();
}

bool InstallDailyClaimFilter()
{
  using Controller = MissionViewController::Class;
  il2cpp::HookSet hooks("DailyClaimFilter");
  IL2CPP_STAGE(hooks, Controller::Method("GetClaimableDailiesList", 0),
               MissionViewController_GetClaimableDailiesList_Hook);
  IL2CPP_STAGE(hooks, Controller::Method("AboutToShow", 0), MissionViewController_AboutToShow_Hook);
  IL2CPP_STAGE(hooks, Controller::Method("AboutToHide", 0), MissionViewController_AboutToHide_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kMissionHud{"MissionHud", &Config::Patches::missionhudtweakshooks, InstallMissionHud};
const Feature kDailyClaimFilter{"DailyClaimFilter", &Config::Patches::dailyfactionbulkclaimhooks,
                                InstallDailyClaimFilter, DailyClaimFilterFrame};

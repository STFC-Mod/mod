// Missions: the hud_* settings, which force mission HUD buttons always shown or never shown, and
// daily_bulk_claim_factions and daily_bulk_claim_toggle_default_on, which limit the missions screen's Claim All to
// the chosen factions' dailies, show only their icons next to the toggle, and can switch the toggle on.

#include "config.h"
#include "patches/patches.h"
#include "str_utils.h"

#include "prime/hud.h"
#include "prime/missions.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/ui.h>

#include <algorithm>
#include <string>

namespace
{
// --- Mission HUD buttons ---

using ButtonGetter = unity::Component* (*)(MissionsHudViewController*);

template <il2cpp::Name Field> unity::Component* HudButton(MissionsHudViewController* controller)
{ return controller->ButtonField<Field>(); }

struct ButtonSetting {
  const char*  name; // the hud_<name> setting
  ButtonGetter button;
};

constexpr ButtonSetting kButtons[] = {
    {"q_trials", HudButton<"_challengesButton">}, {"field_training", HudButton<"_achievementsButton">},
    {"outposts", HudButton<"_outpostsButton">},   {"daily_goals", HudButton<"_dailyGoalsButton">},
    {"missions", HudButton<"_missionsButton">},
};

// The game decides visibility here; the hud_* settings are applied on top every time it does.
int32_t MissionsHudViewController_UpdateButtons_Hook(auto original, MissionsHudViewController* _this)
{
  const auto  result = original(_this);
  const auto& ui     = Config::Get().ui;
  for (const auto& button : kButtons) {
    const auto visibility = ui.MissionHudButtonVisibility(button.name);
    if (visibility == MissionHudVisibility::kAuto)
      continue;
    auto* component = button.button(_this);
    if (auto* object = component ? component->gameObject() : nullptr)
      object->SetActive(visibility == MissionHudVisibility::kAlways);
  }
  return result;
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

unity::Transform* FindChild(unity::Transform* root, std::string_view name)
{
  if (!root)
    return nullptr;
  if (auto* object = root->gameObject(); object && object->Name() == name)
    return root;
  for (int32_t i = 0; i < root->childCount(); ++i)
    if (auto* found = FindChild(root->GetChild(i), name))
      return found;
  return nullptr;
}

void ShowClaimedFactionIcons(MissionViewController* controller)
{
  auto* toggle = controller->claimAllToggle();
  auto* parent = toggle && toggle->transform() ? toggle->transform()->parent() : nullptr;
  if (!parent)
    return;
  for (const auto& faction : kFactions) {
    auto* icon   = FindChild(parent, faction.icon);
    auto* object = icon ? icon->gameObject() : nullptr;
    if (object)
      object->SetActive(Claimed(faction.name));
    else
      spdlog::debug("[DailyFactionBulkClaim] icon '{}' not found", faction.icon);
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
  spdlog::debug("[DailyFactionBulkClaim] claiming {} of {} dailies", list->Count(), total);
  return list;
}

// daily_bulk_claim_toggle_default_on flips the toggle once per session. The toggle is not interactable straight
// away, so the frame tick keeps trying while the missions screen is open, for at most kMaxToggleAttempts frames.
constexpr int kMaxToggleAttempts = 300;

MissionViewController* shown_controller = nullptr;
bool                   toggle_done      = false;
int                    toggle_attempts  = 0;

void MissionViewController_AboutToShow_Hook(auto original, MissionViewController* _this)
{
  original(_this);
  ShowClaimedFactionIcons(_this);
  shown_controller = _this;
}

void MissionViewController_AboutToHide_Hook(auto original, MissionViewController* _this)
{
  if (shown_controller == _this)
    shown_controller = nullptr;
  original(_this);
}

void MissionsUpdate()
{
  if (!shown_controller || toggle_done || !Config::Get().ui.daily_bulk_claim_toggle_default_on)
    return;
  auto* toggle = shown_controller->claimAllToggle();
  if (!toggle)
    return;
  if (!toggle->isOn()) {
    if (++toggle_attempts > kMaxToggleAttempts) {
      spdlog::warn("[DailyFactionBulkClaim] Claim All toggle never became interactable; not switching it on");
      toggle_done = true;
      return;
    }
    toggle->SimulateClick();
  }
  toggle_done = toggle->isOn();
}

void InstallMissionHud()
{
  IL2CPP_HOOK(MissionsHudViewController::Class::Method("UpdateButtons", 0),
              MissionsHudViewController_UpdateButtons_Hook);
}

void InstallDailyClaimFilter()
{
  using Controller = MissionViewController::Class;
  IL2CPP_HOOK(Controller::Method("GetClaimableDailiesList", 0), MissionViewController_GetClaimableDailiesList_Hook);
  IL2CPP_HOOK(Controller::Method("AboutToShow", 0), MissionViewController_AboutToShow_Hook);
  IL2CPP_HOOK(Controller::Method("AboutToHide", 0), MissionViewController_AboutToHide_Hook);
}
} // namespace

const Feature kMissionHud{"MissionHud", &Config::Patches::missionhudtweakshooks, InstallMissionHud};
const Feature kDailyClaimFilter{"DailyClaimFilter", &Config::Patches::dailyfactionbulkclaimhooks,
                                InstallDailyClaimFilter, MissionsUpdate};

// daily_bulk_claim_factions and daily_bulk_claim_toggle_default_on: limits the missions screen's Claim All to the
// chosen factions' dailies, shows only their icons next to the toggle, and can switch the toggle on.

#include "config.h"
#include "patches/patches.h"
#include "str_utils.h"

#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <string>

namespace
{
struct ResourceSpec : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ResourceSpec">;

  Il2CppString* resourceId()
  { return Field<"resourceId_", Il2CppString*>(); }
};

struct InventoryItem : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "InventoryItem">;

  ResourceSpec* resourceSpec()
  { return Field<"_resourceSpec", ResourceSpec*>(); }
};

struct PointerEventData : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.EventSystems", "PointerEventData">;
};

struct Toggle : Component {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.UI", "Toggle">;

  bool isOn()
  { return Get<"isOn", bool>(); }

  // Goes through the pointer handler instead of the isOn setter, so interactable checks and listeners behave as
  // for a real click.
  void SimulateClick()
  {
    if (auto* event = il2cpp::New<PointerEventData>(EventSystem::current()))
      Call<"OnPointerClick">(event);
  }
};

struct MissionViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Missions", "MissionViewController">;

  Toggle* ClaimAllToggle()
  { return Field<"_claimAllToggle", Toggle*>(); }
};

// GetClaimableDailiesList returns List<ITournament>; the items are EventModel today.
using TournamentList = il2cpp::List<il2cpp::Object*>;

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

Transform* FindChild(Transform* root, std::string_view name)
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
  auto* toggle = controller->ClaimAllToggle();
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
std::string FactionOf(il2cpp::Object* tournament)
{
  constexpr std::string_view kPrefix = "Resource_FactionPoint_";
  auto* rewards = tournament ? tournament->Field<"_factionPointRewards", il2cpp::List<InventoryItem*>*>() : nullptr;
  if (!rewards)
    return {};
  for (auto* item : *rewards) {
    auto* spec = item ? item->resourceSpec() : nullptr;
    auto* id   = spec ? spec->resourceId() : nullptr;
    if (auto resource = id ? to_string(id) : std::string(); resource.starts_with(kPrefix))
      return AsciiStrToLower(resource.substr(kPrefix.size()));
  }
  return {};
}

TournamentList* MissionViewController_GetClaimableDailiesList_Hook(auto original, MissionViewController* _this)
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
} // namespace

void DailyFactionBulkClaimUpdate()
{
  if (!shown_controller || toggle_done || !Config::Get().ui.daily_bulk_claim_toggle_default_on)
    return;
  auto* toggle = shown_controller->ClaimAllToggle();
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

void InstallDailyFactionBulkClaimHooks()
{
  using Controller = MissionViewController::Class;
  if (auto m = Controller::Method("GetClaimableDailiesList", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), MissionViewController_GetClaimableDailiesList_Hook);
  if (auto m = Controller::Method("AboutToShow", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), MissionViewController_AboutToShow_Hook);
  if (auto m = Controller::Method("AboutToHide", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), MissionViewController_AboutToHide_Hook);
  InstallScreenManagerUpdateHook();
}

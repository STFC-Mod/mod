#include "config.h"
#include "str_utils.h"

#include <prime/Component.h>
#include <prime/EventSystem.h>
#include <prime/GameObject.h>
#include <prime/Transform.h>

#include <il2cpp/il2cpp.h>
#include <spud/detour.h>

#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

namespace
{
constexpr std::string_view kFactionPointResourcePrefix = "Resource_FactionPoint_";

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
  void OnPointerClick(PointerEventData* event)
  { Call<"OnPointerClick">(event); }

  // Goes through the pointer handler instead of the isOn setter, so interactable checks and
  // listeners behave as for a real click.
  void SimulateClick()
  {
    if (auto* event = il2cpp::New<PointerEventData>(EventSystem::current()))
      OnPointerClick(event);
  }
};

struct MissionViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Missions", "MissionViewController">;

  Toggle* ClaimAllToggle()
  { return Field<"_claimAllToggle", Toggle*>(); }
};

// GetClaimableDailiesList returns List<ITournament>; the items are EventModel today.
using TournamentList = il2cpp::List<il2cpp::Object*>;

Transform* GetTransformOf(Component* component)
{ return component ? component->transform() : nullptr; }

Transform* FindChildByName(Transform* root, const char* name)
{
  if (root == nullptr) {
    return nullptr;
  }

  auto* go = root->gameObject();
  if (go != nullptr && go->Name() == name) {
    return root;
  }

  for (int32_t i = 0; i < root->childCount(); ++i) {
    if (auto* found = FindChildByName(root->GetChild(i), name)) {
      return found;
    }
  }

  return nullptr;
}

struct FactionIconName {
  const char* faction;
  const char* game_object_name;
};

constexpr FactionIconName kFactionIconNames[] = {
    {"federation", "Fed_Icon"},
    {"romulan", "Rom_Icon"},
    {"klingon", "Klg_Icon"},
};

void ApplyFactionIconVisibility(Transform* toggle_tf)
{
  if (toggle_tf == nullptr) {
    return;
  }

  auto* parent = toggle_tf->parent();
  if (parent == nullptr) {
    spdlog::debug("[DailyFactionBulkClaim] ApplyFactionIconVisibility: toggle has no parent Transform");
    return;
  }

  const auto& configured = Config::Get().daily_bulk_claim_factions;

  for (const auto& entry : kFactionIconNames) {
    auto* icon_transform = FindChildByName(parent, entry.game_object_name);
    if (icon_transform == nullptr) {
      spdlog::debug("[DailyFactionBulkClaim] ApplyFactionIconVisibility: '{}' not found", entry.game_object_name);
      continue;
    }

    auto* go = icon_transform->gameObject();
    if (go == nullptr) {
      continue;
    }

    const bool should_show =
        configured.empty() || std::ranges::find(configured, std::string(entry.faction)) != configured.end();
    go->SetActive(should_show);
  }
}

bool                   g_applied_default_toggle_on      = false;
int                    g_default_toggle_attempts        = 0;
MissionViewController* g_active_mission_view_controller = nullptr;

constexpr int kMaxDefaultToggleAttempts = 300;

void ApplyDefaultToggleOnOnce(MissionViewController* controller)
{
  auto* toggle = controller != nullptr ? controller->ClaimAllToggle() : nullptr;
  if (g_applied_default_toggle_on || !Config::Get().daily_bulk_claim_toggle_default_on || toggle == nullptr) {
    return;
  }

  if (toggle->isOn()) {
    g_applied_default_toggle_on = true;
    return;
  }

  if (++g_default_toggle_attempts > kMaxDefaultToggleAttempts) {
    spdlog::warn("[DailyFactionBulkClaim] daily_bulk_claim_toggle_default_on: giving up, toggle never became "
                "interactable");
    g_applied_default_toggle_on = true;
    return;
  }

  toggle->SimulateClick();

  if (toggle->isOn()) {
    spdlog::debug("[DailyFactionBulkClaim] daily_bulk_claim_toggle_default_on: forced Claim All toggle on "
                 "(attempt {})",
                 g_default_toggle_attempts);
    g_applied_default_toggle_on = true;
  }
}

bool FactionMatches(const std::string& faction_name, const std::vector<std::string>& configured)
{
  if (faction_name.empty() || configured.empty()) {
    return false;
  }
  return std::ranges::find(configured, AsciiStrToLower(faction_name)) != configured.end();
}

bool IsMainFactionDaily(const std::string& faction_name)
{
  if (faction_name.empty()) {
    return false;
  }
  const auto lowered = AsciiStrToLower(faction_name);
  return std::ranges::any_of(kFactionIconNames, [&](const auto& entry) { return lowered == entry.faction; });
}

std::string FactionNameOfTournament(il2cpp::Object* tournament)
{
  auto* rewards = tournament ? tournament->Field<"_factionPointRewards", il2cpp::List<InventoryItem*>*>() : nullptr;
  if (!rewards)
    return {};

  for (auto* item : *rewards) {
    auto* spec   = item ? item->resourceSpec() : nullptr;
    auto* id_str = spec ? spec->resourceId() : nullptr;
    if (id_str == nullptr) {
      continue;
    }

    auto resource_id = to_string(id_str);
    if (resource_id.starts_with(kFactionPointResourcePrefix)) {
      return resource_id.substr(kFactionPointResourcePrefix.size());
    }
  }

  return {};
}

void FilterClaimableDailiesByFaction(TournamentList* list, const std::vector<std::string>& configured)
{
  if (list == nullptr) {
    spdlog::debug("[DailyFactionBulkClaim] GetClaimableDailiesList returned null; nothing to filter");
    return;
  }

  const auto total = list->Count();
  spdlog::debug("[DailyFactionBulkClaim] filtering {} claimable daily(ies) against configured factions [{}]", total,
               fmt::join(configured, ", "));

  int kept = 0;
  for (int32_t i = total - 1; i >= 0; --i) {
    auto* tournament    = (*list)[i];
    auto  faction_name  = FactionNameOfTournament(tournament);
    const bool is_main_faction_daily = IsMainFactionDaily(faction_name);
    auto  matches       = FactionMatches(faction_name, configured);

    spdlog::debug("[DailyFactionBulkClaim]   [{}] faction: '{}' -> {}", i,
                 faction_name.empty() ? "<none>" : faction_name,
                 !is_main_faction_daily ? "keep (not a main faction daily)"
                                        : (matches ? "keep (claim)" : "remove (skip)"));

    if (is_main_faction_daily && !matches) {
      list->RemoveAt(i);
    } else {
      ++kept;
    }
  }

  spdlog::debug("[DailyFactionBulkClaim] kept {}/{} daily(ies) for claiming", kept, total);
}

TournamentList* MissionViewController_GetClaimableDailiesList(auto original, MissionViewController* _this)
{
  auto* list = original(_this);

  ApplyFactionIconVisibility(GetTransformOf(_this->ClaimAllToggle()));

  const auto& configured = Config::Get().daily_bulk_claim_factions;
  if (configured.empty()) {
    return list;
  }

  FilterClaimableDailiesByFaction(list, configured);
  return list;
}

void MissionViewController_AboutToShow(auto original, MissionViewController* _this)
{
  original(_this);

  ApplyFactionIconVisibility(GetTransformOf(_this->ClaimAllToggle()));

  g_active_mission_view_controller = _this;
}

void MissionViewController_AboutToHide(auto original, MissionViewController* _this)
{
  if (g_active_mission_view_controller == _this) {
    g_active_mission_view_controller = nullptr;
  }

  original(_this);
}
} // namespace

void DailyFactionBulkClaimUpdate()
{
  if (g_active_mission_view_controller != nullptr) {
    ApplyDefaultToggleOnOnce(g_active_mission_view_controller);
  }
}

void InstallDailyFactionBulkClaimHooks()
{
  using Controller = MissionViewController::Class;
  if (auto m = Controller::Method("GetClaimableDailiesList", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), MissionViewController_GetClaimableDailiesList);
  if (auto m = Controller::Method("AboutToShow", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), MissionViewController_AboutToShow);
  if (auto m = Controller::Method("AboutToHide", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), MissionViewController_AboutToHide);
}

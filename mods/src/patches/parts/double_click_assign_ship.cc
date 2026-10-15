// The ship management dock: pinned_ships, which moves the named ships to the front of the idle ship list after the
// game sorts it, and double_click_to_assign_ship, where a double click on a ship tile or Enter presses Assign.
//
// Only ShipManagementViewContext.Sort is hooked, not ISortedFleetsContext, so the hailing frequencies inventory keeps
// the game's order.

#include "config.h"
#include "patches/patches.h"
#include "ship_tile_click.h"
#include "str_utils.h"

#include "prime/fleet.h"
#include "prime/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <unity/input.h>
#include <unity/tracker.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <vector>

namespace
{
// --- Pinned ships ---

// Unmatched entries are warned about once, not on every sort.
std::set<std::string> warned;

// Within the pinned group and the rest, the game's active sort order is kept.
void PinShips(il2cpp::List<FleetPlayerData*>* list)
{
  const auto& pinned = Config::Get().ui.pinned_ships;
  const auto  count  = list ? list->Count() : 0;
  if (pinned.empty() || count <= 1)
    return;
  if (count > 2000) {
    spdlog::warn("[PinnedShipSort] SortedIdleShips reported an implausible count ({}); skipping", count);
    return;
  }

  struct Ship {
    FleetPlayerData*         data;
    std::vector<std::string> words;
    int64_t                  level;
    bool                     pinned = false;
    int8_t                   tech_applied = -1;

    bool TechApplied()
    {
      if (tech_applied < 0)
        tech_applied = data && data->HasAppliedTech() ? 1 : 0;
      return tech_applied == 1;
    }
  };
  std::vector<Ship> ships;
  ships.reserve(count);
  for (auto* ship : *list)
    ships.push_back(
        {ship, ship ? ShipNameWords(ship->DisplayName()) : std::vector<std::string>(), ship ? ship->Level() : 0});

  // Each entry pins one ship: the highest-level idle ship it matches that an earlier entry has not taken.
  bool any = false;
  for (const auto& pattern : pinned) {
    Ship* best = nullptr;
    for (auto& ship : ships)
      if (!ship.pinned && ShipNameMatches(ship.words, pattern)
          && (!best || ship.level > best->level
              || (ship.level == best->level && ship.data && best->data && ship.data->HullId() == best->data->HullId()
                  && ship.TechApplied() && !best->TechApplied())))
        best = &ship;
    if (best) {
      best->pinned = any = true;
    } else if (const auto name = fmt::format("{}", fmt::join(pattern, " ")); warned.insert(name).second) {
      spdlog::warn("[PinnedShipSort] pinned_ships entry '{}' matched no idle ship (entries match the end of the "
                   "game's own ship name)",
                   name);
    }
  }
  if (!any)
    return;

  std::ranges::stable_partition(ships, &Ship::pinned);
  bool unchanged = true;
  for (int32_t i = 0; i < count && unchanged; ++i)
    unchanged = ships[i].data == (*list)[i];
  if (unchanged)
    return;

  // SortedIdleShips is a List<T>, which always has Clear and Add; the rewrite cannot stop halfway.
  list->Clear();
  for (const auto& ship : ships)
    list->Add(ship.data);
}

void ShipManagementViewContext_Sort_Hook(auto original, ShipManagementViewContext* _this)
{
  original(_this);
  PinShips(_this->SortedIdleShips());
}

// --- Double click to assign ---

constexpr auto kDoubleClickWindow = std::chrono::milliseconds(400);

FleetPlayerData*                      last_clicked_ship = nullptr;
std::chrono::steady_clock::time_point last_click_time{};

void PressAssignButton()
{
  for (auto* widget : unity::Instances<AssignShipsWidget>()) {
    if (!widget || !CanvasController::Shows(widget) || !widget->isActiveAndEnabled())
      continue;
    auto* wrapper = widget->assignButton();
    auto* button  = wrapper && wrapper->Widget() ? wrapper->Widget()->TheButton() : nullptr;
    if (button)
      button->Press();
    return;
  }
}

} // namespace

void DoubleClickOnShipTileClicked(FleetPlayerData* ship)
{
  if (!Config::Get().ui.double_click_to_assign_ship)
    return;
  if (!ship)
    return;

  const auto now          = std::chrono::steady_clock::now();
  const bool double_click = ship == last_clicked_ship && now - last_click_time <= kDoubleClickWindow;
  last_clicked_ship       = ship;
  last_click_time         = now;
  if (!double_click)
    return;

  // Consumed, so a third click does not count as another double click.
  last_clicked_ship = nullptr;
  PressAssignButton();
}

void DockUpdate()
{
  if (!Config::Get().ui.double_click_to_assign_ship)
    return;
  // Down, not Pressed: a held Enter must press Assign once, not every frame.
  if (!unity::Key::Down(unity::KeyCode::Return) && !unity::Key::Down(unity::KeyCode::KeypadEnter))
    return;
  if (unity::Key::IsInputFocused())
    return;
  PressAssignButton();
}

namespace
{
void InstallPinnedShips()
{ IL2CPP_HOOK(ShipManagementViewContext::Class::Method("Sort", 0), ShipManagementViewContext_Sort_Hook); }

void InstallDoubleClickAssign()
{ ship_tile_click::TryInstallHook(); }

} // namespace

const Feature kPinnedShips{"PinnedShips", &Config::Patches::pinnedshiphooks, InstallPinnedShips};
const Feature kDoubleClickAssign{"DoubleClickAssign", &Config::Patches::doubleclickassignshiphooks,
                                 InstallDoubleClickAssign};
const Feature kAssignOnEnter{"AssignOnEnter", nullptr, nullptr, DockUpdate};

namespace
{
void UpdateManageShipDockSelection()
{
  static uint64_t last_fleet_id = 0;
  static auto     next_update   = std::chrono::steady_clock::time_point{};
  const auto      now           = std::chrono::steady_clock::now();
  if (now < next_update)
    return;
  next_update    = now + std::chrono::milliseconds(100);
  auto* sections = Hub::Sections();
  if (!Config::Get().ui.dock_syncmanagement || !sections
      || sections->CurrentSection() != SectionID::ShipManagement_Details) {
    last_fleet_id = 0;
    return;
  }
  for (auto* controller : unity::Instances<ShipManagementViewController>()) {
    if (!controller || !controller->isActiveAndEnabled())
      continue;
    auto* canvas = CanvasController::Of(controller);
    if (!canvas || !canvas->Visible())
      continue;
    auto* context = controller->Context();
    auto* fleet   = context ? context->CurrentFleet() : nullptr;
    auto* manager = FleetsManager::Instance();
    if (!fleet || !fleet->HasShip() || !manager || !manager->HasFleetService())
      continue;
    const auto index = fleet->Index();
    if (index < 0)
      continue;
    // Exclude unassigned and synthetic fleets; management list order is not dock order.
    auto* dock_fleet = manager->GetFleetPlayerData(index);
    if (!dock_fleet || dock_fleet->Id() != fleet->Id())
      continue;
    if (last_fleet_id == fleet->Id())
      return;
    auto* bar = unity::Newest<FleetBarViewController>();
    if (!bar)
      return;
    if (!bar->IsIndexSelected(index)) {
      // Use the bar's selection flow to update both the highlight and fleet panel.
      bar->RequestSelect(index);
      if (!bar->IsIndexSelected(index))
        return; // Retry next update if native selection was blocked.
      spdlog::debug("[ManageShips] selected dock {} for fleet {}", index + 1, fleet->Id());
    }
    last_fleet_id = fleet->Id();
    return;
  }
  last_fleet_id = 0;
}

} // namespace
const Feature kDockSelection{"DockSelection", &Config::Patches::hotkeyhooks, nullptr, UpdateManageShipDockSelection};

// The ship management dock:
// - [ui] pinned_ships moves the named ships to the front of the idle ship list after the game sorts it, and
//   Shift+click on a ship tile pins or unpins that ship;
// - [ui] double_click_to_assign_ship presses Assign on a double click on a ship tile, or on Enter;
// - [ui] dock_syncmanagement selects the fleet bar slot of the ship open in ship management.
//
// Only ShipManagementViewContext.Sort is hooked, not ISortedFleetsContext, so the hailing frequencies inventory keeps
// the game's order.

#include "config.h"
#include "features/features.h"
#include "file.h"
#include "text.h"

#include "game/app.h"
#include "game/fleet.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <toml++/toml.h>
#include <unity/input.h>
#include <unity/tracker.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
// The ship management screen when it is the one showing. The dock is hosted under more than one game section, so
// this follows the screen rather than SectionID.
ShipManagementViewController* VisibleShipManagement()
{
  for (auto* controller : unity::Instances<ShipManagementViewController>())
    if (controller->isActiveAndEnabled() && CanvasController::Shows(controller))
      return controller;
  return nullptr;
}

std::string ShipNameText(const std::vector<std::string>& words)
{ return fmt::format("{}", fmt::join(words, " ")); }

// --- Pinned ships ---

// Unmatched entries are warned about once, not on every sort.
std::set<std::string> warned_patterns;

// Within the pinned group and the rest, the game's active sort order is kept.
void PinShips(il2cpp::List<FleetPlayerData*>* list)
{
  const auto& pinned = Config::Get().ui.pinned_ships;
  const auto  count  = list ? list->Count() : 0;
  if (pinned.empty() || count <= 1)
    return;

  struct Ship {
    FleetPlayerData*         data;
    std::vector<std::string> words;
    int64_t                  level;
    bool                     pinned       = false;
    int8_t                   tech_applied = -1;

    // Read lazily: only ties between equal hulls need it.
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
    } else if (const auto name = ShipNameText(pattern); warned_patterns.insert(name).second) {
      spdlog::warn("[PinnedShips] '{}' matches no idle ship; entries match the end of the game's ship name", name);
    }
  }
  if (!any)
    return;

  // Read from the original snapshot in two passes to preserve both groups' order. Only changed slots cross the
  // managed boundary; the list keeps its count and needs no partition buffer or second read of its items.
  int32_t index = 0;
  for (const bool pinned_group : {true, false})
    for (const auto& ship : ships)
      if (ship.pinned == pinned_group) {
        if (ship.data != ships[index].data)
          list->Set(index, ship.data);
        ++index;
      }
}

void ShipManagementViewContext_Sort_Hook(auto original, ShipManagementViewContext* _this)
{
  original(_this);
  PinShips(_this->SortedIdleShips());
}

// --- Shift+click pinning ---

// The settings file with only the pinned_ships line of [ui] replaced, or added, so the rest keeps its comments and
// layout. A layout this line edit cannot handle, such as an inline [ui] table or a multi-line value, is caught by
// SavePinnedShips checking the result.
std::string WithPinnedShipsLine(const std::string& settings, const std::string& line)
{
  std::vector<std::string> lines;
  std::istringstream       in(settings);
  for (std::string text; std::getline(in, text);)
    lines.push_back(text);
  // Where [ui] ends, and its pinned_ships line, if any.
  size_t ui_end = lines.size(), pinned = lines.size();
  bool   has_ui = false, in_ui = false;
  for (size_t i = 0; i < lines.size(); ++i) {
    auto text = std::string_view(lines[i]);
    text.remove_prefix(std::min(text.find_first_not_of(" \t"), text.size()));
    if (text.starts_with('[')) {
      if (in_ui) {
        ui_end = i;
        break;
      }
      in_ui = has_ui = text.starts_with("[ui]");
    } else if (in_ui && text.starts_with("pinned_ships")) {
      const auto rest = text.substr(std::string_view("pinned_ships").size());
      if (const auto at = rest.find_first_not_of(" \t"); at != std::string_view::npos && rest[at] == '=')
        pinned = i;
    }
  }
  if (pinned < lines.size())
    lines[pinned] = line;
  else if (has_ui)
    lines.insert(lines.begin() + ui_end, line);
  else
    lines.insert(lines.end(), {"", "[ui]", line});
  std::string result;
  for (const auto& text : lines)
    result += text + '\n';
  return result;
}

// Saves the pins to [ui] pinned_ships so they survive a restart. The line edit is used when the result parses back
// to the same value; otherwise the whole file is rewritten from its parsed table, which keeps every setting but
// loses comments. A file that does not parse at all is left alone.
// TODO(tashcan): route this through a settings writer once there is a second setting the mod saves.
void SavePinnedShips(const std::vector<std::vector<std::string>>& names)
{
  std::string joined;
  for (const auto& words : names)
    joined += (joined.empty() ? "" : ", ") + ShipNameText(words);
  std::ostringstream value;
  value << toml::value<std::string>(joined);

  const auto  path = File::Path(File::Config(), true);
  std::string settings;
  {
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    settings = text.str();
  }
  const auto saves = [&](const std::string& text) {
    try {
      return toml::parse(text)["ui"]["pinned_ships"].value<std::string>() == joined;
    } catch (const toml::parse_error&) {
      return false;
    }
  };
  auto updated = WithPinnedShipsLine(settings, "pinned_ships = " + value.str());
  if (!saves(updated)) {
    try {
      auto table = toml::parse(settings);
      if (!table["ui"].is_table())
        table.insert_or_assign("ui", toml::table{});
      table["ui"].as_table()->insert_or_assign("pinned_ships", joined);
      std::ostringstream text;
      text << table;
      updated = text.str();
    } catch (const toml::parse_error& error) {
      spdlog::warn("[PinnedShipClick] not saving pinned ships: {} does not parse: {}", File::Config(),
                   error.description());
      return;
    }
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << updated;
  if (!out)
    spdlog::warn("[PinnedShipClick] could not save pinned ships to {}", File::Config());
}

// Re-runs the pinned sort and rebuilds the dock's tile list through the game's own
// SelectableListBase.ClearAndGenerateContents: the controller is the list's IDataContextProvider, and the list binds
// the reordered SortedIdleShips directly.
void RefreshDock(ShipManagementViewController* controller)
{
  auto* context = controller->Context();
  if (!context)
    return;
  context->Sort();
  for (auto* widget : unity::Instances<AssignShipsWidget>())
    if (auto* list = widget->isActiveAndEnabled() ? widget->selectableList() : nullptr)
      list->Rebuild(controller);
}

bool pin_click_installed = false;

void TogglePinOnShiftClick(FleetPlayerData* ship)
{
  const bool shift = unity::Key::Pressed(unity::KeyCode::LeftShift) || unity::Key::Pressed(unity::KeyCode::RightShift);
  if (!pin_click_installed || !shift)
    return;
  auto* controller = VisibleShipManagement();
  auto  key        = ShipNameWords(ship->DisplayName());
  // Placeholder tiles, such as "build ship", have no name.
  if (!controller || key.empty())
    return;

  auto& names = Config::Get().ui.pinned_ships;
  if (const auto found = std::ranges::find(names, key); found != names.end()) {
    names.erase(found);
    spdlog::info("[PinnedShipClick] unpinned '{}'", ShipNameText(key));
  } else {
    names.push_back(std::move(key));
    spdlog::info("[PinnedShipClick] pinned '{}'", ShipNameText(names.back()));
  }
  SavePinnedShips(names);
  RefreshDock(controller);
}

// --- Double click and Enter to assign ---

constexpr auto kDoubleClickWindow = std::chrono::milliseconds(400);

bool double_click_installed = false;
// Compared only, never dereferenced.
FleetPlayerData*                      last_clicked_ship = nullptr;
std::chrono::steady_clock::time_point last_click_time{};

void PressAssignButton()
{
  for (auto* widget : unity::Instances<AssignShipsWidget>()) {
    if (!widget->isActiveAndEnabled() || !CanvasController::Shows(widget))
      continue;
    auto* wrapper = widget->assignButton();
    auto* button  = wrapper && wrapper->Widget() ? wrapper->Widget()->UnityButton() : nullptr;
    if (button)
      button->Press();
    return;
  }
}

void AssignOnDoubleClick(FleetPlayerData* ship)
{
  if (!double_click_installed || !Config::Get().ui.double_click_to_assign_ship)
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

void AssignOnEnterFrame()
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

// Shift+click and double click share the tile's click handler: one detour, two features.
void ShipTileWidget_HandleOnClick_Hook(auto original, ShipTileWidget* _this)
{
  original(_this);
  // The click arrives through the UI event system, which can run before the frame tick has polled this frame.
  unity::Key::Poll();
  if (auto* ship = _this->Context()) {
    TogglePinOnShiftClick(ship);
    AssignOnDoubleClick(ship);
  }
}

// Shared by Shift+click pinning and double-click assignment, whichever of them install, so it is its own set,
// installed on the first call.
bool InstallShipTileClick()
{
  static const bool installed = [] {
    il2cpp::HookSet hooks("ShipTileClick");
    IL2CPP_STAGE(hooks, ShipTileWidget::Class::Method("HandleOnClick", 0), ShipTileWidget_HandleOnClick_Hook);
    return hooks.Commit();
  }();
  return installed;
}

// --- Dock selection ---

void DockSelectionFrame()
{
  // Ten times a second; selecting a slot is not urgent.
  static Throttle throttle(std::chrono::milliseconds(100));
  static int64_t  last_fleet_id = 0;
  if (!throttle.Due())
    return;
  auto* sections = Hub::Sections();
  if (!Config::Get().ui.dock_syncmanagement || !sections
      || sections->CurrentSection() != SectionID::ShipManagement_Details) {
    last_fleet_id = 0;
    return;
  }
  auto* controller = VisibleShipManagement();
  auto* context    = controller ? controller->Context() : nullptr;
  auto* fleet      = context ? context->CurrentFleet() : nullptr;
  auto* manager    = FleetsManager::Instance();
  if (!fleet || !fleet->HasShip() || !manager || !manager->HasFleetService()) {
    last_fleet_id = 0;
    return;
  }
  // Unassigned and synthetic fleets have no dock slot; the management list order is not the dock order.
  const auto index      = fleet->Index();
  auto*      dock_fleet = index >= 0 ? manager->GetFleetPlayerData(index) : nullptr;
  if (!dock_fleet || dock_fleet->Id() != fleet->Id()) {
    last_fleet_id = 0;
    return;
  }
  if (last_fleet_id == fleet->Id())
    return;
  auto* bar = unity::Newest<FleetBarViewController>();
  if (!bar)
    return;
  if (!bar->IsIndexSelected(index)) {
    // The bar's own selection flow updates both the highlight and the fleet panel.
    bar->RequestSelect(index);
    // Blocked by the game: try again next time.
    if (!bar->IsIndexSelected(index))
      return;
    spdlog::debug("[DockSelection] selected dock slot {} for fleet {}", index + 1, fleet->Id());
  }
  last_fleet_id = fleet->Id();
}

bool InstallPinnedShips()
{
  il2cpp::HookSet hooks("PinnedShips");
  IL2CPP_STAGE(hooks, ShipManagementViewContext::Class::Method("Sort", 0), ShipManagementViewContext_Sort_Hook);
  return hooks.Commit();
}

bool InstallPinnedShipClick()
{
  pin_click_installed = InstallShipTileClick();
  return pin_click_installed;
}

bool InstallDoubleClickAssign()
{
  double_click_installed = InstallShipTileClick();
  return double_click_installed;
}
} // namespace

const Feature kPinnedShips{"PinnedShips", &Config::Patches::pinnedshiphooks, InstallPinnedShips};
const Feature kPinnedShipClick{"PinnedShipClick", &Config::Patches::pinnedshipclicktoggle, InstallPinnedShipClick};
const Feature kDoubleClickAssign{"DoubleClickAssign", &Config::Patches::doubleclickassignshiphooks,
                                 InstallDoubleClickAssign};
// Frame work only. Enter needs no hook, so it follows the setting alone.
const Feature kAssignOnEnter{"AssignOnEnter", nullptr, nullptr, AssignOnEnterFrame};
// Frame work only, installed with the hotkeys it was introduced with.
const Feature kDockSelection{"DockSelection", &Config::Patches::hotkeyhooks, nullptr, DockSelectionFrame};

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
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
// The ship management screen when it is the one showing. The dock is hosted under more than one game section, so
// this follows the screen rather than SectionID.
ShipManagementViewController* VisibleShipManagement()
{
  for (auto* controller : unity::Instances<ShipManagementViewController>())
    if (CanvasController::OnScreen(controller))
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
  if (pinned.empty() || !list || list->Count() <= 1)
    return;

  struct Ship {
    FleetPlayerData*         data = nullptr;
    std::vector<std::string> words;
    int64_t                  level        = 0;
    bool                     pinned       = false;
    int8_t                   tech_applied = -1;

    // Read lazily: only ties between equal hulls need it.
    bool TechApplied()
    {
      if (tech_applied < 0)
        tech_applied = data && data->HasAppliedTech() ? 1 : 0;
      return tech_applied == 1;
    }

    // Higher level first, then, within one hull and level, the ship with tech applied.
    bool BetterThan(Ship& other)
    {
      if (level != other.level)
        return level > other.level;
      return data && other.data && data->HullId() == other.data->HullId() && TechApplied() && !other.TechApplied();
    }
  };
  std::vector<Ship> ships;
  for (auto* ship : *list)
    ships.push_back(ship ? Ship{.data = ship, .words = ShipNameWords(ship->DisplayName()), .level = ship->Level()}
                         : Ship{});

  // Each entry pins one ship: the best idle ship it matches that an earlier entry has not taken.
  for (const auto& pattern : pinned) {
    Ship* best = nullptr;
    for (auto& ship : ships)
      if (!ship.pinned && ShipNameMatches(ship.words, pattern) && (!best || ship.BetterThan(*best)))
        best = &ship;
    if (best)
      best->pinned = true;
    else if (const auto name = ShipNameText(pattern); warned_patterns.insert(name).second)
      spdlog::warn("[PinnedShips] '{}' matches no idle ship; entries match the end of the game's ship name", name);
  }
  if (std::ranges::none_of(ships, [](const Ship& ship) { return ship.pinned; }))
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

// The tile click hook is shared with double-click assignment, so it runs even when this feature did not install.
bool pin_click_installed = false;

// The settings file with only the pinned_ships line of [ui] replaced, or added, so the rest keeps its comments and
// layout. A layout this line edit cannot handle, such as an inline [ui] table or a multi-line value, is caught by
// SavePinnedShips checking the result.
std::string WithPinnedShipsLine(const std::string& settings, const std::string& line)
{
  std::vector<std::string> lines;
  std::istringstream       in(settings);
  for (std::string text; std::getline(in, text);)
    lines.push_back(text);
  const auto trimmed = [](std::string_view text) {
    text.remove_prefix(std::min(text.find_first_not_of(" \t"), text.size()));
    return text;
  };
  constexpr std::string_view kKey = "pinned_ships";
  // Where [ui] ends, and its pinned_ships line, if any.
  size_t ui_end = lines.size(), pinned = lines.size();
  bool   has_ui = false, in_ui = false;
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto text = trimmed(lines[i]);
    if (text.starts_with('[')) {
      if (in_ui) {
        ui_end = i;
        break;
      }
      in_ui = has_ui = text.starts_with("[ui]");
    } else if (in_ui && text.starts_with(kKey) && trimmed(text.substr(kKey.size())).starts_with('=')) {
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
  const auto text_of = [](const auto& value) {
    std::ostringstream text;
    text << value;
    return text.str();
  };
  const auto saves = [&](const std::string& text) {
    try {
      return toml::parse(text)["ui"]["pinned_ships"].value<std::string>() == joined;
    } catch (const toml::parse_error&) {
      return false;
    }
  };

  const auto path     = File::Path(File::Config(), true);
  const auto settings = [&] {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
  }();
  auto updated = WithPinnedShipsLine(settings, "pinned_ships = " + text_of(toml::value<std::string>(joined)));
  if (!saves(updated)) {
    try {
      auto table = toml::parse(settings);
      if (!table["ui"].is_table())
        table.insert_or_assign("ui", toml::table{});
      table["ui"].as_table()->insert_or_assign("pinned_ships", joined);
      updated = text_of(table);
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
    spdlog::info("[PinnedShipClick] unpinned '{}'", ShipNameText(key));
    names.erase(found);
  } else {
    spdlog::info("[PinnedShipClick] pinned '{}'", ShipNameText(key));
    names.push_back(std::move(key));
  }
  SavePinnedShips(names);
  RefreshDock(controller);
}

// --- Double click and Enter to assign ---

constexpr auto kDoubleClickWindow = std::chrono::milliseconds(400);

// See pin_click_installed.
bool double_click_installed = false;
// Compared only, never dereferenced.
FleetPlayerData*                      last_clicked_ship = nullptr;
std::chrono::steady_clock::time_point last_click_time{};

void PressAssignButton()
{
  for (auto* widget : unity::Instances<AssignShipsWidget>()) {
    if (!CanvasController::OnScreen(widget))
      continue;
    auto* wrapper = widget->assignButton();
    auto* inner   = wrapper ? wrapper->Widget() : nullptr;
    // Only the first shown dock is pressed, even when its button cannot be read.
    if (auto* button = inner ? inner->UnityButton() : nullptr)
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
  // Down, not Pressed: a held Enter must press Assign once, not every frame.
  if (Config::Get().ui.double_click_to_assign_ship
      && (unity::Key::Down(unity::KeyCode::Return) || unity::Key::Down(unity::KeyCode::KeypadEnter))
      && !unity::Key::IsInputFocused())
    PressAssignButton();
}

// --- Ship tile click ---

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

// --- Dock selection ---

// The fleet open in ship management, when it holds a ship and has a dock slot.
FleetPlayerData* ManagedDockFleet()
{
  auto* sections = Hub::Sections();
  if (!Config::Get().ui.dock_syncmanagement || !sections
      || sections->CurrentSection() != SectionID::ShipManagement_Details)
    return nullptr;
  auto* controller = VisibleShipManagement();
  auto* context    = controller ? controller->Context() : nullptr;
  auto* fleet      = context ? context->CurrentFleet() : nullptr;
  auto* manager    = FleetsManager::Instance();
  if (!fleet || !fleet->HasShip() || !manager || !manager->HasFleetService())
    return nullptr;
  // Unassigned and synthetic fleets have no dock slot; the management list order is not the dock order.
  const auto index      = fleet->Index();
  auto*      dock_fleet = index >= 0 ? manager->GetFleetPlayerData(index) : nullptr;
  return dock_fleet && dock_fleet->Id() == fleet->Id() ? fleet : nullptr;
}

void DockSelectionFrame()
{
  // Ten times a second; selecting a slot is not urgent.
  static Throttle throttle(std::chrono::milliseconds(100));
  static int64_t  last_fleet_id = 0;
  if (!throttle.Due())
    return;
  auto* fleet = ManagedDockFleet();
  if (!fleet) {
    last_fleet_id = 0;
    return;
  }
  const auto id  = fleet->Id();
  auto*      bar = unity::Newest<FleetBarViewController>();
  if (id == last_fleet_id || !bar)
    return;
  if (const auto index = fleet->Index(); !bar->IsIndexSelected(index)) {
    // The bar's own selection flow updates both the highlight and the fleet panel.
    bar->RequestSelect(index);
    // Blocked by the game: try again next time.
    if (!bar->IsIndexSelected(index))
      return;
    spdlog::debug("[DockSelection] selected dock slot {} for fleet {}", index + 1, id);
  }
  last_fleet_id = id;
}

// --- Installation ---

bool InstallPinnedShips()
{
  il2cpp::HookSet hooks("PinnedShips");
  IL2CPP_STAGE(hooks, ShipManagementViewContext::Class::Method("Sort", 0), ShipManagementViewContext_Sort_Hook);
  return hooks.Commit();
}

// Shared by Shift+click pinning and double-click assignment, whichever of them install, so it is its own set,
// installed on the first call.
bool InstallShipTileClick()
{
  static const bool tile_click_installed = [] {
    il2cpp::HookSet hooks("ShipTileClick");
    IL2CPP_STAGE(hooks, ShipTileWidget::Class::Method("HandleOnClick", 0), ShipTileWidget_HandleOnClick_Hook);
    return hooks.Commit();
  }();
  return tile_click_installed;
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

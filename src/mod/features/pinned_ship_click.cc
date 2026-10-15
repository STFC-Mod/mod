#include "config.h"
#include "features/features.h"
#include "file.h"

#include "text.h"

#include "game/fleet.h"
#include "game/ui.h"
#include "pinned_ship_click.h"
#include "ship_tile_click.h"

#include <il2cpp/il2cpp.h>
#include <unity/input.h>
#include <unity/tracker.h>

#include <spdlog/spdlog.h>
#include <toml++/toml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <spdlog/fmt/ranges.h>
#include <string>
#include <vector>

namespace
{

bool IsShiftHeld()
{ return unity::Key::Pressed(unity::KeyCode::LeftShift) || unity::Key::Pressed(unity::KeyCode::RightShift); }

// Presence of a visible ShipManagementViewController instead of a SectionID
// check: the dock is hosted under more than one game section, and this keeps
// the gate in lockstep with the screen the pinned sort hook drives.
bool IsShipManagementScreenVisible()
{
  for (auto* controller : unity::Instances<ShipManagementViewController>()) {
    if (!controller || !controller->isActiveAndEnabled())
      continue;

    auto canvas = CanvasController::Of(controller);
    if (canvas && canvas->Visible())
      return true;
  }
  return false;
}

std::string JoinNames(const std::vector<std::vector<std::string>>& names)
{
  std::string joined;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i != 0)
      joined += ", ";
    joined += fmt::format("{}", fmt::join(names[i], " "));
  }
  return joined;
}

// Update ui.pinned_ships in the user's config file so pins survive a restart.
// If/when we merge the runtime config writer we can route this through that instead of writing the file directly.
void PersistPinnedShips(const std::vector<std::vector<std::string>>& names)
{
  const auto joined = JoinNames(names);

  toml::table table;
  try {
    if (std::filesystem::exists(File::Path(File::Config()))) {
      table = toml::parse_file(File::Path(File::Config()).string());
    }
  } catch (const std::exception& error) {
    spdlog::warn("[PinnedShipClick] could not read '{}' to persist pinned ships: {}", File::Config(), error.what());
    return;
  }

  if (!table["ui"].is_table()) {
    table.emplace<toml::table>("ui", toml::table{});
  }
  if (auto ui = table["ui"].as_table()) {
    ui->insert_or_assign("pinned_ships", joined);
  }

  std::ofstream file(File::Path(File::Config(), true));
  file << table;
  file.close();
  if (!file)
    spdlog::warn("[PinnedShipClick] could not persist pinned ships to '{}'", File::Config());
}

// Re-runs the pinned sort and rebuilds the dock's tile list through the
// game's own SelectableListBase.ClearAndGenerateContents(IDataContextProvider,
// IList): the controller implements IDataContextProvider and the list binds
// the reordered SortedIdleShips directly.
void RefreshPinnedShipSortNow()
{
  for (auto* controller : unity::Instances<ShipManagementViewController>()) {
    if (!controller || !controller->isActiveAndEnabled())
      continue;

    auto canvas = CanvasController::Of(controller);
    if (!canvas || !canvas->Visible())
      continue;

    auto* context = controller->Context();
    if (!context)
      continue;
    context->Sort();

    for (auto* widget : unity::Instances<AssignShipsWidget>()) {
      if (!widget || !widget->isActiveAndEnabled())
        continue;

      auto* list = widget->selectableList();
      if (!list)
        continue;

      list->Rebuild(controller);
    }
  }
}

} // namespace

void PinShipClickOnTileClicked(FleetPlayerData* ship)
{
  auto& cfg = Config::Get();
  if (!cfg.patches.pinnedshipclicktoggle)
    return;
  if (!IsShiftHeld())
    return;
  if (!IsShipManagementScreenVisible())
    return;
  if (!ship)
    return;

  const auto key = ShipNameWords(ship->DisplayName());
  if (key.empty())
    return; // placeholder tiles (e.g. "build ship") have no name

  auto& names = cfg.ui.pinned_ships;

  const bool was_pinned = std::find(names.begin(), names.end(), key) != names.end();
  if (was_pinned) {
    names.erase(std::find(names.begin(), names.end(), key));
    spdlog::info("[PinnedShipClick] unpinned ship '{}'", fmt::join(key, " "));
  } else {
    names.push_back(key);
    spdlog::info("[PinnedShipClick] pinned ship '{}'", fmt::join(key, " "));
  }

  PersistPinnedShips(names);

  RefreshPinnedShipSortNow();
}

namespace
{
void InstallPinnedShipClickHooks()
{
  // The HandleOnClick detour is owned by the shared ship-tile click
  // dispatcher; installing a second detour is not allowed, especially on macOS.
  if (!ship_tile_click::TryInstallHook()) {
    spdlog::error("[PinnedShipClick] shared ship tile click hook installation failed");
  } else {
    spdlog::info("Pinned ship click: hooks installed");
  }
}
} // namespace

const Feature kPinnedShipClick{"PinnedShipClick", &Config::Patches::pinnedshipclicktoggle, InstallPinnedShipClickHooks};

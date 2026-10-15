#include "patches/object_tracker.h"
#include "config.h"
#include "file.h"
#include "patches/input.h"
#include "ship_name_match.h"

#include "pinned_ship_click.h"
#include "prime/fleet.h"
#include "prime/ui.h"
#include "prime/fleet.h"
#include "prime/fleet.h"
#include "ship_tile_click.h"

#include <il2cpp/il2cpp-functions.h>
#include <il2cpp/il2cpp.h>

#include <spdlog/spdlog.h>
#include <toml++/toml.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

bool IsShiftHeld()
{
  return Key::HasShift();
}

// Presence of a visible ShipManagementViewController instead of a SectionID
// check: the dock is hosted under more than one game section, and this keeps
// the gate in lockstep with the screen the pinned sort hook drives.
bool IsShipManagementScreenVisible()
{
  for (auto* controller : ObjectFinder<ShipManagementViewController>::GetAll()) {
    if (!controller || !controller->isActiveAndEnabled()) continue;

    auto canvas = GetCanvasControllerFromComponent(controller);
    if (canvas && canvas->Visible()) return true;
  }
  return false;
}

std::string JoinNames(const std::vector<std::string>& names)
{
  std::string joined;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i != 0) joined += ", ";
    joined += names[i];
  }
  return joined;
}

// Update ui.pinned_ships in the user's config file so pins survive a restart.
// If/when we merge the runtime config writer we can route this through that instead of writing the file directly.
void PersistPinnedShips(const std::vector<std::string>& names)
{
  const auto joined = JoinNames(names);

  toml::table table;
  try {
    if (std::filesystem::exists(File::Config())) {
      table = toml::parse_file(File::Config());
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

  Config::Save(table, File::Config(), false);
}

const MethodInfo* FindMethodInHierarchy(Il2CppClass* cls, const char* name, int argc)
{
  for (; cls; cls = il2cpp_class_get_parent(cls)) {
    auto* m = il2cpp_class_get_method_from_name(cls, name, argc);
    if (m) return m;
  }
  return nullptr;
}

// Re-runs the pinned sort and rebuilds the dock's tile list through the
// game's own SelectableListBase.ClearAndGenerateContents(IDataContextProvider,
// IList): the controller implements IDataContextProvider and the list binds
// the reordered SortedIdleShips directly.
void RefreshPinnedShipSortNow()
{
  for (auto* controller : ObjectFinder<ShipManagementViewController>::GetAll()) {
    if (!controller || !controller->isActiveAndEnabled()) continue;

    auto canvas = GetCanvasControllerFromComponent(controller);
    if (!canvas || !canvas->Visible()) continue;

    auto* context = controller->Context();
    if (!context) continue;
    context->Sort();

    for (auto* widget : ObjectFinder<AssignShipsWidget>::GetAll()) {
      if (!widget || !widget->isActiveAndEnabled()) continue;

      auto* list = widget->selectableList();
      if (!list) continue;

      auto* list_cls = il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(list));
      auto* get_data = FindMethodInHierarchy(list_cls, "get_Data", 0);
      auto* rebuild  = FindMethodInHierarchy(list_cls, "ClearAndGenerateContents", 2);
      if (!get_data || !rebuild) continue;

      Il2CppException* exc  = nullptr;
      auto*            data = il2cpp_runtime_invoke(get_data, list, nullptr, &exc);
      if (exc || !data) continue;

      void* args[2] = {controller, data};
      exc           = nullptr;
      il2cpp_runtime_invoke(rebuild, list, args, &exc);
      if (exc) {
        spdlog::warn("[PinnedShipClick] ClearAndGenerateContents raised an exception");
      }
    }
  }
}

} // namespace

void PinShipClickOnTileClicked(FleetPlayerData* ship)
{
  auto& cfg = Config::Get();
  if (!cfg.installPinnedShipClickToggle) return;
  if (!IsShiftHeld()) return;
  if (!IsShipManagementScreenVisible()) return;
  if (!ship) return;

  const auto key = ShipNameMatch::NormalizeKey(ShipNameMatch::GameDisplayName(ship));
  if (key.empty()) return; // placeholder tiles (e.g. "build ship") have no name

  auto& names = cfg.pinned_ships;

  const bool was_pinned = std::find(names.begin(), names.end(), key) != names.end();
  if (was_pinned) {
    names.erase(std::find(names.begin(), names.end(), key));
    spdlog::info("[PinnedShipClick] unpinned ship '{}'", key);
  } else {
    names.push_back(key);
    spdlog::info("[PinnedShipClick] pinned ship '{}'", key);
  }

  PersistPinnedShips(names);

  RefreshPinnedShipSortNow();
}

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

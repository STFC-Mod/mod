// pinned_ships: moves the named ships to the front of the dock's idle ship list, after the game sorts it. Within
// the pinned group and the rest, the game's active sort order is kept.
//
// Only ShipManagementViewContext.Sort is hooked, not ISortedFleetsContext, so the hailing frequencies inventory is
// left alone.

#include "config.h"
#include "patches/patches.h"
#include "ship_name.h"

#include <prime/fleet.h>

#include <spdlog/fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <set>
#include <vector>

namespace
{
// Unmatched entries are warned about once, not on every sort.
std::set<std::string> warned;

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
  list->Call<"Clear">();
  for (const auto& ship : ships)
    list->Add(ship.data);
}

void ShipManagementViewContext_Sort_Hook(auto original, ShipManagementViewContext* _this)
{
  original(_this);
  PinShips(_this->SortedIdleShips());
}
} // namespace

void InstallPinnedShipSortHooks()
{
  if (auto m = ShipManagementViewContext::Class::Method("Sort", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), ShipManagementViewContext_Sort_Hook);
}

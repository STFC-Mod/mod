#include "config.h"
#include "ship_name_match.h"

#include <prime/FleetPlayerData.h>
#include <prime/ShipManagementViewContext.h>

#include <il2cpp/il2cpp.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <cstdint>
#include <vector>

// Fleet management dock ship sort: hooks ShipManagementViewContext.Sort()
// and stable-partitions cfg.pinned_ships to the front of SortedIdleShips,
// keeping the active sort field's order within both the pinned group and
// everyone else. Only the concrete class is hooked, not ISortedFleetsContext,
// so HailingFrequenciesInventoryContext is unaffected. Matching is shared
// with the instant-warp filters (ship_name_match.h). If multiple ships share
// a pinned name, only the highest-level unclaimed one is pinned.

namespace {

struct ShipEntry {
  FleetPlayerData* item;
  std::vector<std::string> match_words; // words from the game's own display name
  int64_t       level;
  int64_t                  hull_id      = 0;
  int8_t                   tech_applied = -1;

  bool TechApplied()
  {
    if (tech_applied < 0)
      tech_applied = item && item->HasAppliedTech() ? 1 : 0;
    return tech_applied == 1;
  }
};

ShipEntry BuildShipEntry(FleetPlayerData* ship)
{
  ShipEntry entry{ship, {}, 0};
  if (!ship) return entry;

  entry.level       = ship->Level();
  entry.hull_id     = ship->HullId();
  entry.match_words = ShipNameMatch::DisplayWords(ship);
  return entry;
}

// Stable-partitions `list` (a List<FleetPlayerData>) so that ships matching
// cfg.pinned_ships come first, while both the pinned group and everyone else
// keep their existing relative order (i.e. whatever sort field is active).
void ReorderPinnedShips(il2cpp::List<FleetPlayerData*>* list)
{
  if (!list) return;

  const auto& cfg = Config::Get();
  if (cfg.pinned_ships.empty()) return;

  const auto count = list->Count();
  if (count <= 1) return;
  if (count > 2000) {
    spdlog::warn("[PinnedShipSort] SortedIdleShips reported an implausible count ({}); skipping", count);
    return;
  }

  // cfg.pinned_ships entries are already normalized (see parse_ship_filter /
  // ShipNameMatch::NormalizeKey in config.cc); just split them into words.
  std::vector<std::vector<std::string>> pinned_words;
  pinned_words.reserve(cfg.pinned_ships.size());
  for (const auto& name : cfg.pinned_ships) pinned_words.push_back(ShipNameMatch::SplitWords(name));

  std::vector<ShipEntry> ships;
  ships.reserve(count);
  for (int32_t i = 0; i < count; ++i)
    ships.push_back(BuildShipEntry((*list)[i]));

  constexpr int    kPinnedRank   = 0;
  constexpr int    kUnpinnedRank = 1;
  std::vector<int> ranks(count, kUnpinnedRank);

  bool any_pinned = false;
  for (size_t p = 0; p < pinned_words.size(); ++p) {
    if (pinned_words[p].empty()) continue;

    int     best_index = -1;
    int64_t best_level = -1;
    for (int32_t i = 0; i < count; ++i) {
      if (ranks[i] != kUnpinnedRank) continue; // already claimed by an earlier pinned_ships entry
      if (!ShipNameMatch::MatchesDisplay(ships[i].match_words, pinned_words[p])) continue;
      if (ships[i].level > best_level) {
        best_level = ships[i].level;
        best_index = i;
      } else if (best_index >= 0 && ships[i].level == best_level && ships[i].hull_id == ships[best_index].hull_id
                 && ships[i].TechApplied() && !ships[best_index].TechApplied()) {
        best_index = i;
      }
    }

    if (best_index >= 0) {
      ranks[best_index] = kPinnedRank;
      any_pinned        = true;
    } else {
      spdlog::warn("[PinnedShipSort] pinned_ships entry '{}' matched no idle ship (entries match the "
                   "game's own display name for a ship that is idle in its dock)",
                   cfg.pinned_ships[p]);
    }
  }

  if (!any_pinned) return;

  std::vector<FleetPlayerData*> items;
  items.reserve(count);
  for (const auto& entry : ships) items.push_back(entry.item);

  std::vector<int> order(count);
  for (int32_t i = 0; i < count; ++i) order[i] = i;
  std::ranges::stable_sort(order, [&](int a, int b) { return ranks[a] < ranks[b]; });

  bool changed = false;
  for (int32_t i = 0; i < count; ++i) {
    if (order[i] != i) {
      changed = true;
      break;
    }
  }
  if (!changed) return;

  // SortedIdleShips is a List<T>, which always has Clear and Add; the rewrite cannot stop halfway.
  list->Call<"Clear">();
  for (int32_t i = 0; i < count; ++i)
    list->Add(items[order[i]]);
}

void ShipManagementViewContext_Sort_Hook(auto original, ShipManagementViewContext* _this)
{
  original(_this);
  ReorderPinnedShips(_this->SortedIdleShips());
}

} // namespace

void InstallPinnedShipSortHooks()
{
  if (auto m = ShipManagementViewContext::Class::Method("Sort", 0)) {
    SPUD_STATIC_DETOUR(m.Pointer(), ShipManagementViewContext_Sort_Hook);
    spdlog::info("Pinned ship sort: hooks installed");
  }
}

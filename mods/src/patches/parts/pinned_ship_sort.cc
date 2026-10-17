#include "config.h"
#include "errormsg.h"
#include "ship_name_match.h"

#include <prime/FleetPlayerData.h>
#include <prime/HullSpec.h>

#include <il2cpp/il2cpp-functions.h>
#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>

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
// a pinned name, only the highest-level unclaimed one is pinned; among
// same-hull ships tied on level, the one with forbidden/chaos tech applied
// wins.

namespace {

// Forbidden tech / chaos tech slot spec ids, shared with the ship tech
// indicators patch (ship_tech_indicators.cc). A ship counts as "tech
// applied" when either slot holds an item.
constexpr int64_t kForbiddenTechSlotSpecId = 3502081615;
constexpr int64_t kChaosTechSlotSpecId     = 953301906;

struct PinnedShipSortState {
  IL2CppClassHelper* shipManagementViewContext = nullptr;
  IL2CppFieldHelper* sortedIdleShipsField      = nullptr;
  bool               valid                     = false;
};

PinnedShipSortState& State()
{
  static PinnedShipSortState s;
  return s;
}

bool InitializeState()
{
  auto& s = State();

  s.shipManagementViewContext = new IL2CppClassHelper(
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementViewContext"));
  if (!s.shipManagementViewContext->get_cls()) {
    ErrorMsg::MissingHelper("Digit.Prime.Ships", "ShipManagementViewContext");
    return false;
  }

  s.sortedIdleShipsField = new IL2CppFieldHelper(s.shipManagementViewContext->GetField("SortedIdleShips"));
  if (!s.sortedIdleShipsField->isValidHelper()) {
    spdlog::error("[PinnedShipSort] ShipManagementViewContext field layout changed");
    return false;
  }

  s.valid = true;
  return true;
}

// Bindings for asking ForbiddenTechManager whether a ship has forbidden or
// chaos tech equipped. Resolved once on first use; if anything is missing the
// tech tie-break quietly degrades to "no tech" (previous behaviour).
struct ShipTechQuery {
  const MethodInfo* manager_instance = nullptr;
  const MethodInfo* active_slots     = nullptr;
  const MethodInfo* slot_spec        = nullptr;
  const MethodInfo* slot_item        = nullptr;
  const MethodInfo* ship_id          = nullptr;
  bool              valid            = false;
  bool              resolved         = false;
};

ShipTechQuery& TechQuery()
{
  static ShipTechQuery s;
  return s;
}

void ResolveShipTechQuery()
{
  auto& s = TechQuery();
  if (s.resolved) return;
  s.resolved = true;

  auto manager     = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechManager");
  auto entity_slot = il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "EntitySlot");
  auto base_ship   = il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BaseShip");
  if (!manager.get_cls() || !entity_slot.get_cls() || !base_ship.get_cls()) {
    spdlog::warn("[PinnedShipSort] forbidden tech bindings unavailable; tech tie-break disabled");
    return;
  }

  s.manager_instance = manager.GetParent("MonoSingleton`1").GetMethodInfo("get_Instance", 0);
  s.active_slots     = manager.GetMethodInfo("TryGetActiveForbiddenTechSlot", 2);
  s.slot_spec        = method_contract::Resolve(entity_slot.get_cls(), "get_SlotSpecId", false, "System.Int64", {});
  s.slot_item        = entity_slot.GetMethodInfo("get_SlotItemId", 0);
  s.ship_id          = method_contract::Resolve(base_ship.get_cls(), "get_Id", false, "System.Int64", {});

  const bool manager_instance_valid = s.manager_instance && s.manager_instance->methodPointer
                                       && (s.manager_instance->flags & METHOD_ATTRIBUTE_STATIC)
                                       && s.manager_instance->parameters_count == 0
                                       && method_contract::Type(s.manager_instance->return_type,
                                                                "Digit.Prime.ForbiddenTechs.ForbiddenTechManager");
  const bool active_slots_valid = s.active_slots && s.active_slots->methodPointer
                                  && !(s.active_slots->flags & METHOD_ATTRIBUTE_STATIC)
                                  && method_contract::Type(s.active_slots->return_type, "System.Boolean")
                                  && s.active_slots->parameters_count == 2
                                  && method_contract::Type(s.active_slots->parameters[0], "System.Int64")
                                  && s.active_slots->parameters[1]->byref;
  const bool slot_spec_valid = s.slot_spec != nullptr;
  const bool slot_item_valid = s.slot_item && s.slot_item->methodPointer
                               && !(s.slot_item->flags & METHOD_ATTRIBUTE_STATIC) && s.slot_item->parameters_count == 0
                               && s.slot_item->return_type->type == IL2CPP_TYPE_GENERICINST;
  const bool ship_id_valid = s.ship_id != nullptr;

  s.valid = manager_instance_valid && active_slots_valid && slot_spec_valid && slot_item_valid && ship_id_valid;
  if (!s.valid) {
    spdlog::warn("[PinnedShipSort] forbidden tech bindings failed validation; tech tie-break disabled");
  }
}

// True when the ship has forbidden tech or chaos tech equipped in either of
// its tech slots. Best-effort: any failure counts as "no tech".
bool HasAppliedTech(FleetPlayerData* ship)
{
  ResolveShipTechQuery();
  auto& s = TechQuery();
  if (!ship || !s.valid) return false;

  auto* ship_model = ship->__get_Ship();
  if (!ship_model) return false;

  Il2CppException* exc      = nullptr;
  auto*            boxed_id = il2cpp_runtime_invoke(s.ship_id, ship_model, nullptr, &exc);
  if (exc || !boxed_id) return false;
  int64_t ship_id = *static_cast<int64_t*>(il2cpp_object_unbox(boxed_id));

  exc           = nullptr;
  auto* manager = il2cpp_runtime_invoke(s.manager_instance, nullptr, nullptr, &exc);
  if (exc || !manager) return false;

  Il2CppObject* slots        = nullptr;
  void*         slot_args[2] = {&ship_id, &slots};
  exc                        = nullptr;
  auto* found                = il2cpp_runtime_invoke(s.active_slots, manager, slot_args, &exc);
  if (exc || !found || !*static_cast<bool*>(il2cpp_object_unbox(found)) || !slots) return false;

  auto* slots_class = il2cpp_object_get_class(slots);
  if (!slots_class) return false;
  auto* get_count = il2cpp_class_get_method_from_name(slots_class, "get_Count", 0);
  auto* get_item  = il2cpp_class_get_method_from_name(slots_class, "get_Item", 1);
  if (!get_count || !get_item) return false;

  exc               = nullptr;
  auto* count_obj   = il2cpp_runtime_invoke(get_count, slots, nullptr, &exc);
  if (exc || !count_obj) return false;
  const auto count = *static_cast<int32_t*>(il2cpp_object_unbox(count_obj));

  for (int32_t i = 0; i < count; ++i) {
    exc          = nullptr;
    void* idx[1] = {&i};
    auto* slot   = il2cpp_runtime_invoke(get_item, slots, idx, &exc);
    if (exc || !slot) continue;

    exc            = nullptr;
    auto* spec_obj = il2cpp_runtime_invoke(s.slot_spec, slot, nullptr, &exc);
    if (exc || !spec_obj) continue;
    const auto spec_id = *static_cast<int64_t*>(il2cpp_object_unbox(spec_obj));
    if (spec_id != kForbiddenTechSlotSpecId && spec_id != kChaosTechSlotSpecId) continue;

    exc        = nullptr;
    auto* item = il2cpp_runtime_invoke(s.slot_item, slot, nullptr, &exc);
    if (!exc && item) return true; // non-null Nullable<Int64>: a tech is equipped
  }
  return false;
}

struct ShipEntry {
  void*                    item;
  std::vector<std::string> match_words; // words from the game's own display name
  int64_t                  level;
  int64_t                  hull_id;
  int8_t                   tech_applied; // -1 unknown, 0 no, 1 yes (resolved lazily)

  // Memoized so the manager is only queried when a level tie actually happens.
  bool TechApplied()
  {
    if (tech_applied < 0) {
      tech_applied = HasAppliedTech(reinterpret_cast<FleetPlayerData*>(item)) ? 1 : 0;
    }
    return tech_applied == 1;
  }
};

ShipEntry BuildShipEntry(void* item)
{
  auto* ship = reinterpret_cast<FleetPlayerData*>(item);
  if (!ship) return ShipEntry{item, {}, 0, 0, -1};

  return ShipEntry{item, ShipNameMatch::DisplayWords(ship), ship->Level, ship->HullId, -1};
}

// Stable-partitions `list` (a List<FleetPlayerData>) so that ships matching
// cfg.pinned_ships come first, while both the pinned group and everyone else
// keep their existing relative order (i.e. whatever sort field is active).
void ReorderPinnedShips(void* list)
{
  if (!list) return;

  const auto& cfg = Config::Get();
  if (cfg.pinned_ships.empty()) return;

  auto* listClass = il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(list));
  if (!listClass) return;

  auto* getCount = il2cpp_class_get_method_from_name(listClass, "get_Count", 0);
  auto* getItem  = il2cpp_class_get_method_from_name(listClass, "get_Item", 1);
  auto* clear    = il2cpp_class_get_method_from_name(listClass, "Clear", 0);
  auto* add      = il2cpp_class_get_method_from_name(listClass, "Add", 1);
  if (!getCount || !getItem || !clear || !add) {
    spdlog::warn("[PinnedShipSort] SortedIdleShips list is missing expected List<T> methods");
    return;
  }

  Il2CppException* exc = nullptr;
  auto* countObj = il2cpp_runtime_invoke(getCount, list, nullptr, &exc);
  if (exc || !countObj) return;
  const auto count = *reinterpret_cast<int32_t*>(il2cpp_object_unbox(countObj));
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
  for (int32_t i = 0; i < count; ++i) {
    void* idxArgs[1] = {&i};
    exc              = nullptr;
    auto* item       = il2cpp_runtime_invoke(getItem, list, idxArgs, &exc);
    if (exc) item = nullptr;

    auto entry = BuildShipEntry(item);
    ships.push_back(std::move(entry));
  }

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
        // Same-level, same-hull tie: prefer the ship with forbidden/chaos tech applied.
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

  std::vector<void*> items;
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

  exc = nullptr;
  il2cpp_runtime_invoke(clear, list, nullptr, &exc);
  if (exc) {
    spdlog::warn("[PinnedShipSort] list.Clear raised an exception; aborting reorder");
    return;
  }

  for (int32_t i = 0; i < count; ++i) {
    void* addArgs[1] = {items[order[i]]};
    exc               = nullptr;
    il2cpp_runtime_invoke(add, list, addArgs, &exc);
    if (exc) {
      spdlog::warn("[PinnedShipSort] list.Add raised an exception while re-inserting an item");
    }
  }
}

void ShipManagementViewContext_Sort_Hook(auto original, void* _this)
{
  original(_this);

  auto& s = State();
  if (!s.valid) return;

  auto* list = *reinterpret_cast<void**>(reinterpret_cast<char*>(_this) + s.sortedIdleShipsField->offset());
  ReorderPinnedShips(list);
}

} // namespace

void InstallPinnedShipSortHooks()
{
  if (!InitializeState()) {
    spdlog::error("[PinnedShipSort] initialization failed; hooks not installed");
    return;
  }

  auto& s = State();

  auto sortMethod = s.shipManagementViewContext->GetMethod("Sort", 0);
  if (!sortMethod) {
    ErrorMsg::MissingMethod("ShipManagementViewContext", "Sort");
    return;
  }

  SPUD_STATIC_DETOUR(sortMethod, ShipManagementViewContext_Sort_Hook);
  spdlog::info("Pinned ship sort: hooks installed");
}

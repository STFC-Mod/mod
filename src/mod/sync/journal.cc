// Protobuf battle journals rebuilt in the JSON journal shape. Keys follow the old JSON journals: values the wire
// does not carry are left out, never set to 0 or invented, and explicit nulls appear only where the JSON journals
// had them.

#include "sync/journal.h"

#include <Digit.PrimeServer.Models.pb.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>
#include <system_error>

namespace data_sync
{
namespace
{
  using json   = nlohmann::json;
  namespace pb = Digit::PrimeServer::Models;

  constexpr int32_t kBattleReportGroup = pb::EntityGroup::TYPE_BATTLEREPORT;
  // Larger bodies are not parsed; real journals are a few hundred kilobytes.
  constexpr size_t kMaxJournalBytes = 64u << 20;
  // Bridge crews have 10 slots; a slot index past this is dropped rather than allocated.
  constexpr int64_t kMaxOfficerSlots = 64;

  // --- Values ---

  json Integer(int64_t value)
  { return value; }

  // float32 in its shortest round-trip form: 1.1, not 1.100000023841858. Integral values stay doubles (48241632.0),
  // as in the JSON journals.
  json Float(float value)
  {
    if (!std::isfinite(value))
      return nullptr;
    if (std::trunc(value) == value)
      return static_cast<double>(value);
    char buffer[32];
    if (const auto [end, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value); ec == std::errc{}) {
      // Float from_chars needs macOS 26 in Apple's libc++; the JSON parser reads the shortest form back instead.
      if (auto parsed = json::parse(std::begin(buffer), end, nullptr, false); parsed.is_number_float())
        return parsed;
    }
    return static_cast<double>(value);
  }

  // A float for a key that was an integer in the JSON journals (ratings, ship hit points).
  json Rounded(float value)
  {
    if (!std::isfinite(value) || std::fabs(value) >= 9.2e18f)
      return nullptr;
    return static_cast<int64_t>(std::llround(value));
  }

  // "YYYY-MM-DDTHH:MM:SS" in UTC, with ".ffffff" only when the timestamp has nanoseconds.
  std::string Time(const google::protobuf::Timestamp& time)
  {
    using namespace std::chrono;
    // Far beyond any real date; keeps the calendar arithmetic in range.
    constexpr int64_t kLimit   = int64_t{1} << 40;
    const auto        seconds_ = std::clamp<int64_t>(time.seconds(), -kLimit, kLimit);
    const auto        instant  = sys_seconds{seconds{seconds_}};
    const auto        day      = floor<days>(instant);
    const auto        date     = year_month_day{day};
    const auto        clock    = hh_mm_ss{instant - day};
    char              text[64];
    std::snprintf(text, sizeof(text), "%04d-%02u-%02uT%02d:%02d:%02d", static_cast<int>(date.year()),
                  static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                  static_cast<int>(clock.hours().count()), static_cast<int>(clock.minutes().count()),
                  static_cast<int>(clock.seconds().count()));
    std::string result = text;
    if (time.nanos() != 0) {
      std::snprintf(text, sizeof(text), ".%06d", std::abs(time.nanos() / 1000));
      result += text;
    }
    return result;
  }

  json TimeOrNull(bool present, const google::protobuf::Timestamp& time)
  { return present ? json(Time(time)) : json(nullptr); }

  // Protobuf maps become objects with decimal-string keys; json sorts them as the JSON journals were sorted.
  template <typename Map, typename Value> json Object(const Map& map, Value&& value)
  {
    auto result = json::object();
    for (const auto& entry : map)
      result[std::to_string(entry.first)] = value(entry.second);
    return result;
  }

  template <typename Repeated, typename Value> json Array(const Repeated& items, Value&& value)
  {
    auto result = json::array();
    for (const auto& item : items)
      result.push_back(value(item));
    return result;
  }

  const auto kInteger = [](int64_t value) { return Integer(value); };
  const auto kFloat   = [](float value) { return Float(value); };
  const auto kString  = [](const std::string& value) { return json(value); };

  json IdRefs(const pb::IdRefs& refs)
  {
    json result{
        {"art_file_reference", refs.artfilereference().empty() ? json(nullptr) : json(refs.artfilereference())}};
    if (refs.artid() != 0)
      result["art_id"] = refs.artid();
    if (refs.locaid() != 0)
      result["loca_id"] = refs.locaid();
    return result;
  }

  json Cargo(const pb::CargoProto& cargo)
  { return {{"resources", Object(cargo.resources(), kInteger)}}; }

  // A battle log entry is the server's Python repr of a number ("-96", "1.2000000000000002", "5.0"). It becomes the
  // JSON number with the same text, so integers stay integers and "5.0" stays a float. Anything else stays text.
  json LogEntry(const std::string& text)
  {
    size_t     i     = 0;
    const auto digit = [&](size_t at) { return at < text.size() && text[at] >= '0' && text[at] <= '9'; };
    const auto run   = [&] {
      if (!digit(i))
        return false;
      while (digit(i))
        ++i;
      return true;
    };
    bool integral = true;
    if (i < text.size() && text[i] == '-')
      ++i;
    if (digit(i) && text[i] == '0')
      ++i;
    else if (!run())
      return text;
    if (i < text.size() && text[i] == '.') {
      integral = false;
      ++i;
      if (!run())
        return text;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
      integral = false;
      ++i;
      if (i < text.size() && (text[i] == '+' || text[i] == '-'))
        ++i;
      if (!run())
        return text;
    }
    if (i != text.size())
      return text;

    const auto* begin = text.data();
    const auto* end   = text.data() + text.size();
    if (integral) {
      if (int64_t value; std::from_chars(begin, end, value) == std::from_chars_result{end, std::errc{}})
        return value;
      if (uint64_t value; std::from_chars(begin, end, value) == std::from_chars_result{end, std::errc{}})
        return value;
    }
    if (auto parsed = json::parse(begin, end, nullptr, false);
        parsed.is_number() && (!parsed.is_number_float() || std::isfinite(parsed.get<double>())))
      return parsed;
    return text;
  }

  // --- Chests ---

  json Item(const pb::InventoryItem& item, const pb::IdRefs* paired_refs)
  {
    const auto& common = item.commonparams();
    const auto  type   = static_cast<int32_t>(item.type());
    json        params{{"ref_id", common.refid()}};
    if (type == pb::INVENTORYITEMTYPE_INVENTORYCHEST) {
      const bool  has_chest = item.has_chestparams();
      const auto& chest     = item.chestparams();
      if (common.id() != 0)
        params["id"] = common.id();
      if (has_chest && chest.has_idrefs())
        params["id_refs"] = IdRefs(chest.idrefs());
      else if (paired_refs)
        params["id_refs"] = IdRefs(*paired_refs);
      // Today's wire carries no chest contents; they appear here if the server sends them again.
      if (has_chest && chest.chestitems_size() > 0) {
        auto items = json::array();
        for (const auto& content : chest.chestitems()) {
          items.push_back(Item(content, nullptr));
          const auto ref                          = std::to_string(content.commonparams().refid());
          params["chest_item_" + ref + "_amount"] = content.count();
          params["chest_item_" + ref + "_type"]   = static_cast<int32_t>(content.type());
        }
        params["items"] = std::move(items);
      }
      if (has_chest && chest.chestsource() != 0)
        params["source"] = static_cast<int32_t>(chest.chestsource());
    }
    return {{"count", item.count()},
            {"expiry_time", TimeOrNull(item.has_expirytime(), item.expirytime())},
            {"params", std::move(params)},
            {"type", type}};
  }

  // --- Fleets ---

  struct Side {
    std::optional<json> opponent_fleet_id; // Left out when the opponent has no fleet.
    bool                won = false;
  };

  json DeployedFleet(const pb::DeployedFleet& fleet, const Side& side)
  {
    const auto& attributes = fleet.attributes();
    const auto& stats      = fleet.stats();
    json        result;

    // Keys absent from the wire are left out, never sent as 0. ClientModifierType keys: -7 fleet grade, -8 offense,
    // -9 defense, -10 health, -11 deflector, -12 sensor, -13 officer, -18 forbidden tech.
    const auto attribute = [&](const char* name, int32_t key) {
      if (const auto it = attributes.find(key); it != attributes.end())
        result[name] = Rounded(it->second);
    };
    const auto stat = [&](const char* name, int32_t key) {
      if (const auto it = stats.find(key); it != stats.end())
        result[name] = Float(it->second);
    };
    // Keyed by the ships in ship_stats; a ship without the value has no entry.
    const auto per_ship = [&](const google::protobuf::Map<int64_t, pb::ShipAttributes>& source, int32_t key,
                              auto&& value) {
      auto ships = json::object();
      for (const auto& entry : fleet.shipstats()) {
        const auto ship  = entry.first;
        const auto found = source.find(ship);
        if (found == source.end())
          continue;
        const auto& values = found->second.shipattributes_();
        if (const auto it = values.find(key); it != values.end())
          ships[std::to_string(ship)] = value(it->second);
      }
      return ships;
    };
    const auto floats = [](const auto& map) { return Object(map, kFloat); };

    result["active_buffs"] = Array(fleet.activebuffs(), [](const pb::ActiveBuff& buff) {
      return json{{"activation_time", TimeOrNull(buff.has_activationtime(), buff.activationtime())},
                  {"activator_id", buff.activatorid()},
                  {"buff_id", buff.buffid()},
                  {"expiry_time", TimeOrNull(buff.has_expirytime(), buff.expirytime())},
                  {"ranks", Array(buff.ranks(), kInteger)}};
    });
    result["attributes"]   = floats(attributes);
    if (fleet.battleopponentfleetid() != 0)
      result["battle_opponent_fleet_id"] = fleet.battleopponentfleetid();
    else if (side.opponent_fleet_id)
      result["battle_opponent_fleet_id"] = *side.opponent_fleet_id;
    if (fleet.has_battlestarttime())
      result["battle_start_time"] = Time(fleet.battlestarttime());
    result["battle_won"]           = fleet.battlewon() || side.won;
    result["cause_of_destruction"] = static_cast<int32_t>(fleet.causeofdestruction());
    if (fleet.courseid() != 0)
      result["course_id"] = fleet.courseid();
    attribute("defense_rating", -9);
    attribute("deflector_rating", -11);
    attribute("fleet_grade", -7);
    result["fleet_id"] = fleet.fleetid();
    attribute("forbidden_tech_rating", -18);
    attribute("health_rating", -10);
    result["hull_ids"] = Array(fleet.hullids(), kInteger);
    stat("impulse_speed", 62);
    if (fleet.isactive())
      result["is_active"] = true;
    if (fleet.has_lastupdatetime())
      result["last_update_time"] = Time(fleet.lastupdatetime());
    if (fleet.latestcoursevectorx() != 0.0f)
      result["latest_course_vector_x"] = Float(fleet.latestcoursevectorx());
    if (fleet.latestcoursevectory() != 0.0f)
      result["latest_course_vector_y"] = Float(fleet.latestcoursevectory());
    if (fleet.has_nodeaddress()) {
      const auto& address    = fleet.nodeaddress();
      result["node_address"] = {{"galaxy", address.galaxy()},
                                {"instance", address.instance()},
                                {"planet", address.planet() != 0 ? json(address.planet()) : json(nullptr)},
                                {"system", address.system()}};
    }
    if (fleet.nodeid() != 0)
      result["node_id"] = fleet.nodeid();
    attribute("offense_rating", -8);
    attribute("officer_rating", -13);
    result["pursued_by_num"]    = fleet.pursuedbynum();
    result["pursuit_target_id"] = fleet.pursuittargetid() != 0 ? json(fleet.pursuittargetid()) : json(nullptr);
    attribute("sensor_rating", -12);
    result["ship_attributes"] =
        Object(fleet.shipattributes(), [&](const pb::ShipAttributes& ship) { return floats(ship.shipattributes_()); });
    result["ship_components"] = Object(
        fleet.deployedships(), [](const pb::DeployedShip& ship) { return Array(ship.componentids(), kInteger); });
    result["ship_cosmetics"] = Object(fleet.shipcosmetics(), kInteger);
    // Stat keys: 61 hull, 60 shield, 75 shield regeneration; attribute keys: -5 hull damage, -6 shield damage.
    result["ship_dmg"]                                 = per_ship(fleet.shipattributes(), -5, kFloat);
    result["ship_hps"]                                 = per_ship(fleet.shipstats(), 61, Rounded);
    result["ship_ids"]                                 = Array(fleet.shipids(), kInteger);
    result["ship_level_percentages"]                   = floats(fleet.shiplevelpercentages());
    result["ship_levels"]                              = Object(fleet.shiplevels(), kInteger);
    result["ship_shield_dmg"]                          = per_ship(fleet.shipattributes(), -6, kFloat);
    result["ship_shield_hps"]                          = per_ship(fleet.shipstats(), 60, Rounded);
    result["ship_shield_total_regeneration_durations"] = per_ship(fleet.shipstats(), 75, Rounded);
    result["ship_stats"] =
        Object(fleet.shipstats(), [&](const pb::ShipAttributes& ship) { return floats(ship.shipattributes_()); });
    result["ship_tiers"] = Object(fleet.shiptiers(), kInteger);
    if (fleet.state() != 0)
      result["state"] = static_cast<int32_t>(fleet.state());
    result["stats"]           = floats(stats);
    result["status_counters"] = Array(fleet.statuscounters(), [](const pb::StatusCounter& counter) {
      return json{{"counter", counter.counter()}, {"status_id", counter.statusid()}};
    });
    result["status_effects"]  = Object(fleet.statuseffects(), kInteger);
    result["type"]            = static_cast<int32_t>(fleet.fleettype());
    if (!fleet.uid().empty())
      result["uid"] = fleet.uid();
    // Stat keys: 64 warp range, 63 warp speed.
    stat("warp_distance", 64);
    stat("warp_speed", 63);
    result["warp_data"] = nullptr;
    result["warp_time"] = TimeOrNull(fleet.has_warptime(), fleet.warptime());
    // Not in the JSON journals; sent only when the wire has it.
    if (fleet.armadaattackid() != 0)
      result["armada_attack_id"] = fleet.armadaattackid();
    return result;
  }

  // The side's own fleet: deployed_fleet, else the lowest-keyed deployed_fleets entry (map order is unspecified).
  const pb::DeployedFleet* MainFleet(const pb::JournalFleetData& data)
  {
    if (data.has_deployedfleet())
      return &data.deployedfleet();
    const pb::DeployedFleet* main = nullptr;
    int64_t                  key  = 0;
    for (const auto& entry : data.deployedfleets()) {
      if (!main || entry.first < key) {
        main = &entry.second;
        key  = entry.first;
      }
    }
    return main;
  }

  // battle_opponent_fleet_id for the other side: null against an armada, as the JSON journals had it.
  std::optional<json> OpponentFleetId(const pb::JournalFleetData& data)
  {
    const auto* main = MainFleet(data);
    if (!main)
      return std::nullopt;
    if (data.deployedfleets_size() > 1)
      return json(nullptr);
    return json(main->fleetid());
  }

  // A crew as the JSON journals sent it: crew_size slots, null where nobody sits.
  json Crew(const pb::JournalFleetOfficerEntry& entry)
  {
    const auto in_range = [](int64_t slot) { return slot >= 0 && slot < kMaxOfficerSlots; };
    auto       size     = std::clamp<int64_t>(entry.crewsize(), 0, kMaxOfficerSlots);
    for (const auto& officer : entry.officers())
      if (in_range(officer.slotindex()))
        size = std::max<int64_t>(size, officer.slotindex() + 1);
    auto slots = json::array();
    for (int64_t i = 0; i < size; ++i)
      slots.push_back(nullptr);
    for (const auto& officer : entry.officers())
      if (in_range(officer.slotindex()))
        slots[static_cast<size_t>(officer.slotindex())] = {
            {"id", officer.id()}, {"level", officer.level()}, {"rank", officer.rank()}};
    return slots;
  }

  json FleetData(const pb::JournalFleetData& data, const Side& side)
  {
    auto   crews      = json::object();
    size_t crew_count = 0;
    json   first_crew;
    for (const auto& entry : data.fleetsofficers()) {
      auto crew = Crew(entry);
      if (crew_count++ == 0)
        first_crew = crew;
      crews[std::to_string(entry.fleetid())] = std::move(crew);
    }
    const auto* main      = MainFleet(data);
    const auto  zero_null = [](int64_t value) { return value != 0 ? json(value) : json(nullptr); };

    json result;
    result["armada_owner_id"]  = data.armadaownerid().empty() ? json(nullptr) : json(data.armadaownerid());
    result["battle_data_type"] = data.battledatatype();
    // One crew, as an array; none (an NPC) as {}; several (an armada) as null.
    result["bridge_officers"] = crew_count == 1 ? std::move(first_crew) : crew_count == 0 ? json::object() : json();
    result["cargo_max"]       = zero_null(data.cargomax());
    result["cargo_max_list"]  = Array(data.cargomaxlist(), kInteger);
    result["defense_rating"]  = zero_null(data.defenserating());
    if (main)
      result["deployed_fleet"] = DeployedFleet(*main, side);
    result["deployed_fleets"] =
        Object(data.deployedfleets(), [&](const pb::DeployedFleet& fleet) { return DeployedFleet(fleet, side); });
    if (data.has_finalcargo())
      result["final_cargo"] = Cargo(data.finalcargo());
    result["final_cargo_list"]             = Array(data.finalcargolist(), Cargo);
    result["final_ship_hps"]               = Array(data.finalshiphps(), kInteger);
    result["final_ship_level_percentages"] = Array(data.finalshiplevelpercentages(), kFloat);
    result["final_ship_levels"]            = Array(data.finalshiplevels(), kInteger);
    result["final_ship_shps"]              = Array(data.finalshipshps(), kInteger);
    // An armada side's fleet_id was an id the wire does not carry, so it is left out.
    if (main && data.deployedfleets_size() <= 1)
      result["fleet_id"] = main->fleetid();
    result["fleets_officers"] = std::move(crews);
    result["health_rating"]   = zero_null(data.healthrating());
    result["hull_ids"]        = Array(data.hullids(), kInteger);
    if (data.has_initialcargo())
      result["initial_cargo"] = Cargo(data.initialcargo());
    result["initial_cargo_list"]             = Array(data.initialcargolist(), Cargo);
    result["initial_ship_hps"]               = Array(data.initialshiphps(), kInteger);
    result["initial_ship_level_percentages"] = Array(data.initialshiplevelpercentages(), kFloat);
    result["initial_ship_levels"]            = Array(data.initialshiplevels(), kInteger);
    result["initial_ship_shps"]              = Array(data.initialshipshps(), kInteger);
    result["max_ship_hps"]                   = Array(data.maxshiphps(), kInteger);
    result["max_ship_shps"]                  = Array(data.maxshipshps(), kInteger);
    result["num_drydocks"]                   = data.numdrydocks();
    result["offense_rating"]                 = zero_null(data.attackrating());
    result["officer_rating"]                 = data.officerrating();
    // Only NPC sides carry ref_ids; a player side has null.
    result["ref_ids"]   = data.has_refids() ? IdRefs(data.refids()) : json(nullptr);
    result["ship_ids"]  = Array(data.shipids(), kInteger);
    result["xp_gained"] = nullptr;
    return result;
  }

  // --- Journal ---

  json Journal(const pb::JournalResponse& journal)
  {
    json result;
    if (journal.has_battlecontextdata()) {
      auto context = json::object();
      if (journal.battlecontextdata().has_contextdatamapping()) {
        const auto& mapping = journal.battlecontextdata().contextdatamapping();
        if (mapping.flags() != 0)
          context["flags"] = mapping.flags();
        if (mapping.has_multitargetarmadainformation()) {
          const auto& armadas                  = mapping.multitargetarmadainformation();
          context["multi_target_armadas_info"] = {{"mta_group_size", armadas.mtasize()},
                                                  {"multi_armada_attack_id", armadas.mtaid()}};
        }
        if (mapping.has_galacticanomalydata()) {
          const auto& anomaly              = mapping.galacticanomalydata();
          context["galactic_anomaly_data"] = {{"system_buffs", Object(anomaly.systembuffs(), kInteger)},
                                              {"tournament_id", anomaly.tournamentid()}};
        }
      }
      result["battle_context_data"] = {{"context_data", std::move(context)}};
    }
    result["battle_duration"] = journal.battleduration();
    result["battle_log"]      = Array(journal.battlelog(), LogEntry);
    if (journal.has_battletime())
      result["battle_time"] = Time(journal.battletime());
    result["battle_type"] = journal.battletype();
    if (journal.has_chestdrop()) {
      const auto& drop = journal.chestdrop();
      // A chest's id_refs are its chest_id_refs entry when both lists have the same length.
      const bool paired = drop.chestidrefs_size() == drop.chestsgained_size();
      auto       gained = json::array();
      for (int i = 0; i < drop.chestsgained_size(); ++i)
        gained.push_back(Item(drop.chestsgained(i), paired ? &drop.chestidrefs(i) : nullptr));
      result["chest_drop"] = {{"chest_id_refs", Array(drop.chestidrefs(), IdRefs)},
                              {"chests_gained", std::move(gained)},
                              {"user_id_refs", Array(drop.useridrefs(), kString)}};
    }
    if (journal.has_coords())
      result["coords"] = {{"x", Float(journal.coords().x())}, {"y", Float(journal.coords().y())}};
    result["id"]                    = journal.id();
    result["initiator_fired_first"] = journal.initiatorfiredfirst();

    const Side initiator{OpponentFleetId(journal.targetfleetdata()), journal.initiatorwins()};
    const Side target{OpponentFleetId(journal.initiatorfleetdata()), !journal.initiatorwins()};
    result["initiator_fleet_data"]  = FleetData(journal.initiatorfleetdata(), initiator);
    result["initiator_id"]          = journal.initiatorid();
    result["initiator_wins"]        = journal.initiatorwins();
    result["resources_dropped"]     = Object(journal.resourcesdropped(), kInteger);
    result["resources_transferred"] = Object(journal.resourcestransferred(), kInteger);
    result["system_id"]             = journal.systemid();
    result["target_fleet_data"]     = FleetData(journal.targetfleetdata(), target);
    result["target_id"]             = journal.targetid();
    result["journal_src"]           = "pb1";
    return result;
  }

  std::optional<json> Fail(std::string* error, const char* reason)
  {
    if (error)
      *error = reason;
    return std::nullopt;
  }
} // namespace

bool IsJsonBody(std::string_view body)
{
  const auto start = body.find_first_not_of(" \t\r\n");
  return start != std::string_view::npos && body[start] == '{';
}

std::optional<json> JournalFromProtobuf(std::string_view body, std::string* error)
{
  try {
    if (body.empty())
      return Fail(error, "the body is empty");
    if (body.size() > kMaxJournalBytes)
      return Fail(error, "the body is too large");
    if (IsJsonBody(body))
      return Fail(error, "the body is JSON");
    if (body.size() >= 2 && static_cast<uint8_t>(body[0]) == 0x1f && static_cast<uint8_t>(body[1]) == 0x8b)
      return Fail(error, "the body is still gzip-compressed");

    pb::ServiceResponse response;
    if (!response.ParseFromArray(body.data(), static_cast<int>(body.size())))
      return Fail(error, "the ServiceResponse does not parse");
    const auto& groups = response.entitygroups();
    const auto  group  = std::ranges::find_if(groups, [](const auto& g) { return g.type() == kBattleReportGroup; });
    if (group == groups.end())
      return Fail(error, "there is no battle report entity group");

    pb::JournalResponse journal;
    if (group->group().size() > static_cast<size_t>(std::numeric_limits<int>::max())
        || !journal.ParseFromArray(group->group().data(), static_cast<int>(group->group().size())))
      return Fail(error, "the JournalResponse does not parse");
    if (journal.id() == 0)
      return Fail(error, "the JournalResponse has no id");

    auto result = Journal(journal);
    // Fails here rather than in the uploader if a wire string is not valid UTF-8.
    (void)result.dump();
    return result;
  } catch (const std::exception&) {
    return Fail(error, "the journal could not be converted");
  }
}
} // namespace data_sync

// Game responses become canonical wire records. Delivery owns comparison, removal and initial-sync state.
#include "sync/sync.h"
#include "text.h"
#include <Digit.PrimePlatform.Content.pb.h>
#include <Digit.PrimeServer.Models.pb.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <google/protobuf/repeated_field.h>
#include <ranges>
#include <set>
#include <stfc.sync.pb.h>
#include <unordered_map>

NLOHMANN_JSON_NAMESPACE_BEGIN
template <typename T> struct adl_serializer<google::protobuf::RepeatedField<T>> {
  static void to_json(json& j, const google::protobuf::RepeatedField<T>& values)
  {
    j = json::array();
    for (const auto& value : values)
      j.push_back(value);
  }
};
NLOHMANN_JSON_NAMESPACE_END

namespace data_sync
{
namespace
{
  using json        = nlohmann::json;
  namespace pb      = Digit::PrimeServer::Models;
  namespace content = Digit::PrimePlatform::Content;

  // Build the complete response before publishing: a malformed payload never partially changes current state.
  template <typename Message, typename Build>
  void Read(const Input& input, Stream stream, Build build, Update mode = Update::kMerge)
  {
    Message message;
    if (!message.ParseFromString(input.bytes)) {
      spdlog::error("Failed to parse {}", message.GetTypeName());
      return;
    }
    RecordSet records;
    build(message, records);
    Publish(stream, std::move(records), input.generation, mode);
  }
  json Seconds(bool has, int64_t seconds)
  { return has ? json(seconds) : json(nullptr); }

  json AwayAssignment(const pb::AwayAssignmentInstance& instance)
  {
    std::vector<std::pair<int64_t, int64_t>> slots(instance.officerids().begin(), instance.officerids().end());
    std::ranges::sort(slots, {}, &std::pair<int64_t, int64_t>::first);
    auto officers = json::array();
    for (const auto& [slot, id] : slots)
      if (id)
        officers.push_back(id);
    auto traits = json::array();
    for (const auto& trait : instance.officertraits())
      traits.push_back({{"tid", trait.traitid()}, {"max_level", trait.maxlevel()}});
    std::vector<std::pair<int64_t, int32_t>> scores(instance.parameters().criticaltraitscores().begin(),
                                                    instance.parameters().criticaltraitscores().end());
    std::ranges::sort(scores, {}, &std::pair<int64_t, int32_t>::first);
    auto critical = json::array();
    for (const auto& [id, score] : scores)
      critical.push_back({{"tid", id}, {"score", score}});
    return {{"aid", instance.id()},
            {"template_id", instance.awayassignmenttemplateid()},
            {"state", instance.state()},
            {"officer_ids", officers},
            {"job_uuid", instance.jobuuid()},
            {"duration", instance.parameters().duration()},
            {"rarity", instance.rarity()},
            {"officer_traits", traits},
            {"critical_trait_scores", critical},
            {"success_chance", instance.parameters().successchance()},
            {"critical_success_chance", instance.parameters().criticalsuccesschance()},
            {"max_critical_success_chance", instance.maxcriticalsuccesschance()},
            {"key_stat", instance.keystat()},
            {"max_assignable_officers", instance.maxassignableofficerscount()},
            {"attack_weight", instance.attackweight()},
            {"defense_weight", instance.defenseweight()},
            {"health_weight", instance.healthweight()}};
  }
  void ProcessAwayAssignments(const Input& input)
  {
    Read<pb::AwayAssignmentUserListResponse>(
        input, Stream::kAway,
        [](const auto& response, auto& records) {
          for (const auto& instance : response.instances())
            records[instance.id()] = AwayAssignment(instance);
        },
        Update::kReplace);
  }
  void ProcessAwayAssignment(const Input& input)
  {
    Read<pb::AwayAssignmentInstance>(input, Stream::kAway, [](const auto& instance, auto& records) {
      records[instance.id()] = AwayAssignment(instance);
    });
  }
  void ProcessBattleHeaders(const Input& input)
  {
    pb::BattleResultHeadersResponse response;
    if (!response.ParseFromString(input.bytes)) {
      spdlog::error("Failed to parse battle headers");
      return;
    }
    std::vector<uint64_t> ids;
    for (const auto& header : response.headers())
      ids.push_back(static_cast<uint64_t>(header.id()));
    QueueNewBattles(ids, input.generation);
  }
  void ProcessBuffs(const Input& input)
  {
    Read<pb::GlobalActiveBuffsResponse>(
        input, Stream::kBuffs,
        [](const auto& response, auto& records) {
          for (const auto& buff : response.globalactivebuffs()) {
            const bool expires     = buff.has_activebuff() && buff.activebuff().has_expirytime();
            records[buff.buffid()] = {
                {"bid", buff.buffid()},
                {"level", buff.level()},
                {"expiry_time", Seconds(expires, expires ? buff.activebuff().expirytime().seconds() : 0)}};
          }
        },
        Update::kReplace);
  }
  void ProcessAllianceGameProperties(const Input& input)
  {
    Read<pb::AllianceGamePropertiesResponse>(input, Stream::kEmerald, [](const auto& response, auto& records) {
      for (const auto& property : response.properties())
        if (property.propertyname() == "claimed_loyalty_tiers") {
          int32_t level = -1;
          for (const auto& tier : property.valuelist()) {
            int32_t value = -1;
            std::from_chars(tier.data(), tier.data() + tier.size(), value);
            level = std::max(level, value);
          }
          records[nullptr] = {{"level", level}};
          break;
        }
    });
  }
  void ProcessStarbaseModules(const Input& input)
  {
    Read<pb::StarbaseModulesResponse>(input, Stream::kBuildings, [](const auto& response, auto& records) {
      for (const auto& module : response.modulestates())
        records[module.id()] = {{"bid", module.id()}, {"level", module.level()}};
    });
  }
  // An empty list is not a complete Haven; upstream never sends one, and the first-sync flag waits for a real list.
  void PublishHaven(Stream stream, RecordSet records, uint64_t generation)
  {
    if (!records.empty())
      Publish(stream, std::move(records), generation, Update::kFull);
  }
  void ProcessHavenBase(const Input& input)
  {
    pb::PlanetaryBase response;
    if (!response.ParseFromString(input.bytes)) {
      spdlog::error("Failed to parse {}", response.GetTypeName());
      return;
    }
    RecordSet records;
    for (const auto& [piece, count] : response.pieces())
      records[piece] = {{"sid", piece}, {"count", count}};
    PublishHaven(Stream::kHavenPieces, std::move(records), input.generation);
  }
  void HavenBuildings(const pb::PlanetaryMapData& response, RecordSet& records)
  {
    for (const auto& [id, building] : response.buildings())
      records[id] = {{"sid", building.specid()},
                     {"id", id},
                     {"level", building.level()},
                     {"status", building.status()},
                     {"position", building.position()}};
  }
  void ProcessHavenMap(const Input& input)
  {
    pb::PlanetaryMapData response;
    if (!response.ParseFromString(input.bytes)) {
      spdlog::error("Failed to parse {}", response.GetTypeName());
      return;
    }
    RecordSet records;
    HavenBuildings(response, records);
    PublishHaven(Stream::kHaven, std::move(records), input.generation);
  }
  void ProcessHavenResponse(const Input& input)
  {
    pb::PlanetaryMapResponse response;
    if (!response.ParseFromString(input.bytes)) {
      spdlog::error("Failed to parse planetary map response");
      return;
    }
    if (!response.owneruserid().empty() && response.owneruserid() != input.user_id)
      return;
    RecordSet records;
    HavenBuildings(response.mapdata(), records);
    PublishHaven(Stream::kHaven, std::move(records), input.generation);
  }
  void ProcessHavenProsperity(const Input& input)
  {
    Read<pb::PlanetaryMapProsperityResponse>(input, Stream::kHavenProsperity, [](const auto& response, auto& records) {
      records[nullptr] = {{"prosperity", response.prosperity()}};
    });
  }
  void ProcessInventory(const Input& input)
  {
    Read<pb::InventoryResponse>(
        input, Stream::kInventory,
        [](const auto& response, auto& records) {
          for (const auto& inventory : response.inventories() | std::views::values)
            for (const auto& item : inventory.items())
              if (item.has_commonparams()) {
                const auto id                           = item.commonparams().refid();
                records[json::array({item.type(), id})] = {
                    {"item_type", item.type()}, {"refid", id}, {"count", item.count()}};
              }
        },
        Update::kFull);
  }
  std::optional<json> Job(const pb::Job& job)
  {
    json record{{"job_type", job.type()},
                {"uuid", job.uuid()},
                {"start_time", job.starttime().seconds()},
                {"duration", job.duration()},
                {"reduction", job.reductioninseconds()}};
    switch (job.type()) {
      case pb::JOBTYPE_RESEARCH:
        record.update({{"rid", job.researchparams().projectid()}, {"level", job.researchparams().level()}});
        break;
      case pb::JOBTYPE_STARBASECONSTRUCTION:
        record.update({{"bid", job.starbaseconstructionparams().moduleid()},
                       {"level", job.starbaseconstructionparams().level()}});
        break;
      case pb::JOBTYPE_SHIPTIERUP:
        record.update({{"psid", job.tierupshipparams().shipid()}, {"tier", job.tierupshipparams().newtier()}});
        break;
      case pb::JOBTYPE_SHIPSCRAP:
        record.update({{"psid", job.scrapyardparams().shipid()},
                       {"hull_id", job.scrapyardparams().hullid()},
                       {"level", job.scrapyardparams().level()}});
        break;
      case pb::JOBTYPE_AWAYASSIGNMENT:
        record["aid"] = job.awayassignmentparams().awayassignmentinstanceid();
        break;
      // Part of fleet sync: jobs-only targets see nothing new. The repair changes the fleet's state later, so the
      // fleet is re-read then.
      case pb::JOBTYPE_REPAIRFLEET:
        if (!Config::Sync::Get()->options.fleets)
          return std::nullopt;
        RereadFleet(job.repairfleetparams().fleetid());
        record["fid"] = job.repairfleetparams().fleetid();
        break;
      default:
        return std::nullopt;
    }
    return record;
  }
  void ProcessJobs(const Input& input)
  {
    Read<pb::JobResponse>(
        input, Stream::kJobs,
        [](const auto& response, auto& records) {
          for (const auto& job : response.jobs())
            if (auto record = Job(job))
              records[job.uuid()] = std::move(*record);
        },
        Update::kReplace);
  }
  void ProcessActiveMissions(const Input& input)
  {
    Read<pb::ActiveMissionsResponse>(input, Stream::kActiveMissions, [](const auto& response, auto& records) {
      std::set<int64_t> ids;
      for (const auto& mission : response.activemissions())
        ids.insert(mission.id());
      auto missions = json::array();
      for (const auto id : ids)
        missions.push_back({{"mid", id}});
      if (missions.empty())
        missions.push_back({{"mid", nullptr}});
      records[nullptr] = std::move(missions);
    });
  }
  void ProcessCompletedMissions(const Input& input)
  {
    Read<pb::CompletedMissionsResponse>(input, Stream::kCompletedMissions, [](const auto& response, auto& records) {
      for (const auto id : response.completedmissions())
        records[id] = {{"mid", id}};
    });
  }
  void ProcessOfficers(const Input& input)
  {
    Read<pb::OfficersResponse>(input, Stream::kOfficers, [](const auto& response, auto& records) {
      for (const auto& officer : response.officers())
        records[officer.id()] = {{"oid", officer.id()},
                                 {"rank", officer.rankindex()},
                                 {"level", officer.level()},
                                 {"shard_count", officer.shardcount()}};
    });
  }
  void ProcessResearch(const Input& input)
  {
    Read<pb::ResearchTreesState>(input, Stream::kResearch, [](const auto& response, auto& records) {
      for (const auto& [id, level] : response.researchprojectlevels())
        records[id] = {{"rid", id}, {"level", level}};
    });
  }
  void ProcessResources(const Input& input)
  {
    Read<pb::ResourcesResponse>(
        input, Stream::kResources,
        [](const auto& response, auto& records) {
          for (const auto& resource : response.resources())
            records[resource.id()] = {{"rid", resource.id()}, {"amount", resource.currentamount()}};
        },
        Update::kFull);
  }
  void ProcessResourcesDelta(const Input& input)
  {
    Read<pb::ResourcesDeltaResponse>(input, Stream::kResources, [](const auto& response, auto& records) {
      for (const auto& resource : response.resources())
        records[resource.id()] = {{"rid", resource.id()}, {"amount", resource.amount()}};
    });
  }
  void ProcessAllianceResources(const Input& input)
  {
    Read<pb::AllianceGetBankResourcesResponse>(
        input, Stream::kAllianceResources,
        [](const auto& response, auto& records) {
          for (const auto& item : response.items())
            if (item.has_commonparams()) {
              const auto id = item.commonparams().refid();
              records[id]   = {{"rid", id}, {"amount", item.count()}};
            }
        },
        Update::kFull);
  }
  void ProcessAllianceResourcesDelta(const Input& input)
  {
    Read<pb::AllianceResourcesDeltaResponse>(
        input, Stream::kAllianceResources, [](const auto& response, auto& records) {
          for (const auto& resource : response.resources())
            records[resource.id()] = {{"rid", resource.id()}, {"amount", resource.amount()}};
        });
  }

  // Both transports use these same parameter shapes. Realtime timestamps and fleet setup abbreviations are
  // converted at the JSON boundary below; the wire format never depends on which transport supplied a slot.
  json Slot(int64_t id, int32_t type, int64_t spec, json item, json params)
  {
    return {{"sid", id},
            {"slot_type", type},
            {"spec_id", spec},
            {"item_id", std::move(item)},
            {"params", std::move(params)}};
  }
  std::optional<json> Slot(const pb::EntitySlot& slot)
  {
    json params;
    switch (slot.slottype()) {
      case pb::SLOTTYPE_CONSUMABLE:
        if (slot.has_consumableslotparams()) {
          const auto& p         = slot.consumableslotparams();
          params["expiry_time"] = Seconds(p.has_expirytime(), p.expirytime().seconds());
        }
        break;
      case pb::SLOTTYPE_OFFICERPRESET:
        if (slot.has_officerpresetslotparams()) {
          const auto& p = slot.officerpresetslotparams();
          params        = {{"name", p.name()}, {"order", p.order()}, {"officer_ids", p.officerids()}};
        }
        break;
      case pb::SLOTTYPE_FLEETCOMMANDER:
        if (slot.has_fleetcommanderslotparams())
          params["order"] = slot.fleetcommanderslotparams().order();
        break;
      case pb::SLOTTYPE_SELECTABLESKILL:
        if (slot.has_selectableskillslotparams()) {
          const auto& p                 = slot.selectableskillslotparams();
          params["cooldown_expiration"] = Seconds(p.has_cooldownexpiration(), p.cooldownexpiration().seconds());
        }
        break;
      case pb::SLOTTYPE_FORBIDDENTECH:
        if (slot.has_forbiddentechslotparams()) {
          const auto& p = slot.forbiddentechslotparams();
          params        = {{"owner_id", p.ownerid()}, {"tier", p.tier()}, {"level", p.level()}};
        }
        break;
      case pb::SLOTTYPE_FLEETPRESET:
        if (slot.has_fleetpresetslotparams()) {
          const auto& p      = slot.fleetpresetslotparams();
          auto        setups = json::array();
          for (const auto& setup : p.setups())
            setups.push_back({{"drydock_id", setup.drydockid()},
                              {"ship_id", setup.shipids_size() ? json(setup.shipids(0)) : json(nullptr)},
                              {"officer_ids", setup.officerids()}});
          params = {{"name", p.name()}, {"order", p.order()}, {"setup", setups}};
        }
        break;
      default:
        return std::nullopt;
    }
    return Slot(slot.id(), slot.slottype(), slot.slotspecid(),
                slot.has_slotitemid() ? json(slot.slotitemid().value()) : json(nullptr), std::move(params));
  }
  void ProcessEntitySlots(const Input& input)
  {
    Read<pb::EntitySlots>(input, Stream::kSlots, [](const auto& response, auto& records) {
      for (const auto& slot : response.entityslots_())
        if (auto record = Slot(slot))
          records[slot.id()] = std::move(*record);
    });
  }
  void ProcessEntitySlotsData(const Input& input)
  {
    Read<pb::EntitySlotsData>(input, Stream::kSlots, [](const auto& response, auto& records) {
      for (const auto& entity : response.entityslots())
        for (const auto& slot : entity.slots())
          if (auto record = Slot(slot))
            records[slot.id()] = std::move(*record);
    });
  }
  void ProcessForbiddenTech(const Input& input)
  {
    Read<pb::ForbiddenTechsResponse>(input, Stream::kTech, [](const auto& response, auto& records) {
      for (const auto& tech : response.forbiddentechs())
        records[tech.id()] = {
            {"fid", tech.id()}, {"tier", tech.tier()}, {"level", tech.level()}, {"shard_count", tech.shardcount()}};
    });
  }
  void ProcessOfficerTraits(const Input& input)
  {
    Read<pb::OfficerTraitsResponse>(input, Stream::kTraits, [](const auto& response, auto& records) {
      for (const auto& [officer, traits] : response.activeofficertraits())
        for (const auto& trait : traits.activetraits() | std::views::values)
          records[json::array({officer, trait.traitid()})] = {
              {"oid", officer}, {"tid", trait.traitid()}, {"level", trait.level()}};
    });
  }
  json Ship(int64_t id, int64_t hull, int32_t tier, int32_t level, double progress, json components)
  {
    return {{"psid", id},
            {"hull_id", hull},
            {"tier", tier},
            {"level", level},
            {"level_percentage", progress},
            {"components", std::move(components)}};
  }
  void ProcessShips(const Input& input)
  {
    Read<pb::PlayerShipsResponse>(
        input, Stream::kShips,
        [](const auto& response, auto& records) {
          for (const auto& ship : response.ships() | std::views::values)
            records[ship.id()] =
                Ship(ship.id(), ship.hullid(), ship.tier(), ship.level(), ship.levelpercentage(), ship.components());
        },
        Update::kFull);
  }

  void ProcessJson(const Input& input)
  {
    const auto  config  = Config::Sync::Get();
    const auto& options = config->options;
    const auto  result  = json::parse(input.bytes, nullptr, false);
    if (!result.is_object()) {
      spdlog::error("Failed to parse sync JSON");
      return;
    }
    for (const auto& [name, section] : result.items()) {
      if (name == "battle_result_headers" && options.battlelogs) {
        std::vector<uint64_t> ids;
        for (const auto& header : section)
          if (header.is_object()) {
            const auto id = header.find("id");
            if (id != header.end() && id->is_number_unsigned())
              ids.push_back(id->get<uint64_t>());
          }
        QueueNewBattles(ids, input.generation);
        continue;
      }
      Stream stream;
      if (name == "resources")
        stream = Stream::kResources;
      else if (name == "starbase_modules")
        stream = Stream::kBuildings;
      else if (name == "ships")
        stream = Stream::kShips;
      else
        continue;
      RecordSet records;
      for (const auto& [key, record] : section.items()) {
        try {
          if (stream == Stream::kResources) {
            const auto id = std::stoll(key);
            records[id]   = {{"rid", id}, {"amount", record.at("current_amount").get<int64_t>()}};
          } else if (stream == Stream::kBuildings) {
            const auto id = record.at("id").get<int64_t>();
            records[id]   = {{"bid", id}, {"level", record.at("level").get<int32_t>()}};
          } else {
            const auto id = record.at("id").get<int64_t>();
            records[id]   = Ship(id, record.at("hull_id").get<int64_t>(), record.at("tier").get<int32_t>(),
                                 record.at("level").get<int32_t>(), record.at("level_percentage").get<double>(),
                                 record.at("components").get<std::vector<int64_t>>());
          }
        } catch (const std::exception& e) {
          spdlog::error("Skipping malformed {} record {}: {}", name, key, e.what());
        }
      }
      Publish(stream, std::move(records), input.generation, Update::kFull);
    }
  }
  const char* DeployedStateName(pb::DeployedFleetState state)
  {
    switch (state) {
      case pb::DEPLOYEDFLEETSTATE_IDLE:
        return "Idle";
      case pb::DEPLOYEDFLEETSTATE_MOVING:
        return "Moving";
      case pb::DEPLOYEDFLEETSTATE_WARPING:
        return "Warping";
      case pb::DEPLOYEDFLEETSTATE_BATTLING:
        return "Battling";
      case pb::DEPLOYEDFLEETSTATE_RECALL:
        return "Recall";
      // Docked at a mining node, not the station.
      case pb::DEPLOYEDFLEETSTATE_DOCKED:
        return "OnNode";
      case pb::DEPLOYEDFLEETSTATE_PREBATTLE:
        return "PreBattle";
      default:
        return "Unknown";
    }
  }

  // 1234 not 1234.0, and 14.364 not 14.36400032043457.
  json ShortNumber(float value)
  {
    if (std::trunc(value) == value && std::abs(value) < 9.0e15f)
      return static_cast<int64_t>(value);
    char   buffer[32];
    double widened = value;
    if (const auto [end, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value); ec == std::errc{}) {
      // Float from_chars needs macOS 26 in Apple's libc++; the JSON parser reads the shortest form back instead.
      if (auto parsed = json::parse(std::begin(buffer), end, nullptr, false); parsed.is_number())
        return parsed;
    }
    return widened;
  }

  using StatMap = google::protobuf::Map<int32_t, float>;

  std::optional<float> Stat(const StatMap& stats, pb::ClientModifierType key)
  {
    if (const auto it = stats.find(key); it != stats.end())
      return it->second;
    return std::nullopt;
  }

  // Missing stats are left out instead of sent as 0.
  void PutStat(json& record, const char* name, const StatMap& stats, pb::ClientModifierType key)
  {
    if (const auto value = Stat(stats, key); value && std::isfinite(*value))
      record[name] = ShortNumber(*value);
  }

  // Summed over the fleet's ships: shipStats has the maximum, shipAttributes the damage taken.
  void PutPoints(json& record, const char* current_name, const char* max_name, const pb::DeployedFleet& fleet,
                 pb::ClientModifierType max_key, pb::ClientModifierType damage_key)
  {
    double total_max    = 0;
    double total_damage = 0;
    bool   found        = false;
    for (const auto& [ship_id, stats] : fleet.shipstats()) {
      const auto max = Stat(stats.shipattributes_(), max_key);
      if (!max)
        continue;
      found = true;
      total_max += *max;
      if (const auto it = fleet.shipattributes().find(ship_id); it != fleet.shipattributes().end())
        total_damage += Stat(it->second.shipattributes_(), damage_key).value_or(0.0f);
    }
    if (found) {
      record[current_name] = std::llround(std::max(0.0, total_max - total_damage));
      record[max_name]     = std::llround(total_max);
    }
  }

  // The server's view of the player's deployed fleets. It resends every fleet on each update; delivery sends only
  // fleets that changed, and a fleet that left (docked, destroyed) is dropped so it is sent again when it is back.
  void ProcessDeployedFleets(const Input& input)
  {
    // Each changed or departed fleet is re-read on the game thread, which fills in some values from this data. The
    // last record per fleet, for this session only, tells which ones those are. Never destroyed, see sync.h.
    static uint64_t seen_generation = 0;
    static auto&    seen            = *new std::unordered_map<int64_t, size_t>;
    if (seen_generation != input.generation) {
      seen_generation = input.generation;
      seen.clear();
    }
    const int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    Read<pb::MyDeployedFleetsResponse>(
        input, Stream::kDeployedFleets,
        [&](const auto& response, auto& records) {
          for (const auto& [fleet_id, fleet] : response.fleets()) {
            json record{{"fid", fleet_id},
                        {"src", "server"},
                        {"server_state", static_cast<int>(fleet.state())},
                        {"server_state_name", DeployedStateName(fleet.state())},
                        {"node_id", fleet.nodeid()}};
            // Widened through uint32 like the client records.
            if (fleet.hullids_size() > 0)
              record["hull_id"] = static_cast<int64_t>(static_cast<uint32_t>(fleet.hullids(0)));
            if (fleet.has_nodeaddress())
              record["system_id"] = fleet.nodeaddress().system();
            PutPoints(record, "hull_hp", "hull_max", fleet, pb::CLIENTMODIFIERTYPE_MODHULLHPMAX,
                      pb::CLIENTMODIFIERTYPE_SHIPHULLDAMAGE);
            PutPoints(record, "shield_hp", "shield_max", fleet, pb::CLIENTMODIFIERTYPE_MODSHIELDHPMAX,
                      pb::CLIENTMODIFIERTYPE_SHIPSHIELDDAMAGE);
            PutStat(record, "cargo_cap", fleet.stats(), pb::CLIENTMODIFIERTYPE_MODCARGOCAPACITY);
            PutStat(record, "warp_range", fleet.stats(), pb::CLIENTMODIFIERTYPE_MODWARPDISTANCE);
            PutStat(record, "strength", fleet.attributes(), pb::CLIENTMODIFIERTYPE_FLEETSTRENGTH);
            // A count only, no other players' ids.
            record["pursued_by"] = fleet.pursuedbynum();

            if (auto& last = seen[fleet_id]; last != std::hash<json>{}(record)) {
              last = std::hash<json>{}(record);
              RereadFleet(fleet_id);
            }
            record["t"]       = now;
            records[fleet_id] = std::move(record);
          }
          std::erase_if(seen, [&](const auto& entry) {
            if (response.fleets().contains(entry.first))
              return false;
            RereadFleet(entry.first);
            return true;
          });
        },
        Update::kReplace);
  }

  struct Route {
    EntityGroup::Kind type;
    Processor         process;
    bool              battle = false;
  };
  const Route kRoutes[] = {
      {EntityGroup::Kind::AwayAssignmentsList, ProcessAwayAssignments},
      {EntityGroup::Kind::AwayAssignmentsInstance, ProcessAwayAssignment},
      {EntityGroup::Kind::BattleResultHeaders, ProcessBattleHeaders, true},
      {EntityGroup::Kind::UserProfiles, CachePlayerNames, true},
      {EntityGroup::Kind::AllianceProfiles, CacheAllianceNames, true},
      {EntityGroup::Kind::GlobalActiveBuffs, ProcessBuffs},
      {EntityGroup::Kind::AllianceGetGameProperties, ProcessAllianceGameProperties},
      {EntityGroup::Kind::StarbaseModules, ProcessStarbaseModules},
      {EntityGroup::Kind::MyDeployedFleets, ProcessDeployedFleets},
      {EntityGroup::Kind::PlanetaryBaseData, ProcessHavenBase},
      {EntityGroup::Kind::PlanetaryMapData, ProcessHavenMap},
      {EntityGroup::Kind::PlanetaryMapResponse, ProcessHavenResponse},
      {EntityGroup::Kind::PlanetaryMapProsperity, ProcessHavenProsperity},
      {EntityGroup::Kind::PlayerInventories, ProcessInventory},
      {EntityGroup::Kind::Jobs, ProcessJobs},
      {EntityGroup::Kind::ActiveMissions, ProcessActiveMissions},
      {EntityGroup::Kind::CompletedMissions, ProcessCompletedMissions},
      {EntityGroup::Kind::Officers, ProcessOfficers},
      {EntityGroup::Kind::ResearchTreesState, ProcessResearch},
      {EntityGroup::Kind::Resources, ProcessResources},
      {EntityGroup::Kind::ResourcesDelta, ProcessResourcesDelta},
      {EntityGroup::Kind::AllianceGetBankResources, ProcessAllianceResources},
      {EntityGroup::Kind::ResourcesDeltaAlliance, ProcessAllianceResourcesDelta},
      {EntityGroup::Kind::Ships, ProcessShips},
      {EntityGroup::Kind::EntitySlots, ProcessEntitySlots},
      {EntityGroup::Kind::EntitySlotsData, ProcessEntitySlotsData},
      {EntityGroup::Kind::ForbiddenTechs, ProcessForbiddenTech},
      {EntityGroup::Kind::ActiveOfficerTraits, ProcessOfficerTraits},
      {EntityGroup::Kind::Json, ProcessJson},
  };

  // Store offer prices are in Resource.amount; amountUlong is the fallback.
  int64_t OfferAmount(const content::Resource& resource)
  { return static_cast<int64_t>(resource.amount() != 0 ? resource.amount() : resource.amountulong()); }

  // Platform ids are strings; numeric ones go out as numbers, like the rest of the sync data.
  json OfferId(const std::string& id)
  {
    int64_t     value    = 0;
    const auto* end      = id.data() + id.size();
    const auto [ptr, ec] = std::from_chars(id.data(), end, value);
    return !id.empty() && ec == std::errc{} && ptr == end ? json(value) : json(id);
  }

  // The consumable a bundle sells. Faction store offers are favor levels, sold as consumables.
  const content::ContentItem* OfferConsumable(const content::BundleContent& bundle)
  {
    for (const auto* list : {&bundle.static_(), &bundle.showcase(), &bundle.gifts()})
      for (const auto& item : list->items())
        if (item.type() == "consumable")
          return &item;
    return nullptr;
  }
} // namespace

void ProcessRealtimeSlot(const Input& input)
{
  try {
    auto                                      data     = json::parse(input.bytes);
    const int32_t                             type     = data.at("slot_type").get<int32_t>();
    constexpr std::pair<int32_t, const char*> fields[] = {
        {pb::SLOTTYPE_CONSUMABLE, "consumable_slot_params"},
        {pb::SLOTTYPE_OFFICERPRESET, "officer_preset_slot_params"},
        {pb::SLOTTYPE_FLEETCOMMANDER, "fleet_commander_slot_params"},
        {pb::SLOTTYPE_SELECTABLESKILL, "selectable_skill_slot_params"},
        {pb::SLOTTYPE_FORBIDDENTECH, "forbidden_tech_slot_params"},
        {pb::SLOTTYPE_FLEETPRESET, "fleet_preset_slot_params"}};
    const auto field = std::ranges::find(fields, type, &std::pair<int32_t, const char*>::first);
    if (field == std::end(fields))
      return;
    json params = data.value(field->second, json());
    if (!params.is_null()) {
      if (type == pb::SLOTTYPE_CONSUMABLE || type == pb::SLOTTYPE_SELECTABLESKILL) {
        const char* key       = type == pb::SLOTTYPE_CONSUMABLE ? "expiry_time" : "cooldown_expiration";
        const auto  timestamp = params.value(key, json());
        const auto  seconds =
            timestamp.is_string() ? ParseUtcTimestamp(timestamp.get_ref<const std::string&>()) : std::nullopt;
        params = {{key, seconds ? json(*seconds) : json(nullptr)}};
      } else if (type == pb::SLOTTYPE_FLEETPRESET) {
        auto setups = json::array();
        for (const auto& setup : params.at("setups")) {
          const auto ships = setup.at("s").get<std::vector<int64_t>>();
          setups.push_back({{"drydock_id", setup.at("d").get<int64_t>()},
                            {"ship_id", ships.empty() ? json(nullptr) : json(ships.at(0))},
                            {"officer_ids", setup.at("o").get<std::vector<int64_t>>()}});
        }
        params = {{"name", params.at("name").get<std::string>()},
                  {"order", params.at("order").get<int32_t>()},
                  {"setup", setups}};
      } else if (type == pb::SLOTTYPE_FLEETCOMMANDER)
        params = {{"order", params.at("order").get<int32_t>()}};
      else if (type == pb::SLOTTYPE_OFFICERPRESET)
        params = {{"name", params.at("name").get<std::string>()},
                  {"order", params.at("order").get<int32_t>()},
                  {"officer_ids", params.at("officer_ids").get<std::vector<int64_t>>()}};
      else
        params = {{"owner_id", params.at("owner_id").get<int64_t>()},
                  {"tier", params.at("tier").get<int32_t>()},
                  {"level", params.at("level").get<int32_t>()}};
    }
    const auto id     = data.at("slot_id").get<int64_t>();
    const auto item   = data.value("slot_item_id", json());
    auto       record = Slot(id, type, data.at("slot_spec_id").get<int64_t>(),
                             item.is_null() ? json(nullptr) : json(item.get<int64_t>()), std::move(params));
    Publish(Stream::kSlots, {{id, std::move(record)}}, input.generation);
  } catch (const json::exception& e) {
    spdlog::error("Failed to parse slots JSON: {}", e.what());
  }
}
// A store offers group carries whole store categories. The faction store's consumable offers are one record, which
// targets receive again whenever it changes.
void ProcessStoreOffers(const Input& input)
{
  content::Bundles response;
  if (!response.ParseFromString(input.bytes)) {
    spdlog::error("Failed to parse {}", response.GetTypeName());
    return;
  }
  constexpr std::string_view kCategory = "factions";
  struct Offer {
    const content::Bundle*      bundle;
    const content::ContentItem* consumable;
  };
  std::vector<Offer> found;
  bool               has_category = false;
  for (const auto& bundle : response.bundles_()) {
    if (bundle.category() != kCategory)
      continue;
    has_category = true;
    if (const auto* consumable = OfferConsumable(bundle.content()))
      found.push_back({&bundle, consumable});
  }
  // A group without the faction store says nothing about it.
  if (!has_category)
    return;
  // In a fixed order, so the record does not change with the server's.
  std::ranges::sort(found, {}, [](const Offer& offer) {
    return std::pair{offer.bundle->bundleid(), std::string_view{offer.consumable->id()}};
  });

  auto offers = json::array();
  for (const auto& [bundle, consumable] : found) {
    auto cost = json::array();
    for (const auto& entry : bundle->cost())
      for (const auto& resource : entry.cost_())
        cost.push_back({{"rid", OfferId(resource.id())}, {"amount", OfferAmount(resource)}});
    // For faction_standing requirements the amount is a zero-based tier index.
    auto requirements = json::array();
    for (const auto& resource : bundle->requirements())
      requirements.push_back(
          {{"type", resource.type()}, {"id", OfferId(resource.id())}, {"amount", OfferAmount(resource)}});
    const auto& details = bundle->offerdetails();
    offers.push_back({{"bid", bundle->bundleid()},
                      {"cid", OfferId(consumable->id())},
                      {"cost", std::move(cost)},
                      {"req", std::move(requirements)},
                      {"valid_count", details.validcount()},
                      {"same_remaining_count", details.sameremainingcount()}});
  }
  RecordSet records;
  records[std::string(kCategory)] = {{"category", kCategory}, {"offers", std::move(offers)}};
  Publish(Stream::kStoreOffers, std::move(records), input.generation);
}

// Only the record whose user id is the local player's is read. The server record carries level 0, so the in-memory
// Level captured with the response is used: it lags one own-record response after a level-up.
void ProcessOwnProfile(const Input& input)
{
  pb::UserProfilesResponse response;
  if (!response.ParseFromString(input.bytes)) {
    spdlog::error("Failed to parse own user profile");
    return;
  }
  for (const auto& profile : response.userprofiles()) {
    if (profile.userid() != input.user_id)
      continue;
    RecordSet records;
    records[input.user_id] = {{"uid", input.user_id},
                              {"level", input.level},
                              {"xp", profile.xp()},
                              {"military_might", profile.militarymight()},
                              {"planetary_base_unlocked", profile.planetarybaseunlocked()}};
    Publish(Stream::kProfile, std::move(records), input.generation, Update::kFull);
    return;
  }
}

Processor ProcessorFor(EntityGroup::Kind kind)
{
  const auto  config  = Config::Sync::Get();
  const auto& options = config->options;
  for (const auto& route : kRoutes)
    if (route.type == kind)
      return !route.battle || options.battlelogs ? route.process : nullptr;
  return nullptr;
}
} // namespace data_sync

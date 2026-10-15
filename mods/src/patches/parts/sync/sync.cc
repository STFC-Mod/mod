// Data sync: watches the game's server responses and realtime updates, diffs each category against what was
// last sent, and queues the changes for upload to the configured sync targets.

#include "patches/parts/sync/sync.h"

#include "patches/patches.h"
#include "str_utils.h"

#include <Digit.PrimePlatform.Content.pb.h>
#include <Digit.PrimeServer.Models.pb.h>
#include <stfc.sync.pb.h>
#include <prime/Hub.h>
#include <prime/server.h>
#include <prime/UserProfileManager.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/fmt/fmt.h>
#include <spud/detour.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <ranges>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace data_sync
{
namespace
{
  using json   = nlohmann::json;
  namespace pb      = Digit::PrimeServer::Models;
  namespace content = Digit::PrimePlatform::Content;

  // Responses waiting for the processing thread. A login delivers a few dozen at once; 256 leaves room for a
  // slow backlog without holding unbounded copies of large inventory payloads.
  constexpr size_t kJobQueueCapacity = 256;

  struct Job {
    // A plain processor, or one bound to what the game thread knew when the response arrived.
    std::function<void(const std::string& bytes)> process;
    std::string                                   bytes;
  };

  BoundedQueue<Job> pending{"processing", kJobQueueCapacity};

  struct PairHash {
    template <typename T, typename U> size_t operator()(const std::pair<T, U>& pair) const
    { return std::hash<T>()(pair.first) ^ (std::hash<U>()(pair.second) << 1); }
  };

  // The last value sent for each record of one category, so only changes are uploaded. Everything below runs on
  // the processing thread, so none of it is locked.
  template <typename Key, typename Value, typename Hash = std::hash<Key>> class SentState
  {
  public:
    // Records `value` and reports whether it differs from what was last sent.
    bool Changed(const Key& key, const Value& value)
    {
      auto [it, inserted] = sent_.try_emplace(key, value);
      if (inserted)
        return true;
      if (it->second == value)
        return false;
      it->second = value;
      return true;
    }

    // Forgets every record not in `present` and calls `gone` for each.
    template <typename Present, typename Gone> void Prune(const Present& present, Gone&& gone)
    {
      std::erase_if(sent_, [&](const auto& entry) {
        if (present.contains(entry.first))
          return false;
        gone(entry.first);
        return true;
      });
    }

  private:
    std::unordered_map<Key, Value, Hash> sent_;
  };

  // Each category's first upload in a session is marked as a full sync for the targets.
  struct FirstSync {
    bool pending = true;
    bool Take()
    { return std::exchange(pending, false); }
  };

  // The changed records of one category, queued for upload in one request.
  class Batch
  {
  public:
    explicit Batch(SyncConfig::Type type)
        : type_(type)
    {
    }

    void Add(json record)
    { records_.push_back(std::move(record)); }

    void Send(FirstSync* first = nullptr)
    {
      if (!records_.empty())
        Queue(type_, records_, first && first->Take());
    }

  private:
    SyncConfig::Type type_;
    json             records_ = json::array();
  };

  template <typename Message> std::optional<Message> Parse(const std::string& bytes, std::string_view what)
  {
    Message message;
    if (message.ParseFromString(bytes))
      return message;
    spdlog::error("Failed to parse {}", what);
    return std::nullopt;
  }

  void Trace(std::string_view what, size_t count)
  { logging::trace("PROCESS", what, fmt::format("Processing {}", count)); }

  // Officers and forbidden tech: rank or tier, level, shards.
  using RankLevelShards = std::tuple<int32_t, int32_t, int32_t>;

  struct ShipState {
    int32_t              tier;
    int32_t              level;
    double               level_percentage;
    std::vector<int64_t> components;

    // Level progress jitters in the last digits; only a real change is worth an upload.
    bool operator==(const ShipState& other) const
    {
      return tier == other.tier && level == other.level && std::fabs(level_percentage - other.level_percentage) < 0.01
             && components == other.components;
    }
  };

  SentState<int64_t, int64_t>                               modules, resources, alliance_resources, slots;
  SentState<int64_t, int64_t> haven_pieces;
    SentState<int64_t, std::tuple<int32_t, int32_t, int64_t>> haven_map_buildings;
  SentState<int64_t, std::pair<int32_t, int64_t>>           buffs;
  SentState<std::pair<int32_t, int64_t>, int64_t, PairHash> inventory;
  SentState<std::string, bool>                              active_jobs;
  SentState<uint64_t, RankLevelShards>                      officers, techs;
  SentState<int64_t, int32_t>                               research;
  SentState<std::pair<int64_t, int64_t>, int32_t, PairHash> traits;
  SentState<int64_t, ShipState>                             ships;
  SentState<int64_t, size_t>                                away_assignments;
  // A hash of each deployed fleet's server record.
  SentState<int64_t, size_t>                                deployed_fleets;
  std::unordered_set<int64_t>                               active_missions, completed_missions;
  int32_t                                                   emerald_chain_level = -1;
  // The last store offers snapshot sent.
  json store_offers;
  // The protobuf and JSON resource lists are flagged separately, as two different first uploads.
  FirstSync first_away_assignments, first_buffs, first_inventory, first_jobs, first_resources, first_json_resources,
      first_alliance_resources, first_ships, first_profile, first_haven_pieces, first_haven_map;
  // The own profile's last level, Power and Haven unlock sent. XP rides along but is not part of the key: an XP-only
  // change waits for the next keyed change.
  std::optional<std::tuple<int32_t, uint64_t, bool>> own_profile_sent;

  // An away assignment is compared by a hash of its whole record: officers, state and job.
  void AddAwayAssignment(Batch& batch, const pb::AwayAssignmentInstance& instance)
  {
    // officerIds is a protobuf map, whose order is not slot order. Sorting keeps the array, and so the hash, stable.
    std::vector<std::pair<int64_t, int64_t>> officer_slots(instance.officerids().begin(), instance.officerids().end());
    std::ranges::sort(officer_slots, {}, &std::pair<int64_t, int64_t>::first);
    auto officer_ids = json::array();
    for (const auto& [slot, officer] : officer_slots)
      if (officer != 0)
        officer_ids.push_back(officer);

    auto officer_traits = json::array();
    for (const auto& trait : instance.officertraits())
      officer_traits.push_back({{"tid", trait.traitid()}, {"max_level", trait.maxlevel()}});

    // Same ordering concern as officer_ids: sort by trait id so the array and the de-dup hash are stable.
    std::vector<std::pair<int64_t, int32_t>> critical_traits(instance.parameters().criticaltraitscores().begin(),
                                                             instance.parameters().criticaltraitscores().end());
    std::ranges::sort(critical_traits, {}, &std::pair<int64_t, int32_t>::first);
    auto critical_trait_scores = json::array();
    for (const auto& [tid, score] : critical_traits)
      critical_trait_scores.push_back({{"tid", tid}, {"score", score}});

    // The chances are in the de-dup hash, so an assignment is sent again when they change.
    json record{{"type", SyncConfig::Type::AwayAssignments},
                {"aid", instance.id()},
                {"template_id", instance.awayassignmenttemplateid()},
                {"state", instance.state()},
                {"officer_ids", officer_ids},
                {"job_uuid", instance.jobuuid()},
                {"duration", instance.parameters().duration()},
                {"rarity", instance.rarity()},
                {"officer_traits", officer_traits},
                {"critical_trait_scores", critical_trait_scores},
                {"success_chance", instance.parameters().successchance()},
                {"critical_success_chance", instance.parameters().criticalsuccesschance()},
                {"max_critical_success_chance", instance.maxcriticalsuccesschance()},
                {"key_stat", instance.keystat()},
                {"max_assignable_officers", instance.maxassignableofficerscount()},
                {"attack_weight", instance.attackweight()},
                {"defense_weight", instance.defenseweight()},
                {"health_weight", instance.healthweight()}};
    if (away_assignments.Changed(instance.id(), std::hash<json>{}(record)))
      batch.Add(std::move(record));
  }

  // The full list. An assignment that left it was collected.
  void ProcessAwayAssignments(const std::string& bytes)
  {
    auto response = Parse<pb::AwayAssignmentUserListResponse>(bytes, "away assignments");
    if (!response)
      return;
    Trace("away assignments", response->instances_size());

    Batch                       batch(SyncConfig::Type::AwayAssignments);
    std::unordered_set<int64_t> present;
    for (const auto& instance : response->instances()) {
      present.insert(instance.id());
      AddAwayAssignment(batch, instance);
    }
    away_assignments.Prune(present, [&](int64_t id) {
      batch.Add({{"type", "collected_" + SyncConfig::Type::AwayAssignments}, {"aid", id}});
    });
    batch.Send(&first_away_assignments);
  }

  // One assignment as it starts: first ready to start with its officers set, then in progress with its job.
  void ProcessAwayAssignment(const std::string& bytes)
  {
    auto instance = Parse<pb::AwayAssignmentInstance>(bytes, "away assignment instance");
    if (!instance)
      return;
    logging::trace("PROCESS", "away assignment", fmt::format("Processing away assignment {}", instance->id()));

    Batch batch(SyncConfig::Type::AwayAssignments);
    AddAwayAssignment(batch, *instance);
    batch.Send();
  }

  void ProcessBattleHeaders(const std::string& bytes)
  {
    auto response = Parse<pb::BattleResultHeadersResponse>(bytes, "battle result headers");
    if (!response)
      return;
    Trace("battle headers", response->headers_size());

    std::vector<uint64_t> battle_ids;
    for (const auto& header : response->headers())
      battle_ids.push_back(static_cast<uint64_t>(header.id()));
    QueueNewBattles(battle_ids);
  }

  void ProcessBuffs(const std::string& bytes)
  {
    auto response = Parse<pb::GlobalActiveBuffsResponse>(bytes, "global active buffs");
    if (!response)
      return;
    Trace("global buffs", response->globalactivebuffs_size());

    Batch                       batch(SyncConfig::Type::Buffs);
    std::unordered_set<int64_t> present;
    for (const auto& buff : response->globalactivebuffs()) {
      present.insert(buff.buffid());
      const bool    expires = buff.has_activebuff() && buff.activebuff().has_expirytime();
      const int64_t expiry  = expires ? buff.activebuff().expirytime().seconds() : -1;
      if (buffs.Changed(buff.buffid(), {buff.level(), expiry}))
        batch.Add({{"type", SyncConfig::Type::Buffs},
                   {"bid", buff.buffid()},
                   {"level", buff.level()},
                   {"expiry_time", expires ? json(expiry) : json(nullptr)}});
    }
    buffs.Prune(present, [&](int64_t id) { batch.Add({{"type", "expired_" + SyncConfig::Type::Buffs}, {"bid", id}}); });
    batch.Send(&first_buffs);
  }

  // The emerald chain level is the highest tier the alliance has claimed.
  void ProcessAllianceGameProperties(const std::string& bytes)
  {
    auto response = Parse<pb::AllianceGamePropertiesResponse>(bytes, "alliance games properties");
    if (!response)
      return;

    for (const auto& property : response->properties()) {
      if (property.propertyname() != "claimed_loyalty_tiers")
        continue;
      int32_t level = -1;
      for (const auto& tier : property.valuelist()) {
        int32_t value = -1;
        std::from_chars(tier.data(), tier.data() + tier.size(), value);
        level = std::max(level, value);
      }
      if (level != std::exchange(emerald_chain_level, level)) {
        Batch batch(SyncConfig::Type::EmeraldChain);
        batch.Add({{"type", SyncConfig::Type::EmeraldChain}, {"level", level}});
        batch.Send();
      }
      return;
    }
  }

  void ProcessStarbaseModules(const std::string& bytes)
  {
    auto response = Parse<pb::StarbaseModulesResponse>(bytes, "starbase modules");
    if (!response)
      return;
    Trace("starbase modules", response->modulestates_size());

    Batch batch(SyncConfig::Type::Buildings);
    for (const auto& module : response->modulestates())
      if (modules.Changed(module.id(), module.level()))
        batch.Add({{"type", SyncConfig::Type::Buildings}, {"bid", module.id()}, {"level", module.level()}});
    batch.Send();
  }

  void ProcessHavenBase(const std::string& bytes)
  {
    auto response = Parse<pb::PlanetaryBase>(bytes, "planetary base");
    if (!response)
      return;
    Trace("planetary base data", response->pieces_size());

    Batch batch(SyncConfig::Type::Haven);
    for (const auto& [piece, count] : response->pieces())
      if (haven_pieces.Changed(piece, count))
        batch.Add({{"type", SyncConfig::Type::Haven + "_pieces"}, {"sid", piece}, {"count", count}});
    batch.Send(&first_haven_pieces);
  }

  void ProcessHavenMap(const std::string& bytes)
  {
    auto response = Parse<pb::PlanetaryMapData>(bytes, "planetary map");
    if (!response)
      return;
    Trace("planetary map data", response->buildings_size());

    Batch batch(SyncConfig::Type::Haven);
    for (const auto& [id, building] : response->buildings())
      if (haven_map_buildings.Changed(id, {building.level(), static_cast<int32_t>(building.status()), building.position()}))
        batch.Add({{"type", SyncConfig::Type::Haven}, {"sid", building.specid()}, {"id", id},
                   {"level", building.level()}, {"status", building.status()}, {"position", building.position()}});
    batch.Send(&first_haven_map);
  }

  // The game's record of the local player, or nullptr.
  UserProfile* LocalUserProfile()
  {
    auto* manager = UserProfileManager::Instance();
    return manager ? manager->GetLocalUserProfile() : nullptr;
  }

  void ProcessHavenMapResponse(const std::string& bytes)
  {
    auto response = Parse<pb::PlanetaryMapResponse>(bytes, "planetary map response");
    if (!response)
      return;
    const auto& owner = response->owneruserid();
    if (!owner.empty()) {
      auto* profile = LocalUserProfile();
      auto* user_id = profile ? profile->UserId() : nullptr;
      if (!user_id || owner != to_string(user_id))
        return;
    }
    Batch batch(SyncConfig::Type::Haven);
    for (const auto& [id, building] : response->mapdata().buildings())
      if (haven_map_buildings.Changed(id, {building.level(), static_cast<int32_t>(building.status()), building.position()}))
        batch.Add({{"type", SyncConfig::Type::Haven}, {"sid", building.specid()}, {"id", id},
                   {"level", building.level()}, {"status", building.status()}, {"position", building.position()}});
    batch.Send(&first_haven_map);
  }

  void ProcessHavenProsperity(const std::string& bytes)
  {
    auto response = Parse<pb::PlanetaryMapProsperityResponse>(bytes, "planetary map prosperity");
    if (!response)
      return;

    Batch batch(SyncConfig::Type::Haven);
    batch.Add({{"type", SyncConfig::Type::Haven + "_prosperity"}, {"prosperity", response->prosperity()}});
    batch.Send();
  }

  // Sends the local player's own profile; `self` and `level` come from the game thread. Only the record whose user id
  // is `self` is read. The server record carries level 0, so the in-memory Level is used: it lags one own-record
  // response after a level-up.
  void ProcessOwnProfile(const std::string& bytes, const std::string& self, int32_t level)
  {
    auto response = Parse<pb::UserProfilesResponse>(bytes, "own user profile");
    if (!response)
      return;
    const pb::UserProfile* own = nullptr;
    for (const auto& profile : response->userprofiles()) {
      if (profile.userid() == self) {
        own = &profile;
        break;
      }
    }
    if (!own)
      return;

    const auto key = std::make_tuple(level, own->militarymight(), own->planetarybaseunlocked());
    if (own_profile_sent == key)
      return;
    own_profile_sent = key;
    Batch batch(SyncConfig::Type::Profile);
    batch.Add({{"type", SyncConfig::Type::Profile},
               {"uid", self},
               {"level", level},
               {"xp", own->xp()},
               {"military_might", own->militarymight()},
               {"planetary_base_unlocked", own->planetarybaseunlocked()}});
    batch.Send(&first_profile);
  }

  void ProcessInventory(const std::string& bytes)
  {
    auto response = Parse<pb::InventoryResponse>(bytes, "player inventories");
    if (!response)
      return;
    Trace("player inventories", response->inventories_size());

    Batch batch(SyncConfig::Type::Inventory);
    for (const auto& items : response->inventories() | std::views::values) {
      for (const auto& item : items.items()) {
        if (!item.has_commonparams())
          continue;
        const auto id = item.commonparams().refid();
        if (inventory.Changed({item.type(), id}, item.count()))
          batch.Add({{"type", SyncConfig::Type::Inventory},
                     {"item_type", item.type()},
                     {"refid", id},
                     {"count", item.count()}});
      }
    }
    batch.Send(&first_inventory);
  }

  // The fields that identify what a job is working on, or nothing for job types that are not synced.
  std::optional<json> JobDetails(const pb::Job& job)
  {
    switch (job.type()) {
      case pb::JOBTYPE_RESEARCH:
        return json{{"rid", job.researchparams().projectid()}, {"level", job.researchparams().level()}};
      case pb::JOBTYPE_STARBASECONSTRUCTION:
        return json{{"bid", job.starbaseconstructionparams().moduleid()},
                    {"level", job.starbaseconstructionparams().level()}};
      case pb::JOBTYPE_SHIPTIERUP:
        return json{{"psid", job.tierupshipparams().shipid()}, {"tier", job.tierupshipparams().newtier()}};
      case pb::JOBTYPE_SHIPSCRAP:
        return json{{"psid", job.scrapyardparams().shipid()},
                    {"hull_id", job.scrapyardparams().hullid()},
                    {"level", job.scrapyardparams().level()}};
      case pb::JOBTYPE_AWAYASSIGNMENT:
        return json{{"aid", job.awayassignmentparams().awayassignmentinstanceid()}};
      // Part of fleet sync: jobs-only targets see nothing new. The repair changes the fleet's state later, so the
      // fleet is re-read then.
      case pb::JOBTYPE_REPAIRFLEET:
        if (!Config::Get().sync_options.fleets)
          return std::nullopt;
        RereadFleet(job.repairfleetparams().fleetid());
        return json{{"fid", job.repairfleetparams().fleetid()}};
      default:
        return std::nullopt;
    }
  }

  // Jobs are sent when they start and reported completed when they leave the list.
  void ProcessJobs(const std::string& bytes)
  {
    auto response = Parse<pb::JobResponse>(bytes, "jobs");
    if (!response)
      return;
    Trace("jobs", response->jobs_size());

    Batch                           batch(SyncConfig::Type::Jobs);
    std::unordered_set<std::string> present;
    for (const auto& job : response->jobs()) {
      present.insert(job.uuid());
      if (!active_jobs.Changed(job.uuid(), true))
        continue;
      auto details = JobDetails(job);
      if (!details)
        continue;
      json record{{"type", SyncConfig::Type::Jobs},
                  {"job_type", job.type()},
                  {"uuid", job.uuid()},
                  {"start_time", job.starttime().seconds()},
                  {"duration", job.duration()},
                  {"reduction", job.reductioninseconds()}};
      record.update(*details);
      batch.Add(std::move(record));
    }
    active_jobs.Prune(present, [&](const std::string& uuid) {
      batch.Add({{"type", "completed_" + SyncConfig::Type::Jobs}, {"uuid", uuid}});
    });
    batch.Send(&first_jobs);
  }

  // The whole set of active missions is sent whenever it changes.
  void ProcessActiveMissions(const std::string& bytes)
  {
    auto response = Parse<pb::ActiveMissionsResponse>(bytes, "active missions");
    if (!response)
      return;
    Trace("active missions", response->activemissions_size());

    std::unordered_set<int64_t> current;
    for (const auto& mission : response->activemissions())
      current.insert(mission.id());
    if (current == active_missions)
      return;
    active_missions = std::move(current);

    Batch batch(SyncConfig::Type::Missions);
    for (const auto mission : active_missions)
      batch.Add({{"type", "active_" + SyncConfig::Type::Missions}, {"mid", mission}});
    batch.Send();
  }

  // Completed missions only grow, so only the new ones are sent.
  void ProcessCompletedMissions(const std::string& bytes)
  {
    auto response = Parse<pb::CompletedMissionsResponse>(bytes, "completed missions");
    if (!response)
      return;
    Trace("completed missions", response->completedmissions_size());

    Batch batch(SyncConfig::Type::Missions);
    for (const auto mission : response->completedmissions())
      if (completed_missions.insert(mission).second)
        batch.Add({{"type", SyncConfig::Type::Missions}, {"mid", mission}});
    batch.Send();
  }

  void ProcessOfficers(const std::string& bytes)
  {
    auto response = Parse<pb::OfficersResponse>(bytes, "officers");
    if (!response)
      return;
    Trace("officers", response->officers_size());

    Batch batch(SyncConfig::Type::Officer);
    for (const auto& officer : response->officers())
      if (officers.Changed(officer.id(), {officer.rankindex(), officer.level(), officer.shardcount()}))
        batch.Add({{"type", SyncConfig::Type::Officer},
                   {"oid", officer.id()},
                   {"rank", officer.rankindex()},
                   {"level", officer.level()},
                   {"shard_count", officer.shardcount()}});
    batch.Send();
  }

  void ProcessResearch(const std::string& bytes)
  {
    auto response = Parse<pb::ResearchTreesState>(bytes, "research trees state");
    if (!response)
      return;
    Trace("research projects", response->researchprojectlevels_size());

    Batch batch(SyncConfig::Type::Research);
    for (const auto& [id, level] : response->researchprojectlevels())
      if (research.Changed(id, level))
        batch.Add({{"type", SyncConfig::Type::Research}, {"rid", id}, {"level", level}});
    batch.Send();
  }

  // Resource amounts arrive as full lists and as deltas, for the player and the alliance bank. All feed the same
  // last-sent state per owner, which is why every response goes through one thread in order.
  void AddResource(Batch& batch, SentState<int64_t, int64_t>& state, std::string_view prefix, int64_t id,
                   int64_t amount)
  {
    if (state.Changed(id, amount))
      batch.Add(
          {{"type", std::string(prefix) + to_string(SyncConfig::Type::Resources)}, {"rid", id}, {"amount", amount}});
  }

  void ProcessResources(const std::string& bytes)
  {
    auto response = Parse<pb::ResourcesResponse>(bytes, "resources");
    if (!response)
      return;
    Trace("resources", response->resources_size());

    Batch batch(SyncConfig::Type::Resources);
    for (const auto& resource : response->resources())
      AddResource(batch, resources, "", resource.id(), resource.currentamount());
    batch.Send(&first_resources);
  }

  void ProcessResourcesDelta(const std::string& bytes)
  {
    auto response = Parse<pb::ResourcesDeltaResponse>(bytes, "resources delta");
    if (!response)
      return;
    Trace("resource changes", response->resources_size());

    Batch batch(SyncConfig::Type::Resources);
    for (const auto& resource : response->resources())
      AddResource(batch, resources, "", resource.id(), resource.amount());
    batch.Send();
  }

  void ProcessAllianceResources(const std::string& bytes)
  {
    auto response = Parse<pb::AllianceGetBankResourcesResponse>(bytes, "alliance resources");
    if (!response)
      return;
    Trace("alliance resources", response->items_size());

    Batch batch(SyncConfig::Type::Resources);
    for (const auto& item : response->items())
      if (item.has_commonparams())
        AddResource(batch, alliance_resources, "alliance_", item.commonparams().refid(), item.count());
    batch.Send(&first_alliance_resources);
  }

  void ProcessAllianceResourcesDelta(const std::string& bytes)
  {
    auto response = Parse<pb::AllianceResourcesDeltaResponse>(bytes, "alliance resources delta");
    if (!response)
      return;
    Trace("alliance resource changes", response->resources_size());

    Batch batch(SyncConfig::Type::Resources);
    for (const auto& resource : response->resources())
      AddResource(batch, alliance_resources, "alliance_", resource.id(), resource.amount());
    batch.Send();
  }

  // Slots change through server responses and through realtime messages, which describe the same slot in
  // different shapes. Both reduce it to its params and one state value: the item id, or a hash of the params
  // for slot types whose params are the content.
  void AddSlot(Batch& batch, int64_t id, int32_t slot_type, const json& spec_id, const json& item_id,
               const json& params, int64_t state)
  {
    if (slots.Changed(id, state))
      batch.Add({{"type", SyncConfig::Type::Slots},
                 {"sid", id},
                 {"slot_type", slot_type},
                 {"spec_id", spec_id},
                 {"item_id", item_id},
                 {"params", params}});
  }

  int64_t Hash(const json& params)
  { return static_cast<int64_t>(std::hash<json>{}(params)); }

  json Seconds(bool has, int64_t seconds)
  { return has ? json(seconds) : json(nullptr); }

  void AddSlot(Batch& batch, const pb::EntitySlot& slot)
  {
    json    params;
    int64_t state = slot.has_slotitemid() ? slot.slotitemid().value() : -1;
    switch (slot.slottype()) {
      case pb::SLOTTYPE_CONSUMABLE:
        if (slot.has_consumableslotparams()) {
          const auto& consumable = slot.consumableslotparams();
          params["expiry_time"]  = Seconds(consumable.has_expirytime(), consumable.expirytime().seconds());
        }
        break;
      case pb::SLOTTYPE_OFFICERPRESET:
        if (slot.has_officerpresetslotparams()) {
          const auto& preset = slot.officerpresetslotparams();
          params = {{"name", preset.name()}, {"order", preset.order()}, {"officer_ids", preset.officerids()}};
          state  = Hash(params);
        }
        break;
      case pb::SLOTTYPE_FLEETCOMMANDER:
        if (slot.has_fleetcommanderslotparams())
          params["order"] = slot.fleetcommanderslotparams().order();
        break;
      case pb::SLOTTYPE_SELECTABLESKILL:
        if (slot.has_selectableskillslotparams()) {
          const auto& skill             = slot.selectableskillslotparams();
          params["cooldown_expiration"] = Seconds(skill.has_cooldownexpiration(), skill.cooldownexpiration().seconds());
        }
        break;
      case pb::SLOTTYPE_FORBIDDENTECH:
        if (slot.has_forbiddentechslotparams()) {
          const auto& tech = slot.forbiddentechslotparams();
          params           = {{"owner_id", tech.ownerid()}, {"tier", tech.tier()}, {"level", tech.level()}};
          state            = Hash(params);
        }
        break;
      case pb::SLOTTYPE_FLEETPRESET:
        if (slot.has_fleetpresetslotparams()) {
          const auto& preset = slot.fleetpresetslotparams();
          auto        setups = json::array();
          for (const auto& setup : preset.setups())
            setups.push_back({{"drydock_id", setup.drydockid()},
                              {"ship_id", setup.shipids()[0]},
                              {"officer_ids", setup.officerids()}});
          params = {{"name", preset.name()}, {"order", preset.order()}, {"setup", setups}};
          state  = Hash(params);
        }
        break;
      default:
        return;
    }
    AddSlot(batch, slot.id(), slot.slottype(), slot.slotspecid(),
            slot.has_slotitemid() ? json(slot.slotitemid().value()) : json(nullptr), params, state);
  }

  void ProcessEntitySlots(const std::string& bytes)
  {
    auto response = Parse<pb::EntitySlots>(bytes, "entity slots");
    if (!response)
      return;
    Trace("entity slots", response->entityslots__size());

    Batch batch(SyncConfig::Type::Slots);
    for (const auto& slot : response->entityslots_())
      AddSlot(batch, slot);
    batch.Send();
  }

  void ProcessEntitySlotsData(const std::string& bytes)
  {
    auto response = Parse<pb::EntitySlotsData>(bytes, "entity slots data");
    if (!response)
      return;
    Trace("entity slot groups", response->entityslots_size());

    Batch batch(SyncConfig::Type::Slots);
    for (const auto& entity : response->entityslots())
      for (const auto& slot : entity.slots())
        AddSlot(batch, slot);
    batch.Send();
  }

  // Realtime slot updates are JSON with ISO timestamps and abbreviated fleet preset keys.
  void ProcessRealtimeSlot(const std::string& bytes)
  {
    auto data = json::parse(bytes, nullptr, false);
    if (data.is_discarded()) {
      spdlog::error("Failed to parse slots JSON");
      return;
    }
    try {
      const auto timestamp = [](const json& value) {
        const auto seconds = value.is_string() ? ParseUtcTimestamp(value.get_ref<const std::string&>()) : std::nullopt;
        return seconds ? json(*seconds) : json(nullptr);
      };

      const auto& item  = data["slot_item_id"];
      int64_t     state = item.is_null() ? -1 : item.get<int64_t>();
      json        params;
      const auto  type = data["slot_type"].get<int32_t>();
      switch (type) {
        case pb::SLOTTYPE_CONSUMABLE:
          params["expiry_time"] = timestamp(data["consumable_slot_params"]["expiry_time"]);
          break;
        case pb::SLOTTYPE_OFFICERPRESET:
          if (const auto& preset = data["officer_preset_slot_params"]; !preset.is_null()) {
            params = preset;
            state  = Hash(params);
          }
          break;
        case pb::SLOTTYPE_FLEETCOMMANDER:
          if (const auto& commander = data["fleet_commander_slot_params"]; !commander.is_null())
            params["order"] = commander["order"];
          break;
        case pb::SLOTTYPE_SELECTABLESKILL:
          if (const auto& skill = data["selectable_skill_slot_params"]; !skill.is_null())
            params["cooldown_expiration"] = timestamp(skill["cooldown_expiration"]);
          break;
        case pb::SLOTTYPE_FORBIDDENTECH:
          if (const auto& tech = data["forbidden_tech_slot_params"]; !tech.is_null()) {
            params = tech;
            state  = Hash(params);
          }
          break;
        case pb::SLOTTYPE_FLEETPRESET:
          if (const auto& preset = data["fleet_preset_slot_params"]; !preset.is_null()) {
            auto setups = json::array();
            for (const auto& setup : preset["setups"])
              setups.push_back({{"drydock_id", setup["d"]}, {"ship_id", setup["s"][0]}, {"officer_ids", setup["o"]}});
            params = {{"name", preset["name"]}, {"order", preset["order"]}, {"setup", setups}};
            state  = Hash(params);
          }
          break;
        default:
          return;
      }

      Batch batch(SyncConfig::Type::Slots);
      AddSlot(batch, data["slot_id"].get<int64_t>(), type, data["slot_spec_id"], item, params, state);
      batch.Send();
    } catch (const json::exception& e) {
      spdlog::error("Failed to parse slots JSON: {}", e.what());
    }
  }

  void ProcessForbiddenTech(const std::string& bytes)
  {
    auto response = Parse<pb::ForbiddenTechsResponse>(bytes, "forbidden techs");
    if (!response)
      return;
    Trace("forbidden/chaos techs", response->forbiddentechs_size());

    Batch batch(SyncConfig::Type::Tech);
    for (const auto& tech : response->forbiddentechs())
      if (techs.Changed(tech.id(), {tech.tier(), tech.level(), tech.shardcount()}))
        batch.Add({{"type", SyncConfig::Type::Tech},
                   {"fid", tech.id()},
                   {"tier", tech.tier()},
                   {"level", tech.level()},
                   {"shard_count", tech.shardcount()}});
    batch.Send();
  }

  void ProcessOfficerTraits(const std::string& bytes)
  {
    auto response = Parse<pb::OfficerTraitsResponse>(bytes, "active officer traits");
    if (!response)
      return;
    Trace("officers with traits", response->activeofficertraits_size());

    Batch batch(SyncConfig::Type::Traits);
    for (const auto& [officer, officer_traits] : response->activeofficertraits())
      for (const auto& trait : officer_traits.activetraits() | std::views::values)
        if (traits.Changed({officer, trait.traitid()}, trait.level()))
          batch.Add({{"type", SyncConfig::Type::Traits},
                     {"oid", officer},
                     {"tid", trait.traitid()},
                     {"level", trait.level()}});
    batch.Send();
  }

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

  // A store offers group carries whole store categories. The faction store's consumable offers go out as one
  // snapshot, again whenever it changes.
  void ProcessStoreOffers(const std::string& bytes)
  {
    auto response = Parse<content::Bundles>(bytes, "store offers");
    if (!response)
      return;
    Trace("store offers", response->bundles__size());

    constexpr std::string_view kCategory = "factions";
    struct Offer {
      const content::Bundle*      bundle;
      const content::ContentItem* consumable;
    };
    std::vector<Offer> offers;
    bool               has_category = false;
    for (const auto& bundle : response->bundles_()) {
      if (bundle.category() != kCategory)
        continue;
      has_category = true;
      if (const auto* consumable = OfferConsumable(bundle.content()))
        offers.push_back({&bundle, consumable});
    }
    if (!has_category)
      return;
    // In a fixed order, so neither the snapshot nor its hash depends on the server's.
    std::ranges::sort(offers, {}, [](const Offer& offer) {
      return std::pair{offer.bundle->bundleid(), std::string_view{offer.consumable->id()}};
    });

    auto records = json::array();
    for (const auto& [bundle, consumable] : offers) {
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
      records.push_back({{"bid", bundle->bundleid()},
                         {"cid", OfferId(consumable->id())},
                         {"cost", cost},
                         {"req", requirements},
                         {"valid_count", details.validcount()},
                         {"same_remaining_count", details.sameremainingcount()}});
    }

    json snapshot = {{"type", SyncConfig::Type::StoreOffers}, {"category", kCategory}, {"offers", records}};
    if (snapshot == store_offers)
      return;
    store_offers = snapshot;
    Batch batch(SyncConfig::Type::StoreOffers);
    batch.Add(std::move(snapshot));
    batch.Send();
  }

  void AddShip(Batch& batch, int64_t id, int64_t hull_id, const ShipState& state)
  {
    if (ships.Changed(id, state))
      batch.Add({{"type", SyncConfig::Type::Ships},
                 {"psid", id},
                 {"level", state.level},
                 {"level_percentage", state.level_percentage},
                 {"tier", state.tier},
                 {"hull_id", hull_id},
                 {"components", state.components}});
  }

  void ProcessShips(const std::string& bytes)
  {
    auto response = Parse<pb::PlayerShipsResponse>(bytes, "player ships");
    if (!response)
      return;
    Trace("ships", response->ships_size());

    Batch batch(SyncConfig::Type::Ships);
    for (const auto& ship : response->ships() | std::views::values)
      AddShip(batch, ship.id(), ship.hullid(),
              {ship.tier(), ship.level(), ship.levelpercentage(), {ship.components().begin(), ship.components().end()}});
    batch.Send(&first_ships);
  }

  // Early in a session resources and starbase modules come in the game's JSON responses rather than as protobuf
  // entity groups. Before client build 267, so did ships and battle headers.
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

  // The server's view of the player's deployed fleets. It resends every fleet on each update, so only fleets that
  // changed are sent; a fleet that left (docked, destroyed) is forgotten so it is sent again when it is back. Each
  // changed or departed fleet is re-read on the game thread, which fills in some values from this data.
  void ProcessDeployedFleets(const std::string& bytes)
  {
    auto response = Parse<pb::MyDeployedFleetsResponse>(bytes, "deployed fleets");
    if (!response)
      return;
    Trace("deployed fleets", response->fleets_size());

    const int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    Batch batch(SyncConfig::Type::Fleets);
    for (const auto& [fleet_id, fleet] : response->fleets()) {
      json record{{"type", SyncConfig::Type::Fleets},
                  {"fid", fleet_id},
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

      if (!deployed_fleets.Changed(fleet_id, std::hash<json>{}(record)))
        continue;
      record["t"] = now;
      batch.Add(std::move(record));
      RereadFleet(fleet_id);
    }
    deployed_fleets.Prune(response->fleets(), [](int64_t fleet_id) { RereadFleet(fleet_id); });
    batch.Send();
  }

  void ProcessJsonResources(const json& section)
  {
    Batch batch(SyncConfig::Type::Resources);
    for (const auto& [id, resource] : section.get<json::object_t>())
      AddResource(batch, resources, "", std::stoll(id), resource["current_amount"].get<int64_t>());
    batch.Send(&first_json_resources);
  }

  void ProcessJsonModules(const json& section)
  {
    Batch batch(SyncConfig::Type::Buildings);
    for (const auto& module : section.get<json::object_t>() | std::views::values) {
      const auto id    = module["id"].get<int64_t>();
      const auto level = module["level"].get<int32_t>();
      if (modules.Changed(id, level))
        batch.Add({{"type", SyncConfig::Type::Buildings}, {"bid", id}, {"level", level}});
    }
    batch.Send();
  }

  void ProcessJsonShips(const json& section)
  {
    Batch batch(SyncConfig::Type::Ships);
    for (const auto& ship : section.get<json::object_t>() | std::views::values)
      AddShip(batch, ship["id"].get<int64_t>(), ship["hull_id"].get<int64_t>(),
              {ship["tier"].get<int32_t>(), ship["level"].get<int32_t>(), ship["level_percentage"].get<double>(),
               ship["components"].get<std::vector<int64_t>>()});
    batch.Send(&first_ships);
  }

  void ProcessJsonBattles(const json& section)
  {
    std::vector<uint64_t> battle_ids;
    for (const auto& battle : section)
      battle_ids.push_back(battle["id"].get<uint64_t>());
    QueueNewBattles(battle_ids);
  }

  void ProcessJson(const std::string& bytes)
  {
    const auto& options = Config::Get().sync_options;
    try {
      const auto result = json::parse(bytes);
      for (const auto& [key, section] : result.items()) {
        logging::trace("PROCESS", key, fmt::format("Processing {} entries (JSON)", section.size()));
        if (key == "battle_result_headers" && options.battlelogs)
          ProcessJsonBattles(section);
        else if (key == "resources" && options.resources)
          ProcessJsonResources(section);
        else if (key == "starbase_modules" && options.buildings)
          ProcessJsonModules(section);
        else if (key == "ships" && options.ships)
          ProcessJsonShips(section);
      }
    } catch (const json::exception& e) {
      spdlog::error("Error parsing json: {}", e.what());
    }
  }

  // One thread processes every response in arrival order, so a delta is never applied before the full state it
  // updates.
  void RunJobs()
  {
    for (;;) {
      auto job = pending.Pop();
      try {
        job.process(job.bytes);
      } catch (const std::exception& e) {
        spdlog::error("Exception while processing sync data: {}", e.what());
      } catch (...) {
        spdlog::error("Unknown exception while processing sync data");
      }
    }
  }

  struct Route {
    EntityGroup::Type type;
    void (*process)(const std::string& bytes);
    // The category that must be enabled; null for responses that check their own sections.
    bool SyncConfig::* option;
  };

  const Route kRoutes[] = {
      {EntityGroup::Type::AwayAssignmentsList, ProcessAwayAssignments, &SyncConfig::away_assignments},
      {EntityGroup::Type::AwayAssignmentsInstance, ProcessAwayAssignment, &SyncConfig::away_assignments},
      {EntityGroup::Type::BattleResultHeaders, ProcessBattleHeaders, &SyncConfig::battlelogs},
      {EntityGroup::Type::UserProfiles, CachePlayerNames, &SyncConfig::battlelogs},
      {EntityGroup::Type::AllianceProfiles, CacheAllianceNames, &SyncConfig::battlelogs},
      {EntityGroup::Type::GlobalActiveBuffs, ProcessBuffs, &SyncConfig::buffs},
      {EntityGroup::Type::AllianceGetGameProperties, ProcessAllianceGameProperties, &SyncConfig::buffs},
      {EntityGroup::Type::StarbaseModules, ProcessStarbaseModules, &SyncConfig::buildings},
      {EntityGroup::Type::MyDeployedFleets, ProcessDeployedFleets, &SyncConfig::fleets},
      {EntityGroup::Type::PlanetaryBaseData, ProcessHavenBase, &SyncConfig::haven},
      {EntityGroup::Type::PlanetaryMapResponse, ProcessHavenMapResponse, &SyncConfig::haven},
      {EntityGroup::Type::PlanetaryMapData, ProcessHavenMap, &SyncConfig::haven},
      {EntityGroup::Type::PlanetaryMapProsperity, ProcessHavenProsperity, &SyncConfig::haven},
      {EntityGroup::Type::PlayerInventories, ProcessInventory, &SyncConfig::inventory},
      {EntityGroup::Type::Jobs, ProcessJobs, &SyncConfig::jobs},
      {EntityGroup::Type::ActiveMissions, ProcessActiveMissions, &SyncConfig::missions},
      {EntityGroup::Type::CompletedMissions, ProcessCompletedMissions, &SyncConfig::missions},
      {EntityGroup::Type::Officers, ProcessOfficers, &SyncConfig::officers},
      {EntityGroup::Type::ResearchTreesState, ProcessResearch, &SyncConfig::research},
      {EntityGroup::Type::Resources, ProcessResources, &SyncConfig::resources},
      {EntityGroup::Type::ResourcesDelta, ProcessResourcesDelta, &SyncConfig::resources},
      {EntityGroup::Type::AllianceGetBankResources, ProcessAllianceResources, &SyncConfig::resources},
      {EntityGroup::Type::ResourcesDeltaAlliance, ProcessAllianceResourcesDelta, &SyncConfig::resources},
      {EntityGroup::Type::Ships, ProcessShips, &SyncConfig::ships},
      {EntityGroup::Type::EntitySlots, ProcessEntitySlots, &SyncConfig::slots},
      {EntityGroup::Type::EntitySlotsData, ProcessEntitySlotsData, &SyncConfig::slots},
      {EntityGroup::Type::ForbiddenTechs, ProcessForbiddenTech, &SyncConfig::tech},
      {EntityGroup::Type::ActiveOfficerTraits, ProcessOfficerTraits, &SyncConfig::traits},
      {EntityGroup::Type::Json, ProcessJson, nullptr},
  };

  // Runs on the game thread: copies the bytes out and leaves the work to the processing thread.
  void HandleEntityGroup(EntityGroup* entity_group)
  {
    auto* group = entity_group ? entity_group->Group() : nullptr;
    if (!group || !group->bytes() || group->Length() <= 0)
      return;

    const auto  type    = entity_group->Type_();
    const auto& options = Config::Get().sync_options;
    const auto  bytes   = [group] {
      return std::string(reinterpret_cast<const char*>(group->bytes()->data()), static_cast<size_t>(group->Length()));
    };
    const auto route = std::ranges::find(kRoutes, type, &Route::type);
    if (route != std::end(kRoutes) && (!route->option || options.*route->option))
      pending.Push({route->process, bytes()});

    // The own profile needs the local player's id and level, which only the game thread can read. Skipped if the
    // local profile is not ready; a later response retries.
    if (type == EntityGroup::Type::UserProfiles && options.profile) {
      auto* local   = LocalUserProfile();
      auto* user_id = local ? local->UserId() : nullptr;
      if (auto self = user_id ? to_string(user_id) : std::string{}; !self.empty())
        pending.Push({[self = std::move(self), level = local->Level()](const std::string& bytes) {
                        ProcessOwnProfile(bytes, self, level);
                      },
                      bytes()});
    }
  }

  // Platform responses (store offers among them) pass through the same registry code as game server responses. Only
  // store offers are read from them, and only while a target wants them.
  void HandlePlatformResponse(PlatformServiceResponse* response)
  {
    if (auto* groups = response->EntityGroups())
      for (auto* entity_group : *groups) {
        auto* group = entity_group && entity_group->Type_() == PlatformEntityGroup::StoreOffers
                          ? entity_group->Group()
                          : nullptr;
        if (group && group->bytes() && group->Length() > 0)
          pending.Push({ProcessStoreOffers, std::string(reinterpret_cast<const char*>(group->bytes()->data()),
                                                        static_cast<size_t>(group->Length()))});
      }
  }

  using GameServerModelRegistry =
      il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Core", "GameServerModelRegistry">;
  using GameServer = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Core", "GameServer">;
  using SlotAssignRtcParser =
      il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Parsers", "SlotAssignRtcParser">;
  using SlotClearRtcParser =
      il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Parsers", "SlotClearRtcParser">;

  // IL2CPP value type: keep this parameter by value so following arguments retain their platform ABI positions.
  struct CentrifugoInfo {
    Il2CppString* Channel;
    int32_t       Offset;
  };
  static_assert(sizeof(CentrifugoInfo) == 16);

  void* RtcParser_ParseFinalPayload(auto original, void* _this, CentrifugoInfo centrifugoInfo,
                                    RealtimeDataPayload* realtimeDataPayload, void* finalPayload)
  {
    if (Config::Get().sync_options.slots && realtimeDataPayload && realtimeDataPayload->Target()
        && realtimeDataPayload->DataType() && realtimeDataPayload->Data()) {
      // DataType arrives as a number in a string. This runs inside the game's parser, so nothing here may throw.
      const auto type_string = to_string(realtimeDataPayload->DataType());
      int        type        = -1;
      std::from_chars(type_string.data(), type_string.data() + type_string.size(), type);
      if (type == DataType::JSON)
        pending.Push({ProcessRealtimeSlot, to_string(realtimeDataPayload->Data())});
    }
    return original(_this, centrifugoInfo, realtimeDataPayload, finalPayload);
  }

  void GameServerModelRegistry_ProcessResultInternal(auto original, void* _this, void* parsing_context,
                                                     ServiceResponse* service_response, MethodInfo* method)
  {
    // The hooked method is shared generic code; PlatformModelRegistry passes its own response type.
    if (auto* groups =
            service_response && service_response->Is<ServiceResponse>() ? service_response->EntityGroups() : nullptr)
      for (auto* group : *groups)
        HandleEntityGroup(group);
    else if (auto* platform = service_response && Config::Get().sync_options.store_offers
                                  ? service_response->As<PlatformServiceResponse>()
                                  : nullptr)
      HandlePlatformResponse(platform);
    return original(_this, parsing_context, service_response, method);
  }

  void PrimeApp_InitPrimeServer(auto original, void* _this, Il2CppString* gameServerUrl, Il2CppString* gatewayServerUrl,
                                Il2CppString* sessionId, Il2CppString* serverRegion)
  {
    original(_this, gameServerUrl, gatewayServerUrl, sessionId, serverRegion);
    SetGameServer(to_string(gameServerUrl), to_string(sessionId));
  }

  void GameServer_Initialise(auto original, void* _this, Il2CppString* sessionId, Il2CppString* gameVersion,
                             bool encryptRequests, Il2CppString* serverRegion)
  {
    original(_this, sessionId, gameVersion, encryptRequests, serverRegion);
    SetGameVersion(to_string(gameVersion));
  }

  void GameServer_SetInstanceIdHeader(auto original, void* _this, int32_t instanceId)
  {
    original(_this, instanceId);
    SetInstanceId(instanceId);
  }
} // namespace
} // namespace data_sync

void InstallSyncPatches()
{
  using namespace data_sync;

  if (auto m = GameServerModelRegistry::Method("ProcessResultInternal"))
    SPUD_STATIC_DETOUR(m.Pointer(), GameServerModelRegistry_ProcessResultInternal);
  if (auto m = PrimeApp::Class::Method("InitPrimeServer"))
    SPUD_STATIC_DETOUR(m.Pointer(), PrimeApp_InitPrimeServer);

  // This hook only captures the game version for the X-PRIME-VERSION header, which has a fallback. It can be
  // turned off for users whose other mods detour the same method: a native and a managed detour on one prologue
  // corrupt each other and crash at login.
  if (!Config::Get().installGameVersionHook)
    spdlog::info("Sync: skipping GameServer::Initialise hook (patches.game_version = false)");
  else if (auto m = GameServer::Method("Initialise"))
    SPUD_STATIC_DETOUR(m.Pointer(), GameServer_Initialise);

  if (auto m = GameServer::Method("SetInstanceIdHeader"))
    SPUD_STATIC_DETOUR(m.Pointer(), GameServer_SetInstanceIdHeader);

  // Each parser overrides ParseFinalPayload, so these are two distinct native methods.
  if (auto m = SlotAssignRtcParser::Method("ParseFinalPayload"))
    SPUD_STATIC_DETOUR(m.Pointer(), RtcParser_ParseFinalPayload);
  if (auto m = SlotClearRtcParser::Method("ParseFinalPayload"))
    SPUD_STATIC_DETOUR(m.Pointer(), RtcParser_ParseFinalPayload);

  InstallFleetSync();

  StartUploads();
  StartBattleLogs();
  std::thread(RunJobs).detach();
}

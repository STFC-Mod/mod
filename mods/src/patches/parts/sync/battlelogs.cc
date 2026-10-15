// Battle logs: the game only sends battle headers, so each new battle's journal is fetched from the game server
// with the participants' player and alliance names resolved, then uploaded.

#include "patches/parts/sync/sync.h"

#include "file.h"

#include <Digit.PrimeServer.Models.pb.h>

#include <EASTL/algorithm.h>
#include <EASTL/bonus/ring_buffer.h>
#include <spdlog/fmt/fmt.h>

#include <chrono>
#include <fstream>
#include <optional>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#if _WIN32
#include <winrt/windows.foundation.h>
#endif

namespace data_sync
{
namespace
{
  using json  = nlohmann::json;
  using Clock = std::chrono::steady_clock;

  // Battles already sent, persisted so a restart does not resend the recent ones. The game lists far fewer
  // headers than this at a time.
  constexpr size_t kSentBattlesKept = 300;

  // Names seen in battles and profile responses, kept for [sync] resolver_cache_ttl seconds. A busy server shows
  // a few thousand players a day; past this size, expired entries are dropped before adding more.
  constexpr size_t kNameCacheSize = 10'000;

  // Filled by the processing thread from responses the game requested and by the battle-log thread from its own
  // requests, so it locks.
  template <typename Key, typename Value> class NameCache
  {
  public:
    std::optional<Value> Find(const Key& key)
    {
      std::scoped_lock lock(mutex_);
      const auto       it = entries_.find(key);
      if (it == entries_.end())
        return std::nullopt;
      if (it->second.expires_at <= Clock::now()) {
        entries_.erase(it);
        return std::nullopt;
      }
      return it->second.value;
    }

    void Store(const Key& key, Value value)
    {
      const auto       expires_at = Clock::now() + std::chrono::seconds(Config::Get().sync_resolver_cache_ttl);
      std::scoped_lock lock(mutex_);
      if (entries_.size() >= kNameCacheSize)
        std::erase_if(entries_, [now = Clock::now()](const auto& entry) { return entry.second.expires_at <= now; });
      entries_.insert_or_assign(key, Entry{std::move(value), expires_at});
    }

  private:
    struct Entry {
      Value             value;
      Clock::time_point expires_at;
    };
    std::mutex                     mutex_;
    std::unordered_map<Key, Entry> entries_;
  };

  struct Player {
    std::string name;
    int64_t     alliance = -1;
  };

  struct Alliance {
    std::string name;
    std::string tag;
  };

  NameCache<std::string, Player> players;
  NameCache<int64_t, Alliance>   alliances;

  eastl::ring_buffer<uint64_t> sent_battles;
  std::mutex                   sent_mutex;

  BoundedQueue<uint64_t> battles{"battle log", kSentBattlesKept};

  // The players in a battle. Fleet battles list them in deployed_fleets (fleet data with ref_ids is an NPC's);
  // cracked stations have no fleet, so a base raid's defender only shows up in the journal's own ids.
  std::unordered_set<std::string> Participants(const json& journal)
  {
    std::unordered_set<std::string> ids;
    for (const char* side : {"target_fleet_data", "initiator_fleet_data"}) {
      // operator[] on a const json is undefined for a missing key, so every lookup goes through value().
      const auto fleet = journal.value(side, json::object());
      if (!fleet.value("ref_ids", json()).is_null())
        continue;
      for (const auto& deployed : fleet.value("deployed_fleets", json::array()))
        if (deployed.contains("uid"))
          ids.insert(deployed["uid"].get<std::string>());
    }
    for (const char* key : {"initiator_id", "target_id"}) {
      // NPC ids contain '_' (mar_61, npc_<id>_1); there is nothing to resolve for them.
      if (const auto& id = journal.value(key, json());
          id.is_string() && !id.get_ref<const std::string&>().empty()
          && id.get_ref<const std::string&>().find('_') == std::string::npos)
        ids.insert(id.get<std::string>());
    }
    return ids;
  }

  // Fetches profiles the cache does not have. A malformed profile is skipped, not the whole reply. The results are
  // returned as well as cached, since a zero TTL expires them at once.
  std::unordered_map<std::string, Player> FetchPlayers(const std::vector<std::string>& ids)
  {
    std::unordered_map<std::string, Player> fetched;
    if (ids.empty())
      return fetched;
    logging::trace("PROCESS", "combat log", fmt::format("Fetching {} player profiles", ids.size()));
    const auto reply =
        json::parse(RequestGameData("/user_profile/profiles", json{{"user_ids", ids}}.dump()), nullptr, false);
    const auto profiles = reply.is_object() ? reply.value("user_profiles", json::object()) : json::object();
    for (const auto& [id, profile] : profiles.items()) {
      try {
        Player player{profile["name"].get<std::string>(), profile["alliance_id"].get<int64_t>()};
        players.Store(id, player);
        fetched.emplace(id, std::move(player));
      } catch (const json::exception& e) {
        spdlog::error("Failed to parse user profile {}: {}", id, e.what());
      }
    }
    return fetched;
  }

  std::unordered_map<int64_t, Alliance> FetchAlliances(const std::vector<int64_t>& ids)
  {
    std::unordered_map<int64_t, Alliance> fetched;
    if (ids.empty())
      return fetched;
    logging::trace("PROCESS", "combat log", fmt::format("Fetching {} alliance profiles", ids.size()));
    const json request{{"user_current_rank", 0}, {"alliance_id", 0}, {"alliance_ids", ids}};
    const auto reply =
        json::parse(RequestGameData("/alliance/get_alliances_public_info", request.dump()), nullptr, false);
    const auto infos = reply.is_object() ? reply.value("alliances_info", json::object()) : json::object();
    for (const auto& [key, info] : infos.items()) {
      try {
        const auto id = info["id"].get<int64_t>();
        Alliance   alliance{info["name"].get<std::string>(), info["tag"].get<std::string>()};
        alliances.Store(id, alliance);
        fetched.emplace(id, std::move(alliance));
      } catch (const json::exception& e) {
        spdlog::error("Failed to parse alliance profile: {}", e.what());
      }
    }
    return fetched;
  }

  // {player id: {name, alliance_name, alliance_tag}} for everyone in the battle, fetching what the caches lack.
  // A player whose alliance could not be resolved keeps alliance_id instead of a name and tag. Players without an
  // alliance (id 0 or below) are not looked up.
  json ResolveNames(const json& journal)
  {
    std::unordered_map<std::string, Player> known;
    std::vector<std::string>                missing_players;
    for (const auto& id : Participants(journal)) {
      if (auto player = players.Find(id))
        known.emplace(id, std::move(*player));
      else
        missing_players.push_back(id);
    }
    known.merge(FetchPlayers(missing_players));

    std::unordered_map<int64_t, Alliance> known_alliances;
    std::unordered_set<int64_t>           missing_alliances;
    for (const auto& player : known | std::views::values) {
      if (player.alliance <= 0 || known_alliances.contains(player.alliance))
        continue;
      if (auto alliance = alliances.Find(player.alliance))
        known_alliances.emplace(player.alliance, std::move(*alliance));
      else
        missing_alliances.insert(player.alliance);
    }
    known_alliances.merge(FetchAlliances({missing_alliances.begin(), missing_alliances.end()}));

    auto names = json::object();
    for (const auto& [id, player] : known) {
      json       entry{{"name", player.name}, {"alliance_name", nullptr}, {"alliance_tag", nullptr}};
      const auto alliance = known_alliances.find(player.alliance);
      if (alliance != known_alliances.end()) {
        entry["alliance_name"] = alliance->second.name;
        entry["alliance_tag"]  = alliance->second.tag;
      } else {
        entry["alliance_id"] = player.alliance;
      }
      names[id] = std::move(entry);
    }
    return names;
  }

  void UploadBattle(uint64_t journal_id)
  {
    logging::trace("PROCESS", "combat log", fmt::format("Fetching combat log for battle {}", journal_id));
    const auto reply = RequestGameData("/journals/get", json{{"journal_id", journal_id}}.dump());
    if (reply.empty())
      return;
    const auto battle = json::parse(reply, nullptr, false);
    if (!battle.is_object() || !battle.contains("journal")) {
      spdlog::error("Error parsing journal response from game server for battle {}", journal_id);
      return;
    }

    const auto& journal = battle["journal"];
    Queue(SyncConfig::Type::Battles,
          json::array({{{"type", SyncConfig::Type::Battles}, {"names", ResolveNames(journal)}, {"journal", journal}}}));
  }

  void RunBattleLogs()
  {
#if _WIN32
    winrt::init_apartment();
#endif
    for (;;) {
      const auto journal_id = battles.Pop();
      try {
        UploadBattle(journal_id);
      } catch (const std::exception& e) {
        spdlog::error("Error processing combat log {}: {}", journal_id, e.what());
      } catch (...) {
        spdlog::error("Unknown error processing combat log {}", journal_id);
      }
    }
  }

  void LoadSentBattles()
  {
    std::scoped_lock lock(sent_mutex);
    sent_battles.set_capacity(kSentBattlesKept);

    std::ifstream file(File::Path(File::Battles()), std::ios::binary);
    if (!file) {
      spdlog::warn("Failed to open battles file (not found or not readable); starting with empty cache");
      return;
    }
    const auto ids = json::parse(file, nullptr, false);
    if (!ids.is_array()) {
      spdlog::error("Failed to parse battles file");
      return;
    }
    for (const auto& id : ids)
      if (id.is_number_unsigned())
        sent_battles.push_back(id.get<uint64_t>());
    spdlog::debug("Loaded {} previously sent battle logs", sent_battles.size());
  }

  void SaveSentBattles()
  {
    auto ids = json::array();
    {
      std::scoped_lock lock(sent_mutex);
      for (const auto id : sent_battles)
        ids.push_back(id);
    }
    std::ofstream file(File::Path(File::Battles(), true), std::ios::binary | std::ios::trunc);
    if (!file) {
      spdlog::error("Failed to open battles file for writing");
      return;
    }
    file << ids.dump();
  }
} // namespace

void QueueNewBattles(const std::vector<uint64_t>& battle_ids)
{
  // Oldest first, so the ring buffer drops the oldest when it fills.
  std::vector<uint64_t> fresh;
  {
    std::scoped_lock lock(sent_mutex);
    for (const auto id : battle_ids | std::views::reverse) {
      if (eastl::find(sent_battles.begin(), sent_battles.end(), id) == sent_battles.end()) {
        sent_battles.push_back(id);
        fresh.push_back(id);
      }
    }
  }
  if (fresh.empty())
    return;

  logging::debug("QUEUE", "battle headers", fmt::format("Queuing {} battles for background processing", fresh.size()));
  for (const auto id : fresh)
    battles.Push(id);
  SaveSentBattles();
}

void CachePlayerNames(const std::string& bytes)
{
  Digit::PrimeServer::Models::UserProfilesResponse response;
  if (!response.ParseFromString(bytes)) {
    spdlog::error("Failed to parse user profile");
    return;
  }
  for (const auto& profile : response.userprofiles())
    players.Store(profile.userid(), {profile.name(), profile.allianceid()});
}

void CacheAllianceNames(const std::string& bytes)
{
  Digit::PrimeServer::Models::GetAllianceProfilesResponse response;
  if (!response.ParseFromString(bytes)) {
    spdlog::error("Failed to parse alliance profile");
    return;
  }
  for (const auto& alliance : response.allianceprofiles())
    if (alliance.id() > 0)
      alliances.Store(alliance.id(), {alliance.name(), alliance.tag()});
}

void StartBattleLogs()
{
  LoadSentBattles();
  std::thread(RunBattleLogs).detach();
}
} // namespace data_sync

// Battle logs: the game only sends battle headers, so each new battle's journal is fetched from the game server
// with the participants' player and alliance names resolved, then uploaded.

#include "file.h"
#include "patches/parts/sync/sync.h"
#include "version.h"

#include <Digit.PrimeServer.Models.pb.h>
#include <cpr/cpr.h>
#include <curl/curl.h>
#include <spdlog/fmt/fmt.h>

#include <chrono>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#if _WIN32
#include <rpc.h>
#else
#include <uuid/uuid.h>
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
      const auto       expires_at = Clock::now() + std::chrono::seconds(Config::Get().sync.resolver_cache_ttl);
      std::scoped_lock lock(mutex_);
      if (entries_.size() >= kNameCacheSize) {
        std::erase_if(entries_, [now = Clock::now()](const auto& entry) { return entry.second.expires_at <= now; });
        // Everything still valid (a long TTL and a lot of browsing): start over rather than grow past the bound.
        if (entries_.size() >= kNameCacheSize)
          entries_.clear();
      }
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

  // Shared with the processing thread and never destroyed, see sync.h.
  auto& players   = *new NameCache<std::string, Player>;
  auto& alliances = *new NameCache<int64_t, Alliance>;
  auto& battles   = *new BoundedQueue<uint64_t>{"battle log", kSentBattlesKept};

  // The newest kSentBattlesKept battles sent, oldest first. Loaded before the threads start, then only touched by
  // the processing thread.
  std::deque<uint64_t> sent_battles;

  void RememberSent(uint64_t id)
  {
    sent_battles.push_back(id);
    if (sent_battles.size() > kSentBattlesKept)
      sent_battles.pop_front();
  }

  // The game's Unity build, sent as the game client sends it.
  const std::string kUnityVersion = "6000.0.59f2-deadlock-fix-test";

  // Written by game hooks on the main thread and read by the battle-log thread.
  struct GameSession {
    std::mutex  mutex;
    std::string server_url;
    std::string session_id;
    // Until the game reports its own version.
    std::string version     = "1.000.51336";
    int32_t     instance_id = 0;
  };
  auto& session = *new GameSession;

  std::string NewUuid()
  {
#if _WIN32
    UUID uuid;
    UuidCreate(&uuid);
    unsigned char* text = nullptr;
    UuidToStringA(&uuid, &text);
    std::string result(reinterpret_cast<char*>(text));
    RpcStringFreeA(&text);
    return result;
#else
    uuid_t uuid;
    uuid_generate_random(uuid);
    char text[37];
    uuid_unparse(uuid, text);
    return text;
#endif
  }

  // `base` with its path replaced, through libcurl's URL parser so ports and IDN hosts survive.
  std::string WithPath(const std::string& base, const std::string& path)
  {
    std::string result = base;
    if (auto* handle = curl_url()) {
      char* url = nullptr;
      if (curl_url_set(handle, CURLUPART_URL, base.c_str(), 0) == CURLUE_OK
          && curl_url_set(handle, CURLUPART_PATH, path.c_str(), 0) == CURLUE_OK
          && curl_url_get(handle, CURLUPART_URL, &url, CURLU_PUNYCODE) == CURLUE_OK) {
        result = url;
      }
      curl_free(url);
      curl_url_cleanup(handle);
    }
    return result;
  }

  // The battle-log thread's session with the game server, set up as the game client sets up its own. It is
  // recreated when a settings reload changes the [sync] proxy or TLS check. Battle-log thread only.
  cpr::Session& GameServer()
  {
    struct Session {
      std::unique_ptr<cpr::Session> http;
      std::string                   proxy;
      bool                          verify_ssl = true;
    };
    static auto& session = *new Session; // never destroyed, see sync.h
    const auto&  options = Config::Get().sync.options;
    if (session.http && options.proxy == session.proxy && options.verify_ssl == session.verify_ssl)
      return *session.http;

    session.proxy      = options.proxy;
    session.verify_ssl = options.verify_ssl;
    session.http       = std::make_unique<cpr::Session>();
    auto& http         = *session.http;
    http.SetAcceptEncoding(cpr::AcceptEncoding{});
    http.SetHttpVersion(cpr::HttpVersion{cpr::HttpVersionCode::VERSION_1_1});
    spdlog::debug("scopely session: proxy='{}', verify_ssl={}", options.proxy, options.verify_ssl);
    if (!options.proxy.empty())
      http.SetProxies({{"https", options.proxy}});
    if (!options.verify_ssl)
      http.SetSslOptions(cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true}));
    http.SetUserAgent("UnityPlayer/" + kUnityVersion + " (UnityWebRequest/1.0, libcurl/8.10.1-DEV)");
    http.SetHeader({
        {"Accept", "application/json"},
        {"Content-Type", "application/json"},
        {"X-PRIME-SYNC", "0"},
        {"X-Unity-Version", kUnityVersion},
        {"X-Powered-By", "stfc community mod/" VER_FILE_VERSION_STR},
    });
    return http;
  }

  // POSTs to the game server as the game client would, for data the client did not request itself. Empty on
  // error. Battle-log thread only.
  std::string RequestGameData(const std::string& path, const std::string& body)
  {
    auto& http    = GameServer();
    auto& headers = http.GetHeader();
    {
      std::scoped_lock lock(session.mutex);
      http.SetUrl(WithPath(session.server_url, path));
      headers.insert_or_assign("X-AUTH-SESSION-ID", session.session_id);
      headers.insert_or_assign("X-PRIME-VERSION", session.version);
      headers.insert_or_assign("X-Instance-ID", fmt::format("{:03}", session.instance_id));
    }
    headers.insert_or_assign("X-TRANSACTION-ID", NewUuid());
    http.SetBody(body);

    const auto response = http.Post();
    if (response.status_code == 0) {
      logging::error("DOWNLOAD", path, "Failed to send request: {}", response.error.message);
      return {};
    }
    if (response.status_code >= 400) {
      logging::error("DOWNLOAD", path, "Failed to communicate with server: {}", response.status_line);
      return {};
    }
    const auto type = response.header.find("Content-Type");
    logging::debug("DOWNLOAD", path, "Response: {} ({}), {:.0f} ms elapsed", response.status_line,
                   type == response.header.end() ? "unknown" : type->second, response.elapsed * 1'000);
    return response.text;
  }

  // The players in a battle. Fleet battles list them in deployed_fleets (fleet data with ref_ids is an NPC's);
  // cracked stations have no fleet, so a base raid's defender only shows up in the journal's own ids.
  std::unordered_set<std::string> Participants(const json& journal)
  {
    std::unordered_set<std::string> ids;
    for (const char* side : {"target_fleet_data", "initiator_fleet_data"}) {
      // operator[] on a const json is undefined for a missing key, so lookups go through find() and value(). A
      // side can be null (a cracked station).
      const auto fleet = journal.find(side);
      if (fleet == journal.end() || !fleet->is_object() || !fleet->value("ref_ids", json()).is_null())
        continue;
      const auto deployed_fleets = fleet->find("deployed_fleets");
      if (deployed_fleets == fleet->end() || !deployed_fleets->is_array())
        continue;
      for (const auto& deployed : *deployed_fleets)
        if (const auto uid = deployed.find("uid"); uid != deployed.end() && uid->is_string())
          ids.insert(uid->get<std::string>());
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
    logging::trace("PROCESS", "combat log", "Fetching {} player profiles", ids.size());
    const auto reply =
        json::parse(RequestGameData("/user_profile/profiles", json{{"user_ids", ids}}.dump()), nullptr, false);
    const auto profiles = reply.is_object() ? reply.value("user_profiles", json::object()) : json::object();
    for (const auto& [id, profile] : profiles.items()) {
      try {
        Player player{profile.at("name").get<std::string>(), profile.at("alliance_id").get<int64_t>()};
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
    logging::trace("PROCESS", "combat log", "Fetching {} alliance profiles", ids.size());
    const json request{{"user_current_rank", 0}, {"alliance_id", 0}, {"alliance_ids", ids}};
    const auto reply =
        json::parse(RequestGameData("/alliance/get_alliances_public_info", request.dump()), nullptr, false);
    const auto infos = reply.is_object() ? reply.value("alliances_info", json::object()) : json::object();
    for (const auto& [key, info] : infos.items()) {
      try {
        const auto id = info.at("id").get<int64_t>();
        Alliance   alliance{info.at("name").get<std::string>(), info.at("tag").get<std::string>()};
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
    logging::trace("PROCESS", "combat log", "Fetching combat log for battle {}", journal_id);
    const auto reply = RequestGameData("/journals/get", json{{"journal_id", journal_id}}.dump());
    if (reply.empty())
      return;
    auto battle = json::parse(reply, nullptr, false);
    if (!battle.is_object() || !battle.contains("journal")) {
      spdlog::error("Error parsing journal response from game server for battle {}", journal_id);
      return;
    }

    auto& journal = battle["journal"];
    auto  names   = ResolveNames(journal);
    Queue(SyncConfig::Type::kBattles,
          json::array(
              {{{"type", SyncConfig::Type::kBattles}, {"names", std::move(names)}, {"journal", std::move(journal)}}}));
  }

  void RunBattleLogs()
  {
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
        RememberSent(id.get<uint64_t>());
    spdlog::debug("Loaded {} previously sent battle logs", sent_battles.size());
  }

  void SaveSentBattles()
  {
    auto ids = json::array();
    for (const auto id : sent_battles)
      ids.push_back(id);
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
  // Oldest first, so the oldest are forgotten first.
  std::vector<uint64_t> fresh;
  for (const auto id : battle_ids | std::views::reverse) {
    if (std::ranges::find(sent_battles, id) == sent_battles.end()) {
      RememberSent(id);
      fresh.push_back(id);
    }
  }
  if (fresh.empty())
    return;

  logging::debug("QUEUE", "battle headers", "Queuing {} battles for background processing", fresh.size());
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

void SetGameServer(std::string url, std::string session_id)
{
  std::scoped_lock lock(session.mutex);
  session.server_url = std::move(url);
  session.session_id = std::move(session_id);
}

void SetGameVersion(std::string version)
{
  std::scoped_lock lock(session.mutex);
  session.version = std::move(version);
}

void SetInstanceId(int32_t instance_id)
{
  std::scoped_lock lock(session.mutex);
  session.instance_id = instance_id;
}
} // namespace data_sync

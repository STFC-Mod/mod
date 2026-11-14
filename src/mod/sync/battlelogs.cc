// Battle logs: the game only sends battle headers, so each new battle's journal is fetched from the game server
// with the participants' player and alliance names resolved, then uploaded.

#include "sync/sync.h"
#include "version.h"

#include <Digit.PrimeServer.Models.pb.h>
#include <cpr/cpr.h>
#include <curl/curl.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
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
  struct Player {
    std::string name;
    int64_t     alliance = -1;
  };

  struct Alliance {
    std::string name;
    std::string tag;
  };

  // Shared with the processing thread. Leaked so the detached battle-log thread never sees them destroyed at exit.
  auto& players   = *new NameCache<std::string, Player>;
  auto& alliances = *new NameCache<int64_t, Alliance>;

  // A journal the game server would not hand over is tried this many times, waiting 1 s and doubling up to a minute:
  // about four minutes in all, enough to ride out a server hiccup without retrying a lost journal all session.
  constexpr int kJournalAttempts   = 10;
  constexpr int kJournalMaxBackoff = 60;

  // A battle to fetch, with the session that reported it: its journal is fetched with that session's credentials
  // or not at all.
  struct Battle {
    uint64_t          id;
    GameSession       session;
    int               attempts = 0;
    Clock::time_point retry_at{};
  };
  // Only the recent journals are kept for targets, so a longer backlog would be fetched for nothing.
  auto& battles = *new BoundedQueue<Battle>{"battle log", kRecentBattles};
  // Guards `pending` and keeps it in step with `battles`. Taken before the state mutex (HasBattle), never after.
  auto& battle_mutex = *new std::mutex;
  // {generation, id} of battles queued or being fetched, so each journal is fetched once. Entries leave when the
  // run loop finishes with a battle or the queue drops it. Delivery to targets is tracked by the record store.
  auto& pending = *new std::set<std::pair<uint64_t, uint64_t>>;

  // The game client's UnityWebRequest identity, which the game server expects. Update with each client Unity bump.
  const std::string kUnityVersion = "6000.0.59f2-deadlock-fix-test";
  const std::string kClientCurl   = "libcurl/8.10.1-DEV";

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

  // The battle-log thread's HTTP session with the game server, set up as the game client sets up its own. It is
  // recreated when a settings reload changes the [sync] proxy or TLS check. Battle-log thread only.
  cpr::Session& GameServerHttp()
  {
    struct Cached {
      std::unique_ptr<cpr::Session> http;
      std::string                   proxy;
      bool                          verify_ssl = true;
    };
    static auto& cached  = *new Cached; // Leaked, like the thread that uses it.
    const auto   config  = Config::Sync::Get();
    const auto&  options = config->options;
    if (cached.http && options.proxy == cached.proxy && options.verify_ssl == cached.verify_ssl)
      return *cached.http;

    cached.proxy      = options.proxy;
    cached.verify_ssl = options.verify_ssl;
    cached.http       = std::make_unique<cpr::Session>();
    auto& http        = *cached.http;
#ifndef _MODDBG
    http.SetConnectTimeout(cpr::ConnectTimeout{3'000});
    http.SetTimeout(cpr::Timeout{10'000});
#endif
    http.SetAcceptEncoding(cpr::AcceptEncoding{});
    http.SetHttpVersion(cpr::HttpVersion{cpr::HttpVersionCode::VERSION_1_1});
    logging::Debug("download", "game server", "new session, proxy '{}', verify_ssl {}", options.proxy,
                   options.verify_ssl);
    if (!options.proxy.empty())
      http.SetProxies({{"https", options.proxy}});
    if (!options.verify_ssl)
      http.SetSslOptions(cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true}));
    http.SetUserAgent("UnityPlayer/" + kUnityVersion + " (UnityWebRequest/1.0, " + kClientCurl + ")");
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
  // error, or when `session` has ended before or during the request. Battle-log thread only.
  std::string RequestGameData(const GameSession& session, const std::string& path, const std::string& body)
  {
    // The URL and credentials are the captured session's; the version and instance headers are the latest, since
    // the game reports them after login.
    const auto current = Session();
    if (session.generation != current.generation)
      return {};
    auto& http    = GameServerHttp();
    auto& headers = http.GetHeader();
    http.SetUrl(WithPath(session.server_url, path));
    headers.insert_or_assign("X-AUTH-SESSION-ID", session.session_id);
    headers.insert_or_assign("X-PRIME-VERSION", current.version);
    headers.insert_or_assign("X-Instance-ID", fmt::format("{:03}", current.instance_id));
    headers.insert_or_assign("X-TRANSACTION-ID", NewUuid());
    http.SetBody(body);

    const auto response = http.Post();
    if (session.generation != SessionGeneration())
      return {};
    if (response.status_code == 0) {
      logging::Error("download", path, "the request was not sent: {}", response.error.message);
      return {};
    }
    if (response.status_code < 200 || response.status_code >= 300) {
      logging::Error("download", path, "the server answered {}", response.status_line);
      return {};
    }
    const auto type = response.header.find("Content-Type");
    logging::Debug("download", path, "{} ({}) in {:.0f} ms", response.status_line,
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
    // NPC ids contain '_' (mar_61, npc_<id>_1); there is nothing to resolve for them.
    for (const char* key : {"initiator_id", "target_id"})
      if (const auto id = journal.value(key, json()); id.is_string())
        if (const auto& text = id.get_ref<const std::string&>(); !text.empty() && !text.contains('_'))
          ids.insert(text);
    return ids;
  }

  // Fetches profiles the cache does not have. A malformed profile is skipped, not the whole reply. The results are
  // returned as well as cached, since a zero TTL expires them at once.
  std::unordered_map<std::string, Player> FetchPlayers(const GameSession& session, const std::vector<std::string>& ids)
  {
    std::unordered_map<std::string, Player> fetched;
    if (ids.empty())
      return fetched;
    logging::Trace("download", "battle log", "fetching {} player profiles", ids.size());
    const auto reply =
        json::parse(RequestGameData(session, "/user_profile/profiles", json{{"user_ids", ids}}.dump()), nullptr, false);
    const auto profiles = reply.is_object() ? reply.value("user_profiles", json::object()) : json::object();
    for (const auto& [id, profile] : profiles.items()) {
      try {
        Player player{.name     = profile.at("name").get<std::string>(),
                      .alliance = profile.at("alliance_id").get<int64_t>()};
        players.Store(id, player, session.generation);
        fetched.emplace(id, std::move(player));
      } catch (const json::exception& e) {
        logging::Error("process", "player profile", "skipping {}: {}", id, e.what());
      }
    }
    return fetched;
  }

  std::unordered_map<int64_t, Alliance> FetchAlliances(const GameSession& session, const std::vector<int64_t>& ids)
  {
    std::unordered_map<int64_t, Alliance> fetched;
    if (ids.empty())
      return fetched;
    logging::Trace("download", "battle log", "fetching {} alliance profiles", ids.size());
    const json request{{"user_current_rank", 0}, {"alliance_id", 0}, {"alliance_ids", ids}};
    const auto reply =
        json::parse(RequestGameData(session, "/alliance/get_alliances_public_info", request.dump()), nullptr, false);
    const auto infos = reply.is_object() ? reply.value("alliances_info", json::object()) : json::object();
    for (const auto& [key, info] : infos.items()) {
      try {
        const auto id = info.at("id").get<int64_t>();
        Alliance   alliance{.name = info.at("name").get<std::string>(), .tag = info.at("tag").get<std::string>()};
        alliances.Store(id, alliance, session.generation);
        fetched.emplace(id, std::move(alliance));
      } catch (const json::exception& e) {
        logging::Error("process", "alliance profile", "skipping one: {}", e.what());
      }
    }
    return fetched;
  }

  // {player id: {name, alliance_name, alliance_tag}} for everyone in the battle, fetching what the caches lack.
  // A player whose alliance could not be resolved keeps alliance_id instead of a name and tag. Players without an
  // alliance (id 0 or below) are not looked up.
  json ResolveNames(const GameSession& session, const json& journal)
  {
    std::unordered_map<std::string, Player> known;
    std::vector<std::string>                missing_players;
    for (const auto& id : Participants(journal)) {
      if (auto player = players.Find(id, session.generation))
        known.emplace(id, std::move(*player));
      else
        missing_players.push_back(id);
    }
    known.merge(FetchPlayers(session, missing_players));

    std::unordered_map<int64_t, Alliance> known_alliances;
    std::unordered_set<int64_t>           missing_alliances;
    for (const auto& player : known | std::views::values) {
      if (player.alliance <= 0 || known_alliances.contains(player.alliance))
        continue;
      if (auto alliance = alliances.Find(player.alliance, session.generation))
        known_alliances.emplace(player.alliance, std::move(*alliance));
      else
        missing_alliances.insert(player.alliance);
    }
    known_alliances.merge(FetchAlliances(session, {missing_alliances.begin(), missing_alliances.end()}));

    auto names = json::object();
    for (const auto& [id, player] : known) {
      json entry{{"name", player.name}, {"alliance_name", nullptr}, {"alliance_tag", nullptr}};
      if (const auto alliance = known_alliances.find(player.alliance); alliance != known_alliances.end()) {
        entry["alliance_name"] = alliance->second.name;
        entry["alliance_tag"]  = alliance->second.tag;
      } else {
        entry["alliance_id"] = player.alliance;
      }
      names[id] = std::move(entry);
    }
    return names;
  }

  // Fetches one journal and publishes it with its names. False when the game server did not hand it over.
  bool FetchBattle(const Battle& battle)
  {
    const auto& session = battle.session;
    logging::Trace("download", "battle log", "fetching the journal of battle {}", battle.id);
    const auto text = RequestGameData(session, "/journals/get", json{{"journal_id", battle.id}}.dump());
    if (text.empty())
      return false;
    auto reply = json::parse(text, nullptr, false);
    if (!reply.is_object() || !reply.contains("journal")) {
      logging::Error("process", "battle log", "the journal of battle {} does not parse", battle.id);
      return false;
    }
    auto& journal = reply["journal"];
    auto  names   = ResolveNames(session, journal);
    Publish(Stream::kBattles, {{battle.id, {{"names", std::move(names)}, {"journal", std::move(journal)}}}},
            session.generation);
    return true;
  }
} // namespace

void RunBattleLogs()
{
  for (;;) {
    auto battle = battles.Pop();
    // A battle from an older session, or one queued before battle logs were switched off, is only forgotten.
    if (battle.session.generation == SessionGeneration() && Config::Sync::Get()->options.battlelogs) {
      // A retried journal goes to the back of the queue, so everything queued before it has been fetched; only
      // what arrived later waits for it.
      if (const auto now = Clock::now(); battle.retry_at > now)
        std::this_thread::sleep_for(battle.retry_at - now);
      bool fetched = false;
      try {
        fetched = FetchBattle(battle);
      } catch (const std::exception& e) {
        logging::Error("process", "battle log", "battle {} failed: {}", battle.id, e.what());
      }
      if (!fetched) {
        if (++battle.attempts >= kJournalAttempts) {
          logging::Error("download", "battle log", "giving up on battle {} after {} attempts", battle.id,
                         kJournalAttempts);
        } else if (battle.session.generation == SessionGeneration()) {
          battle.retry_at =
              Clock::now() + std::chrono::seconds(std::min(1 << (battle.attempts - 1), kJournalMaxBackoff));
          // Under the lock, like QueueNewBattles, so a battle the full queue drops leaves `pending` in the same step.
          std::scoped_lock lock(battle_mutex);
          if (auto dropped = battles.Push(battle))
            pending.erase({dropped->session.generation, dropped->id});
          continue;
        }
      }
    }
    std::scoped_lock lock(battle_mutex);
    pending.erase({battle.session.generation, battle.id});
  }
}

void QueueNewBattles(const std::vector<uint64_t>& ids, uint64_t generation)
{
  const auto session = Session();
  if (session.generation != generation)
    return;
  std::scoped_lock lock(battle_mutex);
  // Older sessions' entries can never match again; the run loop drops their battles without fetching.
  std::erase_if(pending, [&](const auto& entry) { return entry.first != generation; });
  for (auto id : ids | std::views::reverse) {
    if (HasBattle(id, generation) || !pending.emplace(generation, id).second)
      continue;
    if (auto dropped = battles.Push({id, session}))
      pending.erase({dropped->session.generation, dropped->id});
  }
}

void CachePlayerNames(const Input& input)
{
  const auto response = Parse<Digit::PrimeServer::Models::UserProfilesResponse>(input, "player profiles");
  if (!response)
    return;
  for (const auto& profile : response->userprofiles())
    players.Store(profile.userid(), {profile.name(), profile.allianceid()}, input.generation);
}

void CacheAllianceNames(const Input& input)
{
  const auto response = Parse<Digit::PrimeServer::Models::GetAllianceProfilesResponse>(input, "alliance profiles");
  if (!response)
    return;
  for (const auto& alliance : response->allianceprofiles())
    if (alliance.id() > 0)
      alliances.Store(alliance.id(), {alliance.name(), alliance.tag()}, input.generation);
}
} // namespace data_sync

#include "config.h"
#include "errormsg.h"
#include "file.h"
#include "patches/fleet_watch.h"
#include "patches/screen_update_hook.h"
#include "str_utils.h"

#include <Digit.Platform.Models.pb.h>
#include <Digit.PrimePlatform.Content.pb.h>
#include <Digit.PrimeServer.Models.pb.h>
#include <stfc.sync.pb.h>
#include <il2cpp-api-types.h>
#include <il2cpp/il2cpp_helper.h>
#include <prime/EntityGroup.h>
#include <prime/FleetsManager.h>
#include <prime/RealtimeDataPayload.h>
#include <prime/ServiceResponse.h>
#include <prime/UserProfileManager.h>
#include <spud/detour.h>

#include <spdlog/spdlog.h>
#if !__cpp_lib_format
#include <spdlog/fmt/fmt.h>
#endif

#if _WIN32
#include <rpc.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.Headers.h>
#else
#include <uuid/uuid.h>
#endif

#include <EASTL/algorithm.h>
#include <EASTL/bonus/ring_buffer.h>
#include <cpr/cpr.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef STR_FORMAT
#if __cpp_lib_format
#define STR_FORMAT std::format
#else
#define STR_FORMAT fmt::format
#endif
#endif

#ifndef _WIN32
#include <time.h>
#endif

#if _WIN32
// Introduce RAII guard for COM apartments
struct WinRtApartmentGuard {
  WinRtApartmentGuard() { winrt::init_apartment(); }
  ~WinRtApartmentGuard() { winrt::uninit_apartment(); }
};
#endif

NLOHMANN_JSON_NAMESPACE_BEGIN
template <typename T> struct adl_serializer<google::protobuf::RepeatedField<T>> {
  static void to_json(json& j, const google::protobuf::RepeatedField<T>& proto)
  {
    j = json::array();

    for (const auto& v : proto) {
      j.push_back(v);
    }
  }

  static void from_json(const json& j, google::protobuf::RepeatedField<T>& proto)
  {
    if (j.is_array()) {
      for (const auto& v : j) {
        proto.Add(v.get<T>());
      }
    }
  }
};

template <> struct adl_serializer<SyncConfig::Type> {
  static void to_json(json& j, const SyncConfig::Type t)
  {
    for (const auto& opt : SyncOptions) {
      if (opt.type == t) {
        j = opt.type_str;
        return;
      }
    }

    j = nullptr;
  }

  static void from_json(const json& j, SyncConfig::Type& t)
  {
    if (j.is_string()) {
      const auto& s = j.get_ref<const std::string&>();

      for (const auto& opt : SyncOptions) {
        if (opt.type_str == s) {
          t = opt.type;
          return;
        }
      }
    }
  }
};
NLOHMANN_JSON_NAMESPACE_END

namespace http
{

namespace headers
{
  static std::string    gameServerUrl;
  static std::string    instanceSessionId;
  static int32_t        instanceId;
  static std::string    unityVersion{"6000.0.59f2-deadlock-fix-test"};
  static std::string    primeVersion{"1.000.51336"};
  static std::string    poweredBy{"stfc community mod/" VER_FILE_VERSION_STR};
} // namespace headers

namespace logging
{
  static constexpr std::string CURL_TYPE_UPLOAD   = "UPLOAD";
  static constexpr std::string CURL_TYPE_DOWNLOAD = "DOWNLOAD";

  static void error(const std::string& type, const std::string& target, const std::string& text)
  {
    if (Config::Get().sync_logging) {
      spdlog::error("SYNC-{} - {}: {}", type, target, text);
    }
  }

  static void warn(const std::string& type, const std::string& target, const std::string& text)
  {
    if (Config::Get().sync_logging) {
      spdlog::warn("SYNC-{} - {}: {}", type, target, text);
    }
  }

  static void info(const std::string& type, const std::string& target, const std::string& text)
  {
    if (Config::Get().sync_logging) {
      spdlog::info("SYNC-{} - {}: {}", type, target, text);
    }
  }

  static void debug(const std::string& type, const std::string& target, const std::string& text)
  {
    if (Config::Get().sync_logging && Config::Get().sync_debug) {
      spdlog::debug("SYNC-{} - {}: {}", type, target, text);
    }
  }

  static void trace(const std::string& type, const std::string& target, const std::string& text)
  {
    if (Config::Get().sync_logging && Config::Get().sync_debug) {
      spdlog::trace("SYNC-{} - {}: {}", type, target, text);
    }
  }
}

[[nodiscard]] static std::string newUUID()
{
#ifdef _WIN32
  UUID uuid;
  UuidCreate(&uuid);

  unsigned char* str;
  UuidToStringA(&uuid, &str);

  std::string s(reinterpret_cast<char*>(str));

  RpcStringFreeA(&str);
#else
  uuid_t uuid;
  uuid_generate_random(uuid);
  char s[37];
  uuid_unparse(uuid, s);
#endif
  return s;
}

// Simple URL manipulation class to replace boost::url
class Url
{
public:
  explicit Url(std::string url) : m_url(std::move(url))
  {
    m_handle = curl_url();
    if (m_handle != nullptr) {
      curl_url_set(m_handle, CURLUPART_URL, m_url.data(), 0);
    }
  }

  ~Url()
  {
    if (m_handle != nullptr) {
      curl_url_cleanup(m_handle);
    }
  }

  // Delete copy operations to prevent double-free
  Url(const Url&) = delete;
  Url& operator=(const Url&) = delete;

  Url(Url&& other) noexcept : m_handle(other.m_handle), m_url(std::move(other.m_url))
  {
    other.m_handle = nullptr;
  }

  Url& operator=(Url&& other) noexcept
  {
    if (this != &other) {
      if (m_handle != nullptr) {
        curl_url_cleanup(m_handle);
      }

      m_handle = other.m_handle;
      m_url = std::move(other.m_url);
      other.m_handle = nullptr;
    }

    return *this;
  }

  void set_path(const std::string& path)
  {
    if (m_handle == nullptr) {
      return;
    }

    if (CURLUcode rc = curl_url_set(m_handle, CURLUPART_PATH, path.c_str(), 0); rc == CURLUE_OK) {
      char *url = nullptr;
      if (rc = curl_url_get(m_handle, CURLUPART_URL , &url, CURLU_PUNYCODE); rc == CURLUE_OK) {
        m_url = url;
      }

      if (url != nullptr) {
        curl_free(url);
      }
    }
  }

  [[nodiscard]] const char* c_str() const
  {
    return m_url.c_str();
  }

private:
  CURLU *m_handle;
  std::string m_url;
};

// Per-target worker thread infrastructure
struct TargetWorker {
  TargetWorker() = default;
  TargetWorker(const TargetWorker&) = delete;
  TargetWorker& operator=(const TargetWorker&) = delete;

  using request_t = std::tuple<std::string, std::string, bool>;

  std::shared_ptr<cpr::Session> session;
  std::thread                   worker_thread;
  std::atomic_bool              stop_requested{false};
  std::queue<request_t>         request_queue;
  std::mutex                    queue_mtx;
  std::condition_variable       queue_cv;
};

static std::unordered_map<std::string, std::shared_ptr<TargetWorker>> target_workers;
static std::mutex target_workers_mtx;

static void target_worker_thread(std::shared_ptr<TargetWorker> worker)
{
#if _WIN32
  WinRtApartmentGuard apartmentGuard;
#endif

  while (!worker->stop_requested.load(std::memory_order_acquire)) {
    std::string identifier;
    std::string post_data;
    bool is_first_sync = false;

    {
      std::unique_lock lk(worker->queue_mtx);
      worker->queue_cv.wait(lk, [&worker] {
        return worker->stop_requested.load(std::memory_order_acquire) || !worker->request_queue.empty();
      });

      if (worker->stop_requested.load(std::memory_order_acquire) && worker->request_queue.empty()) {
        break;
      }

      if (!worker->request_queue.empty()) {
        auto item = std::move(worker->request_queue.front());
        worker->request_queue.pop();
        identifier = std::move(std::get<0>(item));
        post_data = std::move(std::get<1>(item));
        is_first_sync = std::get<2>(item);
      }
    }

    if (post_data.empty()) {
      continue;
    }

    // Process the request
    try {
      const auto httpClient = worker->session;
      auto& headers = httpClient->GetHeader();

      if (is_first_sync) {
        headers.insert_or_assign("X-PRIME-SYNC", "2");
        logging::trace(logging::CURL_TYPE_UPLOAD, identifier, "Adding X-Prime-Sync header for initial sync");
      } else {
        headers.erase("X-PRIME-SYNC");
      }

      httpClient->SetBody(cpr::Body{post_data});

      logging::debug(logging::CURL_TYPE_UPLOAD, identifier, "Sending data to " + httpClient->GetFullRequestUrl());

      // Synchronously wait for response
      const auto response = httpClient->Post();

      if (response.status_code == 0) {
        logging::error(logging::CURL_TYPE_UPLOAD, identifier, "Failed to send request: " + response.error.message);
      } else if (response.status_code >= 400) {
        if (response.status_code == 501) {
          logging::error(logging::CURL_TYPE_UPLOAD, identifier, STR_FORMAT("Sync target does not support this operation ({}, after {:.0f} ms). Consider turning off this upload category for the target.", response.status_line, response.elapsed * 1'000));
        } else {
          logging::error(logging::CURL_TYPE_UPLOAD, identifier, STR_FORMAT("Failed to communicate with server: {} (after {:.0f} ms)", response.status_line, response.elapsed * 1'000));
        }
      } else {
        logging::debug(logging::CURL_TYPE_UPLOAD, identifier, STR_FORMAT("Response: {} ({:.0f} ms elapsed)", response.status_line, response.elapsed * 1'000));
      }
    } catch (const std::runtime_error& e) {
      ErrorMsg::SyncRuntime(identifier.c_str(), e);
    } catch (const std::exception& e) {
      ErrorMsg::SyncException(identifier.c_str(), e);
#if _WIN32
    } catch (winrt::hresult_error const& ex) {
      ErrorMsg::SyncWinRT(identifier.c_str(), ex);
#endif
    } catch (...) {
      ErrorMsg::SyncMsg(identifier.c_str(), "Unknown error occurred");
    }
  }
}

static std::shared_ptr<TargetWorker> get_curl_client_sync(const std::string& target)
{
  std::scoped_lock lk(target_workers_mtx);

  // Check if a worker already exists
  if (const auto it = target_workers.find(target); it != target_workers.end()) {
    return it->second;
  }

  // Create a new worker for this target
  auto worker = std::make_shared<TargetWorker>();

  // Initialize session
  worker->session = std::make_shared<cpr::Session>();
  const auto& target_config = Config::Get().sync_targets[target];

  worker->session->SetUrl(target_config.url);
  worker->session->SetUserAgent("stfc community mod " VER_FILE_VERSION_STR " (libcurl/" LIBCURL_VERSION ")");
  worker->session->SetAcceptEncoding(cpr::AcceptEncoding{});
  worker->session->SetHttpVersion(cpr::HttpVersion{cpr::HttpVersionCode::VERSION_1_1});
  worker->session->SetRedirect(cpr::Redirect{3, true, false, cpr::PostRedirectFlags::POST_ALL});

#ifndef _MODDBG
  worker->session->SetConnectTimeout(cpr::ConnectTimeout{3'000});
  worker->session->SetTimeout(cpr::Timeout{10'000});
#endif

  spdlog::debug("sync target '{}': proxy='{}', verify_ssl={}", target, target_config.proxy, target_config.verify_ssl);

  if (!target_config.proxy.empty()) {
    worker->session->SetProxies({{"http", target_config.proxy}, {"https", target_config.proxy}});
  }

  if (!target_config.verify_ssl) {
    worker->session->SetSslOptions(
      cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true})
    );
  }

  worker->session->SetHeader({
    {"Content-Type", "application/json"},
    {"X-Powered-By", headers::poweredBy},
    {"stfc-sync-token", target_config.token},
  });

  // Start the worker thread
  worker->worker_thread = std::thread(target_worker_thread, worker);
  target_workers[target] = worker;

  return worker;
}

static void send_data(SyncConfig::Type type, const std::string& post_data, bool is_first_sync)
{
  static std::once_flag emit_warning;
  const auto& targets = Config::Get().sync_targets;

  std::call_once(emit_warning, [targets] {
    if (targets.empty()) {
      logging::warn(logging::CURL_TYPE_UPLOAD, "GLOBAL", "No target found, will not attempt to send");
    }
  });

  for (const auto& target : targets
       | std::views::filter([type](const auto& t) { return t.second.enabled(type); })
       | std::views::keys) {

    const auto target_identifier = STR_FORMAT("{} ({})", target, to_string(type));

    try {
      const auto worker = get_curl_client_sync(target);

      // Enqueue the request for this target's worker
      {
        std::scoped_lock lk(worker->queue_mtx);
        worker->request_queue.emplace(target_identifier, post_data, is_first_sync);
        logging::trace(logging::CURL_TYPE_UPLOAD, target_identifier,
                       STR_FORMAT("Queued request (queue size: {})", worker->request_queue.size()));
      }
      worker->queue_cv.notify_all();

    } catch (const std::runtime_error& e) {
      spdlog::error("Failed to send sync data to target '{}' - Runtime error: {}", target_identifier, e.what());
    } catch (const std::exception& e) {
      spdlog::error("Failed to send sync data to target '{}' - Exception: {}", target_identifier, e.what());
    } catch (...) {
      spdlog::error("Failed to send sync data to target '{}' - Unknown error occurred", target_identifier);
    }
  }
}

static std::shared_ptr<cpr::Session> get_curl_client_scopely()
{
  static std::shared_ptr<cpr::Session> session{nullptr};
  static std::once_flag init_flag;

  // thread-safe initialization
  std::call_once(init_flag, [] {
    session = std::make_shared<cpr::Session>();
    session->SetAcceptEncoding(cpr::AcceptEncoding{});
    session->SetHttpVersion(cpr::HttpVersion{cpr::HttpVersionCode::VERSION_1_1});

    spdlog::debug("scopely session: proxy='{}', verify_ssl={}", Config::Get().sync_options.proxy, Config::Get().sync_options.verify_ssl);

    if (!Config::Get().sync_options.proxy.empty()) {
      session->SetProxies({{"https", Config::Get().sync_options.proxy}});
    }

    if (!Config::Get().sync_options.verify_ssl) {
      session->SetSslOptions(
        cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true})
      );
    }

    session->SetUserAgent("UnityPlayer/" + headers::unityVersion + " (UnityWebRequest/1.0, libcurl/8.10.1-DEV)");
    session->SetHeader({
        {"Accept", "application/json"},
        {"Content-Type", "application/json"},
        {"X-TRANSACTION-ID", newUUID()},
        {"X-AUTH-SESSION-ID", headers::instanceSessionId},
        {"X-PRIME-VERSION", headers::primeVersion},
        {"X-Instance-ID", STR_FORMAT("{:03}", headers::instanceId)},
        {"X-PRIME-SYNC", "0"},
        {"X-Unity-Version", headers::unityVersion},
        {"X-Powered-By", headers::poweredBy},
    });
  });

  return session;
}

static std::string get_scopely_data(const std::string& path, const std::string& post_data)
{
  static std::once_flag emit_warning;

  if (Config::Get().sync_targets.empty()) {
    std::call_once(emit_warning, [] {
      logging::warn(logging::CURL_TYPE_UPLOAD, "GLOBAL", "No target found, will not attempt to retrieve data");
    });

    return {};
  }

  Url url(headers::gameServerUrl);
  url.set_path(path);

  const auto        httpClient = get_curl_client_scopely();
  static std::mutex client_mutex;

  std::string response_text;

  {
    std::scoped_lock lk(client_mutex);
    httpClient->SetUrl(url.c_str());

    auto& headers = httpClient->GetHeader();
    headers.insert_or_assign("X-TRANSACTION-ID", newUUID());
    headers.insert_or_assign("X-AUTH-SESSION-ID", headers::instanceSessionId);
    headers.insert_or_assign("X-Instance-ID", STR_FORMAT("{:03}", headers::instanceId));

    httpClient->SetBody(post_data);
    const auto response = httpClient->Post();

    if (response.status_code == 0) {
      logging::error(logging::CURL_TYPE_DOWNLOAD, path, "Failed to send request: " + response.error.message);
      return {};
    }

    if (response.status_code >= 400) {
      logging::error(logging::CURL_TYPE_DOWNLOAD, path, "Failed to communicate with server: " + response.status_line);
      return {};
    }

    const auto  response_headers = response.header;
    std::string type;

    try {
      type = response_headers.at("Content-Type");
    } catch (const std::out_of_range&) {
      type = "unknown";
    }

    logging::debug(logging::CURL_TYPE_DOWNLOAD, path,
                   STR_FORMAT("Response: {} ({}), {:.0f} ms elapsed,", response.status_line, type, response.elapsed * 1'000));
    response_text = response.text;
  }

  return response_text;
}

} // namespace http

namespace trackers
{

namespace types
{
  struct RankLevelState {
    explicit RankLevelState(const int32_t r = -1, const int32_t l = -1)
        : rank(r)
        , level(l)
    {
    }

    bool operator==(const RankLevelState& other) const
    { return this->rank == other.rank && this->level == other.level; }

  private:
    int64_t rank  = -1;
    int64_t level = -1;
  };

  struct RankLevelShardsState {
    explicit RankLevelShardsState(const int32_t r = -1, const int32_t l = -1, const int32_t s = -1)
        : rank(r)
        , level(l)
        , shards(s)
    {
    }

    bool operator==(const RankLevelShardsState& other) const
    { return this->rank == other.rank && this->level == other.level && this->shards == other.shards; }

  private:
    int32_t rank   = -1;
    int32_t level  = -1;
    int32_t shards = -1;
  };

  struct ShipState {
    explicit ShipState(const int32_t t = -1, const int32_t l = -1, const double_t lp = -1.0,
                       const std::vector<int64_t>& c = {})
        : tier(t)
        , level(l)
        , level_percentage(lp)
        , components(c)
    {
    }

    bool operator==(const ShipState& other) const
    {
      return this->tier == other.tier && this->level == other.level
             && std::fabs(this->level_percentage - other.level_percentage) < 0.01
             && this->components == other.components;
    }

  private:
    int32_t              tier             = -1;
    int32_t              level            = -1;
    double_t             level_percentage = -1.0;
    std::vector<int64_t> components;
  };

  struct PlanetaryBaseState {
    explicit PlanetaryBaseState(const int32_t l = -1, const int32_t s = -1, const int64_t p = -1)
        : level(l)
        , status(s)
        , position(p)
    {
    }

    bool operator==(const PlanetaryBaseState& other) const
    { return this->level == other.level && this->status == other.status && this->position == other.position; }

  private:
    int32_t level    = -1;
    int32_t status   = -1;
    int64_t position = -1;
  };

  struct pairhash {
    template <typename T, typename U> std::size_t operator()(const std::pair<T, U>& x) const
    { return std::hash<T>()(x.first) ^ std::hash<U>()(x.second); }
  };

  struct CachedPlayerData {
    std::string                           name;
    int64_t                               alliance{-1};
    std::chrono::steady_clock::time_point expires_at;
  };

  struct CachedAllianceData {
    std::string                           name;
    std::string                           tag;
    std::chrono::steady_clock::time_point expires_at;
  };
} // namespace types

namespace names
{
  using namespace types;

  static std::unordered_map<std::string, CachedPlayerData> player_data_cache;
  static std::mutex                                        player_data_cache_mtx;

  static std::unordered_map<int64_t, CachedAllianceData> alliance_data_cache;
  static std::mutex                                      alliance_data_cache_mtx;

  static void cache_player_names(std::unique_ptr<std::string>&& bytes)
  {
    if (auto response = Digit::PrimeServer::Models::UserProfilesResponse(); response.ParseFromString(*bytes)) {

      std::unordered_map<std::string, CachedPlayerData> names;
      const auto                                        expires_at =
          std::chrono::steady_clock::now() + std::chrono::seconds(Config::Get().sync_resolver_cache_ttl);

      for (const auto& profile : response.userprofiles()) {
        names.insert_or_assign(
            profile.userid(),
            CachedPlayerData{.name = profile.name(), .alliance = profile.allianceid(), .expires_at = expires_at});
      }

      {
        std::scoped_lock lk(player_data_cache_mtx);
        player_data_cache.insert(names.begin(), names.end());
      }
    } else {
      spdlog::error("Failed to parse user profile");
    }
  }

  static void cache_alliance_names(std::unique_ptr<std::string>&& bytes)
  {
    if (auto response = Digit::PrimeServer::Models::GetAllianceProfilesResponse(); response.ParseFromString(*bytes)) {

      std::unordered_map<int64_t, CachedAllianceData> names;
      const auto                                      expires_at =
          std::chrono::steady_clock::now() + std::chrono::seconds(Config::Get().sync_resolver_cache_ttl);

      for (const auto& alliance : response.allianceprofiles()) {
        if (alliance.id() > 0) {
          names.insert_or_assign(
              alliance.id(),
              CachedAllianceData{.name = alliance.name(), .tag = alliance.tag(), .expires_at = expires_at});
        }
      }

      {
        std::scoped_lock lk(alliance_data_cache_mtx);
        alliance_data_cache.insert(names.begin(), names.end());
      }

    } else {
      spdlog::error("Failed to parse alliance profile");
    }
  }

  static inline void collect_user_ids_from_fleet(const nlohmann::json&            fleet_data,
                                                 std::unordered_set<std::string>& user_ids)
  {
    if (!fleet_data.contains("ref_ids") || fleet_data["ref_ids"].is_null()) {
      for (const auto& fleet : fleet_data["deployed_fleets"]) {
        const auto& player_id = fleet["uid"].get<std::string>();
        user_ids.insert(player_id);
      }
    }
  }

  // Cracked stations have no fleet, so a base raid's defender isn't in deployed_fleets -- take the
  // participant ids from the journal instead.
  static inline void collect_journal_participants(const nlohmann::json&            journal,
                                                  std::unordered_set<std::string>& user_ids)
  {
    for (const char* key : {"initiator_id", "target_id"}) {
      if (!journal.contains(key) || journal[key].is_null()) {
        continue;
      }
      const auto& id = journal[key].get<std::string>();
      // NPC ids contain '_' (mar_61, npc_<id>_1); player ids don't -- skip, nothing to resolve.
      if (!id.empty() && id.find('_') == std::string::npos) {
        user_ids.insert(id);
      }
    }
  }

  static inline void collect_alliance_ids(const nlohmann::json& names, std::unordered_set<int64_t>& alliance_ids)
  {
    for (const auto& [player_id, entry] : names.items()) {
      try {
        const auto alliance_id = entry["alliance_id"].get<int64_t>();
        alliance_ids.insert(alliance_id);
      } catch (const nlohmann::json::exception&) {
      }
    }
  }

  static void resolve_player_names(const std::unordered_set<std::string>& user_ids, nlohmann::json& out_names,
                                   nlohmann::json&                                          out_request_ids,
                                   const std::chrono::time_point<std::chrono::steady_clock> now)
  {
    std::scoped_lock lk(player_data_cache_mtx);

    for (const auto& user_id : user_ids) {
      const auto it = player_data_cache.find(user_id);
      if (it != player_data_cache.end()) {
        if (it->second.expires_at > now) {
          out_names[user_id] = {{"name", it->second.name},
                                {"alliance_id", it->second.alliance},
                                {"alliance_name", nullptr},
                                {"alliance_tag", nullptr}};
        } else {
          // expired entry: erase and queue for fetch
          player_data_cache.erase(it);
          out_request_ids.push_back(user_id);
        }
      } else {
        // cache miss: queue for fetch
        out_request_ids.push_back(user_id);
      }
    }
  }

  static void resolve_alliance_names(const std::unordered_set<int64_t>& alliance_ids, nlohmann::json& out_names,
                                     nlohmann::json&                                          out_request_ids,
                                     const std::chrono::time_point<std::chrono::steady_clock> now)
  {
    std::scoped_lock lk(alliance_data_cache_mtx);

    for (const auto& alliance_id : alliance_ids) {
      const auto it = alliance_data_cache.find(alliance_id);
      if (it != alliance_data_cache.end()) {
        if (it->second.expires_at > now) {
          for (const auto& [player_id, entry] : out_names.items()) {
            try {
              if (entry["alliance_id"].get<int64_t>() == alliance_id) {
                entry["alliance_name"] = it->second.name;
                entry["alliance_tag"]  = it->second.tag;
                entry.erase("alliance_id");
              }
            } catch (const nlohmann::json::exception&) {
            }
          }
        } else {
          // expired entry: erase and queue for fetch
          alliance_data_cache.erase(it);
          out_request_ids.push_back(alliance_id);
        }
      }
    }
  }
} // namespace names

static std::unordered_map<int64_t, int64_t> module_states;
static std::mutex                           module_states_mtx;

static std::unordered_map<int64_t, int64_t> resource_states;
static std::mutex                           resource_states_mtx;

static std::unordered_map<int64_t, int64_t> resource_states_alliance;
static std::mutex                           resource_states_alliance_mtx;

static std::unordered_map<int64_t, int64_t> slot_states;
static std::mutex                           slot_states_mtx;

static std::unordered_map<int64_t, size_t> away_assignment_states;
static std::mutex                          away_assignment_states_mtx;

static std::unordered_map<int64_t, types::PlanetaryBaseState> structure_states;
static std::mutex                                             structure_states_mtx;

static std::unordered_map<int64_t, int64_t> pieces_states;
static std::mutex                           pieces_states_mtx;

static eastl::ring_buffer<uint64_t> previously_sent_battlelogs;
static std::mutex                   previously_sent_battlelogs_mtx;

static void load_previously_sent_logs()
{
  using json = nlohmann::json;
  std::scoped_lock lk(previously_sent_battlelogs_mtx);

  previously_sent_battlelogs.set_capacity(300);

  try {
    const std::filesystem::path path(File::MakePath(File::Battles()));
    std::ifstream              file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
      spdlog::warn("Failed to open battles file (not found or not readable); starting with empty cache");
      return;
    }

    const auto battlelogs = json::parse(file);
    for (const auto& v : battlelogs) {
      previously_sent_battlelogs.push_back(v.get<uint64_t>());
    }

    spdlog::debug("Loaded {} previously sent battle logs", previously_sent_battlelogs.size());
  } catch (const std::exception& e) {
    spdlog::error("Failed to parse battles file: {}", e.what());
  } catch (...) {
    spdlog::error("Failed to parse battles file");
  }
}

static void save_previously_sent_logs()
{
  using json           = nlohmann::json;
  auto battlelog_array = json::array();

  {
    std::scoped_lock lk(previously_sent_battlelogs_mtx);
    for (auto id : previously_sent_battlelogs) {
      battlelog_array.push_back(id);
    }
  }

  try {
    const std::filesystem::path path(File::MakePath(File::Battles(), true));
    std::ofstream               file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
      spdlog::error("Failed to open battles file for writing");
      return;
    }

    file << battlelog_array.dump();
    spdlog::trace("Saved {} previously sent battle logs", battlelog_array.size());

  } catch (const std::exception& e) {
    spdlog::error("Failed to save battles JSON: {}", e.what());
  } catch (...) {
    spdlog::error("Unknown error while saving battles JSON.");
  }
}
} // namespace trackers

namespace fleets
{
// Set from any thread when another category's first sync is queued; the game thread answers with a full fleet snapshot.
static std::atomic_bool snapshot_requested{false};

// Fleets the server just sent new data for. The client fills in some values (mining and travel timers, damage) from
// that data, so the game thread re-reads these fleets shortly after.
static std::mutex                                                          reread_mtx;
static std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> reread_at;

static void request_reread(uint64_t fid)
{
  std::scoped_lock lk(reread_mtx);
  reread_at.try_emplace(fid, std::chrono::steady_clock::now() + std::chrono::seconds(1));
}
} // namespace fleets

namespace workers
{

static std::mutex                                                  sync_data_mtx;
static std::condition_variable                                     sync_data_cv;
static std::queue<std::tuple<SyncConfig::Type, std::string, bool>> sync_data_queue;

static std::mutex              combat_log_data_mtx;
static std::condition_variable combat_log_data_cv;
static std::queue<uint64_t>    combat_log_data_queue;

static void queue_data(SyncConfig::Type type, const std::string& data, bool is_first_sync = false)
{
  {
    std::scoped_lock lk(sync_data_mtx);
    sync_data_queue.emplace(type, data, is_first_sync);
    http::logging::debug("QUEUE", to_string(type), "Added data to sync queue");
  }

  sync_data_cv.notify_all();
}

static void queue_data(SyncConfig::Type type, const nlohmann::json& data, bool is_first_sync = false)
{
  if (is_first_sync && type != SyncConfig::Type::Fleets) {
    fleets::snapshot_requested.store(true, std::memory_order_release);
  }

  {
    std::scoped_lock lk(sync_data_mtx);
    sync_data_queue.emplace(type, data.dump(), is_first_sync);
    http::logging::debug("QUEUE", to_string(type), STR_FORMAT("Added {} entries to sync queue", data.size()));
  }

  sync_data_cv.notify_all();
}

static void ship_sync_data()
{
#if _WIN32
  WinRtApartmentGuard apartmentGuard;
#endif

  try {
    for (;;) {
      std::tuple<SyncConfig::Type, std::string, bool> sync_data;
      {
        std::unique_lock lock(sync_data_mtx);
        sync_data_cv.wait(lock, []() { return !sync_data_queue.empty(); });
        // Move the item out while holding the lock to avoid races/UB
        sync_data = std::move(sync_data_queue.front());
        sync_data_queue.pop();
      }

      try {
        auto& [type, data, is_first_sync] = sync_data;
        http::send_data(type, data, is_first_sync);
      } catch (const std::runtime_error& e) {
        ErrorMsg::SyncRuntime("ship", e);
      } catch (const std::exception& e) {
        ErrorMsg::SyncMsg("ship", e.what());
      } catch (const std::wstring& sz) {
        ErrorMsg::SyncMsg("ship", sz);
#if _WIN32
      } catch (winrt::hresult_error const& ex) {
        ErrorMsg::SyncWinRT("ship", ex);
#endif
      } catch (...) {
        ErrorMsg::SyncMsg("ship", "Unknown error during sending of sync data");
      }
    }
  } catch (const std::exception& e) {
    spdlog::critical("ship_sync_data thread terminated: {}", e.what());
  } catch (...) {
    spdlog::critical("ship_sync_data thread terminated: unknown exception");
  }
}

static void ship_combat_log_data()
{
  using json = nlohmann::json;
  using namespace trackers::names;

#if _WIN32
  WinRtApartmentGuard apartmentGuard;
#endif

  for (;;) {
    uint64_t journal_id;
    {
      std::unique_lock lock(combat_log_data_mtx);
      combat_log_data_cv.wait(lock, [] { return !combat_log_data_queue.empty(); });
      // Move the item out while holding the lock to avoid races/UB
      journal_id = combat_log_data_queue.front();
      combat_log_data_queue.pop();
    }

    try {
      http::logging::trace("PROCESS", "combat log", STR_FORMAT("Fetching combat log for battle {}", journal_id));

      const json journals_body{{"journal_id", journal_id}};
      auto       battle_log = http::get_scopely_data("/journals/get", journals_body.dump());
      json       battle_json;

      if (battle_log.empty()) {
        continue;
      }

      try {
        battle_json = std::move(json::parse(battle_log));
      } catch (const json::exception& e) {
        spdlog::error("Error parsing journal response from game server: {}", e.what());
        continue;
      }

      const auto& journal              = battle_json["journal"];
      const auto& target_fleet_data    = journal["target_fleet_data"];
      const auto& initiator_fleet_data = journal["initiator_fleet_data"];

      auto       names      = json::object();
      const auto now        = std::chrono::steady_clock::now();
      const auto expires_at = now + std::chrono::seconds(Config::Get().sync_resolver_cache_ttl);

      {
        std::unordered_set<std::string> user_ids;
        collect_user_ids_from_fleet(target_fleet_data, user_ids);
        collect_user_ids_from_fleet(initiator_fleet_data, user_ids);
        collect_journal_participants(journal, user_ids);

        json profiles_request{{"user_ids", json::array()}};
        resolve_player_names(user_ids, names, profiles_request["user_ids"], now);

        const auto fetch_count = profiles_request["user_ids"].size();
        if (fetch_count > 0) {
          http::logging::trace("PROCESS", "combat log", STR_FORMAT("Fetching {} player profiles", fetch_count));

          auto profiles      = http::get_scopely_data("/user_profile/profiles", profiles_request.dump());
          auto profiles_json = json::parse(profiles);

          std::scoped_lock lk(player_data_cache_mtx);

          try {
            for (const auto& [player_id, profile] : profiles_json["user_profiles"].get<json::object_t>()) {
              const auto& name        = profile["name"].get<std::string>();
              const auto& alliance_id = profile["alliance_id"].get<int64_t>();

              names[player_id] = {
                  {"name", name}, {"alliance_id", alliance_id}, {"alliance_name", nullptr}, {"alliance_tag", nullptr}};
              player_data_cache[player_id] = {.name=name, .alliance=alliance_id, .expires_at=expires_at};
            }
          } catch (const json::exception& e) {
            spdlog::error("Failed to parse user profiles: {}", e.what());
          }
        }
      }

      {
        std::unordered_set<int64_t> alliance_ids;
        json alliances_request{{"user_current_rank", 0}, {"alliance_id", 0}, {"alliance_ids", json::array()}};

        collect_alliance_ids(names, alliance_ids);
        resolve_alliance_names(alliance_ids, names, alliances_request["alliance_ids"], now);

        const auto fetch_count = alliances_request["alliance_ids"].size();
        if (fetch_count > 0) {
          http::logging::trace("PROCESS", "combat log", STR_FORMAT("Fetching {} alliance profiles", fetch_count));

          auto profiles      = http::get_scopely_data("/alliance/get_alliances_public_info", alliances_request.dump());
          auto profiles_json = json::parse(profiles);

          std::scoped_lock lk(alliance_data_cache_mtx);

          try {
            for (const auto& [alliance_id_str, profile] : profiles_json["alliances_info"].get<json::object_t>()) {
              const auto  id   = profile["id"].get<int64_t>();
              const auto& name = profile["name"].get<std::string>();
              const auto& tag  = profile["tag"].get<std::string>();

              alliance_data_cache[id] = {.name=name, .tag=tag, .expires_at=expires_at};
            }
          } catch (json::exception& e) {
            spdlog::error("Failed to parse alliance profiles: {}", e.what());
          }

          for (const auto& [player_id, entry] : names.items()) {
            try {
              if (entry.contains("alliance_id")) {
                const auto alliance_id = entry["alliance_id"].get<int64_t>();
                const auto it          = alliance_data_cache.find(alliance_id);
                if (it != alliance_data_cache.end()) {
                  entry["alliance_name"] = it->second.name;
                  entry["alliance_tag"]  = it->second.tag;
                  entry.erase("alliance_id");
                }
              }
            } catch (json::exception& e) {
              spdlog::error("Failed to update cached player data: {}", e.what());
            }
          }
        }
      }

      auto battle_array = json::array();
      battle_array.push_back(
          {{"type", SyncConfig::Type::Battles}, {"names", names}, {"journal", battle_json["journal"]}});

      try {
        auto ship_data = battle_array.dump();
        http::send_data(SyncConfig::Type::Battles, ship_data, false);
      } catch (const std::runtime_error& e) {
        ErrorMsg::SyncRuntime("combat", e);
      } catch (const std::exception& e) {
        ErrorMsg::SyncException("combat", e);
      } catch (const std::wstring& sz) {
        ErrorMsg::SyncMsg("combat", sz);
#if _WIN32
      } catch (winrt::hresult_error const& ex) {
        ErrorMsg::SyncWinRT("combat", ex);
#endif
      } catch (...) {
        ErrorMsg::SyncMsg("combat", "Unknown error during sending of sync data");
      }

    } catch (json::exception& e) {
      spdlog::error("Error parsing combat log or profiles: {}", e.what());
    } catch (std::exception& e) {
      spdlog::error("Error processing combat log: {}", e.what());
    } catch (...) {
      spdlog::error("Unknown error during processing of combat log data");
    }
  }
}

} // namespace workers

namespace processors
{

// Builds the sync event for one away team assignment instance and appends it to `out_array` only if the
// instance's observable state changed since the last time it was emitted. Caller must hold
// trackers::away_assignment_states_mtx.
static void away_assignment_event(const Digit::PrimeServer::Models::AwayAssignmentInstance& instance,
                                  nlohmann::json&                                           out_array)
{
  using json = nlohmann::json;
  using trackers::away_assignment_states;

  // Protobuf map iteration order is not guaranteed to match slot order, so sort by slot index
  // before emitting officer_ids to keep the array (and therefore the de-dup hash) stable.
  std::vector<std::pair<int64_t, int64_t>> officer_slots(instance.officerids().begin(), instance.officerids().end());
  std::ranges::sort(officer_slots, {}, &std::pair<int64_t, int64_t>::first);

  auto officer_ids = json::array();
  for (const auto& [slot, officer_id] : officer_slots) {
    if (officer_id != 0) {
      officer_ids.push_back(officer_id);
    }
  }

  auto officer_traits = json::array();
  for (const auto& trait : instance.officertraits()) {
    officer_traits.push_back({{"tid", trait.traitid()}, {"max_level", trait.maxlevel()}});
  }

  // Same ordering concern as officer_ids: sort by trait id so the array and the de-dup hash are stable.
  std::vector<std::pair<int64_t, int32_t>> critical_trait_pairs(instance.parameters().criticaltraitscores().begin(),
                                                                instance.parameters().criticaltraitscores().end());
  std::ranges::sort(critical_trait_pairs, {}, &std::pair<int64_t, int32_t>::first);

  auto critical_trait_scores = json::array();
  for (const auto& [tid, score] : critical_trait_pairs) {
    critical_trait_scores.push_back({{"tid", tid}, {"score", score}});
  }

  json event = {{"type", SyncConfig::Type::AwayAssignments},
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

  const auto state_value = std::hash<json>{}(event);

  if (const auto& it = away_assignment_states.find(instance.id());
      it == away_assignment_states.end() || it->second != state_value) {
    away_assignment_states[instance.id()] = state_value;
    out_array.push_back(std::move(event));
  }
}

static void away_assignments_list(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  static std::atomic_bool is_first_sync{true};

  if (auto response = Digit::PrimeServer::Models::AwayAssignmentUserListResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "away assignments",
                         STR_FORMAT("Processing {} away assignments", response.instances_size()));

    std::unordered_set<int64_t> ids_in_response;
    ids_in_response.reserve(static_cast<size_t>(response.instances_size()));
    auto assignment_array = json::array();

    {
      std::scoped_lock lk(trackers::away_assignment_states_mtx);

      for (const auto& instance : response.instances()) {
        ids_in_response.insert(instance.id());
        away_assignment_event(instance, assignment_array);
      }

      // Prune entries that are no longer present to prevent unbounded growth
      for (auto it = trackers::away_assignment_states.begin(); it != trackers::away_assignment_states.end();) {
        if (!ids_in_response.contains(it->first)) {
          assignment_array.push_back({{"type", "collected_" + SyncConfig::Type::AwayAssignments}, {"aid", it->first}});
          it = trackers::away_assignment_states.erase(it);
        } else {
          ++it;
        }
      }
    }

    if (!assignment_array.empty()) {
      const bool first_sync = is_first_sync.exchange(false, std::memory_order_acq_rel);
      workers::queue_data(SyncConfig::Type::AwayAssignments, assignment_array, first_sync);
    }
  } else {
    spdlog::error("Failed to parse away assignments");
  }
}

static void away_assignment_instance(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;

  if (auto instance = Digit::PrimeServer::Models::AwayAssignmentInstance(); instance.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "away assignment instance",
                         STR_FORMAT("Processing away assignment {}", instance.id()));

    auto assignment_array = json::array();
    {
      std::scoped_lock lk(trackers::away_assignment_states_mtx);
      away_assignment_event(instance, assignment_array);
    }

    if (!assignment_array.empty()) {
      workers::queue_data(SyncConfig::Type::AwayAssignments, assignment_array);
    }
  } else {
    spdlog::error("Failed to parse away assignment instance");
  }
}

static void queue_battle_ids(const std::vector<uint64_t>& battle_ids)
{
  std::vector<uint64_t> to_enqueue;
  {
    using trackers::previously_sent_battlelogs;
    using trackers::previously_sent_battlelogs_mtx;

    std::scoped_lock lk(previously_sent_battlelogs_mtx);

    for (const auto id : battle_ids | std::views::reverse) {
      if (eastl::find(previously_sent_battlelogs.begin(), previously_sent_battlelogs.end(), id)
          == previously_sent_battlelogs.end()) {
        previously_sent_battlelogs.push_back(id);
        to_enqueue.push_back(id);
      }
    }
  }

  if (!to_enqueue.empty()) {
    http::logging::debug("QUEUE", "battle headers",
                         STR_FORMAT("Queuing {} battles for background processing", to_enqueue.size()));

    {
      std::scoped_lock lk(workers::combat_log_data_mtx);
      for (const auto id : to_enqueue) {
        workers::combat_log_data_queue.push(id);
      }
    }

    trackers::save_previously_sent_logs();
    workers::combat_log_data_cv.notify_all();
  }
}

static void battle_result_headers(std::unique_ptr<std::string>&& bytes)
{
  if (auto response = Digit::PrimeServer::Models::BattleResultHeadersResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "battle headers",
                         STR_FORMAT("Processing {} battle headers", response.headers_size()));

    std::vector<uint64_t> battle_ids;
    battle_ids.reserve(static_cast<size_t>(response.headers_size()));

    for (const auto& header : response.headers()) {
      battle_ids.push_back(static_cast<uint64_t>(header.id()));
    }

    queue_battle_ids(battle_ids);
  } else {
    spdlog::error("Failed to parse battle result headers");
  }
}

struct ShipRecord {
  int64_t              id;
  int64_t              hull_id;
  int32_t              tier;
  int32_t              level;
  double               level_percentage;
  std::vector<int64_t> components;
};

static void sync_ships(const std::vector<ShipRecord>& ships)
{
  using json = nlohmann::json;
  using trackers::types::ShipState;

  static std::unordered_map<int64_t, ShipState> ship_states;
  static std::mutex                             ship_states_mtx;
  static std::atomic_bool                       is_first_sync{true};

  auto ship_array = json::array();
  {
    std::scoped_lock lk(ship_states_mtx);

    for (const auto& ship : ships) {
      const ShipState state{ship.tier, ship.level, ship.level_percentage, ship.components};

      if (const auto& it = ship_states.find(ship.id); it == ship_states.end() || it->second != state) {
        ship_states[ship.id] = state;
        ship_array.push_back({{"type", SyncConfig::Type::Ships},
                              {"psid", ship.id},
                              {"level", ship.level},
                              {"level_percentage", ship.level_percentage},
                              {"tier", ship.tier},
                              {"hull_id", ship.hull_id},
                              {"components", ship.components}});
      }
    }
  }

  if (!ship_array.empty()) {
    const bool first_sync = is_first_sync.exchange(false, std::memory_order_acq_rel);
    workers::queue_data(SyncConfig::Type::Ships, ship_array, first_sync);
  }
}

static void player_ships(std::unique_ptr<std::string>&& bytes)
{
  if (auto response = Digit::PrimeServer::Models::PlayerShipsResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "ships", STR_FORMAT("Processing {} ships", response.ships_size()));

    std::vector<ShipRecord> ships;
    ships.reserve(static_cast<size_t>(response.ships_size()));

    for (const auto& ship : response.ships() | std::views::values) {
      ships.push_back({ship.id(), ship.hullid(), ship.tier(), ship.level(), ship.levelpercentage(),
                       std::vector<int64_t>(ship.components().begin(), ship.components().end())});
    }

    sync_ships(ships);
  } else {
    spdlog::error("Failed to parse player ships");
  }
}

static void battle_report(std::unique_ptr<std::string>&& bytes)
{
  // TODO: Placeholder for future client support; currently unused by the game client.
  spdlog::debug("process_battle_report() was called");
}

static void global_active_buffs(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  static std::unordered_map<int64_t, std::pair<int32_t, int64_t>> buff_states;
  static std::mutex                                               buff_states_mtx;
  static std::atomic_bool                                         is_first_sync{true};

  if (auto response = Digit::PrimeServer::Models::GlobalActiveBuffsResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "global buffs",
                         STR_FORMAT("Processing {} active buffs", response.globalactivebuffs_size()));

    auto buff_array = json::array();
    {
      std::scoped_lock lk(buff_states_mtx);

      // Track all buff ids present in the response to detect removals.
      std::unordered_set<int64_t> present_ids;
      present_ids.reserve(static_cast<size_t>(response.globalactivebuffs_size()));

      for (const auto& buff : response.globalactivebuffs()) {
        present_ids.insert(buff.buffid());
        const bool expires = buff.has_activebuff() && buff.activebuff().has_expirytime();
        const auto state   = std::make_pair(buff.level(), expires ? buff.activebuff().expirytime().seconds() : -1);

        if (const auto& it = buff_states.find(buff.buffid()); it == buff_states.end() || it->second != state) {
          buff_states[buff.buffid()] = state;
          buff_array.push_back({{"type", SyncConfig::Type::Buffs},
                                {"bid", buff.buffid()},
                                {"level", state.first},
                                {"expiry_time", expires ? json(state.second) : json(nullptr)}});
        }
      }

      // Remove buffs that are no longer present and record each removal.
      for (auto it = buff_states.begin(); it != buff_states.end();) {
        if (!present_ids.contains(it->first)) {
          buff_array.push_back({
              {"type", "expired_" + SyncConfig::Type::Buffs},
              {"bid", it->first},
          });
          it = buff_states.erase(it);
        } else {
          ++it;
        }
      }
    }

    if (!buff_array.empty()) {
      const bool first_sync = is_first_sync.exchange(false, std::memory_order_acq_rel);
      workers::queue_data(SyncConfig::Type::Buffs, buff_array, first_sync);
    }
  } else {
    spdlog::error("Failed to parse global active buffs");
  }
}

static void alliance_games_props(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  static std::atomic_int32_t emerald_chain_level{-1};

  if (auto response = Digit::PrimeServer::Models::AllianceGamePropertiesResponse(); response.ParseFromString(*bytes)) {

    for (const auto& prop : response.properties()) {
      if (prop.propertyname() == "claimed_loyalty_tiers") {
        const auto& claimed_ec_levels = prop.valuelist();
        const auto& max_element =
            std::ranges::max_element(claimed_ec_levels, {}, [](const auto& level) { return std::stoi(level); });
        const auto ec_level = max_element != claimed_ec_levels.end() ? std::stoi(*max_element) : -1;

        int32_t current_level = emerald_chain_level.load();
        if (ec_level != current_level && emerald_chain_level.compare_exchange_strong(current_level, ec_level)) {
          auto ag_array = json::array();
          ag_array.push_back({{"type", SyncConfig::Type::EmeraldChain}, {"level", ec_level}});
          workers::queue_data(SyncConfig::Type::EmeraldChain, ag_array);
        }

        break;
      }
    }
  } else {
    spdlog::error("Failed to parse alliance games properties");
  }
}

static void starbase_modules(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::module_states;
  using trackers::module_states_mtx;

  if (auto response = Digit::PrimeServer::Models::StarbaseModulesResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "starbase modules", STR_FORMAT("Processing {} buildings", response.modulestates_size()));

    auto starbase_array = json::array();
    {
      std::scoped_lock lk(module_states_mtx);

      for (const auto& module : response.modulestates()) {
        if (const auto& it = module_states.find(module.id()); it == module_states.end() || it->second != module.level()) {
          module_states[module.id()] = module.level();
          starbase_array.push_back({{"type", SyncConfig::Type::Buildings}, {"bid", module.id()}, {"level", module.level()}});
        }
      }
    }

    if (!starbase_array.empty()) {
      workers::queue_data(SyncConfig::Type::Buildings, starbase_array);
    }
  } else {
    spdlog::error("Failed to parse resources");
  }
}

static void planetary_base_data(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::pieces_states;
  using trackers::pieces_states_mtx;

  if (auto response = Digit::PrimeServer::Models::PlanetaryBase(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "planetary base data", STR_FORMAT("Processing {} haven building pieces", response.pieces_size()));
    auto pieces_array = json::array();
    {
      std::scoped_lock lk(pieces_states_mtx);

      for (const auto& [piece, count] : response.pieces()) {
        if (const auto& it = pieces_states.find(piece); it == pieces_states.end() || it->second != count) {
          pieces_states[piece] = count;
          pieces_array.push_back({{"type", SyncConfig::Type::Haven + "_pieces"}, {"sid", piece}, {"count", count}});
        }
      }
    }

    if (!pieces_array.empty()) {
      workers::queue_data(SyncConfig::Type::Haven, pieces_array);
    }
  }
}

static void planetary_map_building_data(const google::protobuf::Map<int64_t, Digit::PrimeServer::Models::PlanetaryMapData_BuildingData>& buildings)
{
  // TODO: use PlanetaryMapBuildingDiff for updates during game session

  using trackers::structure_states;
  using trackers::structure_states_mtx;
  using trackers::types::PlanetaryBaseState;

  auto structure_array = nlohmann::json::array();
  {
    std::scoped_lock lk(structure_states_mtx);

    for (const auto& [id, building] : buildings) {
      const PlanetaryBaseState state{building.level(), building.status(), building.position()};

      if (const auto& it = structure_states.find(id); it == structure_states.end() || it->second != state) {
        structure_states[id] = state;
        structure_array.push_back({{"type", SyncConfig::Type::Haven}, {"sid", building.specid()}, {"id", id}, {"level", building.level()}, {"status", building.status()}, {"position", building.position()}});
      }
    }
  }

  if (!structure_array.empty()) {
    workers::queue_data(SyncConfig::Type::Haven, structure_array, true);
  }
}

static void planetary_map_data(std::unique_ptr<std::string>&& bytes)
{
  if (auto response = Digit::PrimeServer::Models::PlanetaryMapData(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "planetary map data", STR_FORMAT("Processing {} buildings", response.buildings_size()));
    planetary_map_building_data(response.buildings());
  }
}

static void planetary_map_prosperity(std::unique_ptr<std::string>&& bytes)
{
  if (auto response = Digit::PrimeServer::Models::PlanetaryMapProsperityResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "planetary map prosperity", STR_FORMAT("Processing prosperity={}", response.prosperity()));
    const auto prosperity_array = nlohmann::json::array(
        {{{"type", SyncConfig::Type::Haven + "_prosperity"}, {"prosperity", response.prosperity()}}});
    workers::queue_data(SyncConfig::Type::Haven, prosperity_array, true);
  }
}

// The game's record of the local player, or nullptr. Calls into IL2CPP: game thread only.
static UserProfile* local_user_profile()
{
  auto* const manager = UserProfileManager::Instance();
  if (manager == nullptr) {
    return nullptr;
  }

  static auto fn = UserProfileManager::get_class_helper().GetMethodInfo("GetLocalUserProfile");
  if (fn == nullptr) {
    return nullptr;
  }

  Il2CppException* exception = nullptr;
  auto* const      profile   = reinterpret_cast<UserProfile*>(il2cpp_runtime_invoke(fn, manager, nullptr, &exception));
  return exception == nullptr ? profile : nullptr;
}

static void planetary_map(std::unique_ptr<std::string>&& bytes)
{
  if (auto response = Digit::PrimeServer::Models::PlanetaryMapResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "planetary map", STR_FORMAT("Processing {} haven structures", response.mapdata().buildings_size()));

    const auto& owner = response.owneruserid();
    if (!owner.empty()) {
      auto localProfile = local_user_profile();
      if (localProfile == nullptr) {
        spdlog::error("Failed to get local user profile");
        return;
      }

      const auto userId = localProfile->UserId;
      if (userId == nullptr) {
        spdlog::error("Local user profile has empty UserId");
        return;
      }

      if (owner != userId) {
        spdlog::debug("Planetary map data is not for local user, skipping processing");
        return;
      }
    }

    planetary_map_building_data(response.mapdata().buildings());
  } else {
    spdlog::error("Failed to parse planetary map");
  }
}

// The local player's user id, or "" if the profile is not available. Game thread only.
static std::string local_user_id(UserProfile* profile)
{
  if (profile == nullptr) {
    return {};
  }

  const auto* const user_id = profile->UserId;
  return user_id == nullptr ? std::string{} : to_string(user_id);
}

// Sends the local player's own profile; `self` and `level` come from the game thread. Only the record whose user id
// is `self` is read. The server record carries level 0, so the in-memory Level is used: it lags one own-record
// response after a level-up.
static void own_profile(std::unique_ptr<std::string>&& bytes, const std::string& self, int32_t level)
{
  using json = nlohmann::json;

  static std::tuple<int32_t, uint64_t, bool> last_sent;
  static bool                                has_sent = false;
  static std::mutex                          last_sent_mtx;

  auto response = Digit::PrimeServer::Models::UserProfilesResponse();
  if (!response.ParseFromString(*bytes)) {
    spdlog::error("Failed to parse own user profile");
    return;
  }

  const Digit::PrimeServer::Models::UserProfile* own = nullptr;
  for (const auto& profile : response.userprofiles()) {
    if (profile.userid() == self) {
      own = &profile;
      break;
    }
  }

  if (own == nullptr) {
    return;
  }

  // XP rides along but is not part of the key: an XP-only change waits for the next keyed change.
  const auto key = std::make_tuple(level, own->militarymight(), own->planetarybaseunlocked());
  bool       first_sync;
  {
    std::scoped_lock lk(last_sent_mtx);
    first_sync = !has_sent;
    if (!first_sync && last_sent == key) {
      return;
    }
    last_sent = key;
    has_sent  = true;
  }

  auto profile_array = json::array();
  profile_array.push_back({{"type", SyncConfig::Type::Profile},
                           {"uid", self},
                           {"level", level},
                           {"xp", own->xp()},
                           {"military_might", own->militarymight()},
                           {"planetary_base_unlocked", own->planetarybaseunlocked()}});

  workers::queue_data(SyncConfig::Type::Profile, profile_array, first_sync);
}

static void player_inventories(std::unique_ptr<std::string>&& bytes)
{
  using json   = nlohmann::json;
  using trackers::types::pairhash;
  using item_t = std::underlying_type_t<Digit::PrimeServer::Models::InventoryItemType>;

  static std::unordered_map<std::pair<item_t, int64_t>, int64_t, pairhash> inventory_states;
  static std::mutex                                                        inventory_states_mtx;
  static std::atomic_bool                                                  is_first_sync{true};

  if (auto response = Digit::PrimeServer::Models::InventoryResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "player inventories",
                         STR_FORMAT("Processing {} inventories", response.inventories_size()));

    auto inventory_items = json::array();
    {
      std::scoped_lock lk(inventory_states_mtx);

      for (const auto& inventory : response.inventories() | std::views::values) {
        for (const auto& item : inventory.items()) {
          if (item.has_commonparams()) {
            const auto item_id = item.commonparams().refid();
            const auto count   = item.count();
            const auto key     = std::make_pair(item.type(), item_id);

            if (const auto& it = inventory_states.find(key); it == inventory_states.end() || it->second != count) {
              inventory_states[key] = count;
              inventory_items.push_back({{"type", SyncConfig::Type::Inventory},
                                         {"item_type", item.type()},
                                         {"refid", item_id},
                                         {"count", count}});
            }
          }
        }
      }
    }

    if (!inventory_items.empty()) {
      const bool first_sync = is_first_sync.exchange(false, std::memory_order_acq_rel);
      workers::queue_data(SyncConfig::Type::Inventory, inventory_items, first_sync);
    }
  } else {
    spdlog::error("Failed to parse player inventories");
  }
}

static void jobs(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using job_state = std::tuple<int64_t, int64_t, int64_t>;
  static std::unordered_map<std::string, job_state> jobs_active;
  static std::mutex                                 jobs_active_mtx;
  static std::atomic_bool                           is_first_sync{true};

  if (auto response = Digit::PrimeServer::Models::JobResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "jobs", STR_FORMAT("Processing {} jobs", response.jobs_size()));

    std::unordered_set<std::string> uuids_in_response;
    uuids_in_response.reserve(response.jobs_size());
    auto job_array = json::array();

    for (const auto& job : response.jobs()) {
      const std::string& uuid = job.uuid();
      uuids_in_response.insert(uuid);

      const job_state state{job.starttime().seconds(), job.duration(), job.reductioninseconds()};

      bool emit = false;
      {
        std::scoped_lock lk(jobs_active_mtx);
        const auto [it, inserted] = jobs_active.try_emplace(uuid, state);
        emit                      = inserted || it->second != state;
        it->second                = state;
      }

      if (!emit) {
        continue;
      }

      json job_params;

      switch (job.type()) {
        case Digit::PrimeServer::Models::JOBTYPE_RESEARCH: {
          const auto& research = job.researchparams();
          job_params           = {{"rid", research.projectid()}, {"level", research.level()}};
        } break;
        case Digit::PrimeServer::Models::JOBTYPE_STARBASECONSTRUCTION: {
          const auto& construction = job.starbaseconstructionparams();
          job_params               = {{"bid", construction.moduleid()}, {"level", construction.level()}};
        } break;
        case Digit::PrimeServer::Models::JOBTYPE_SHIPTIERUP: {
          const auto& upgrade = job.tierupshipparams();
          job_params          = {{"psid", upgrade.shipid()}, {"tier", upgrade.newtier()}};
        } break;
        case Digit::PrimeServer::Models::JOBTYPE_SHIPSCRAP: {
          const auto& scrap = job.scrapyardparams();
          job_params        = {{"psid", scrap.shipid()}, {"hull_id", scrap.hullid()}, {"level", scrap.level()}};
        } break;
        case Digit::PrimeServer::Models::JOBTYPE_AWAYASSIGNMENT: {
          const auto& away_assignment = job.awayassignmentparams();
          job_params                  = {{"aid", away_assignment.awayassignmentinstanceid()}};
        } break;
        case Digit::PrimeServer::Models::JOBTYPE_REPAIRFLEET: {
          // part of fleet sync
          if (!Config::Get().sync_options.fleets) {
            continue;
          }
          job_params = {{"fid", job.repairfleetparams().fleetid()}};
          fleets::request_reread(job.repairfleetparams().fleetid());
        } break;
        default:
          continue;
      }

      json job_data = json::object({{"type", SyncConfig::Type::Jobs},
                                    {"job_type", job.type()},
                                    {"uuid", job.uuid()},
                                    {"start_time", job.starttime().seconds()},
                                    {"duration", job.duration()},
                                    {"reduction", job.reductioninseconds()}});

      job_data.update(job_params);
      job_array.push_back(std::move(job_data));
    }

    // Prune entries that are no longer present to prevent unbounded growth
    {
      std::scoped_lock lk(jobs_active_mtx);
      for (auto it = jobs_active.begin(); it != jobs_active.end();) {
        if (!uuids_in_response.contains(it->first)) {
          job_array.push_back({{"type", "completed_" + SyncConfig::Type::Jobs}, {"uuid", it->first}});
          it = jobs_active.erase(it);
        } else {
          ++it;
        }
      }
    }

    if (!job_array.empty()) {
      const bool first_sync = is_first_sync.exchange(false, std::memory_order_acq_rel);
      workers::queue_data(SyncConfig::Type::Jobs, job_array, first_sync);
    }
  } else {
    spdlog::error("Failed to parse jobs");
  }
}

static void active_missions(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  static std::unordered_set<int64_t> active_mission_states;
  static std::mutex                  active_mission_states_mtx;

  if (auto response = Digit::PrimeServer::Models::ActiveMissionsResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "active missions",
                         STR_FORMAT("Processing {} active missions", response.activemissions_size()));

    std::unordered_set<int64_t> active_missions;
    for (const auto& mission : response.activemissions()) {
      active_missions.insert(mission.id());
    }

    bool changed = false;
    {
      std::scoped_lock lk(active_mission_states_mtx);
      if (active_mission_states != active_missions) {
        changed               = true;
        active_mission_states = std::move(active_missions);
      }
    }

    if (changed && !active_mission_states.empty()) {
      auto mission_array = json::array();

      for (const auto mission : active_mission_states) {
        mission_array.push_back({{"type", "active_" + SyncConfig::Type::Missions}, {"mid", mission}});
      }

      workers::queue_data(SyncConfig::Type::Missions, mission_array);
    }
  } else {
    spdlog::error("Failed to parse active missions");
  }
}

static void completed_missions(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  static std::vector<int64_t> completed_mission_states;
  static std::mutex           completed_mission_states_mtx;

  if (auto response = Digit::PrimeServer::Models::CompletedMissionsResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "completed missions",
                         STR_FORMAT("Processing {} completed missions", response.completedmissions_size()));

    const auto&          missions = response.completedmissions();
    std::vector<int64_t> completed_missions{missions.begin(), missions.end()};
    std::vector<int64_t> diff;

    // Assume the completed missions list is append-only: new entries may be added, but existing ones are never removed.
    {
      std::scoped_lock lk(completed_mission_states_mtx);
      std::ranges::set_difference(completed_missions, completed_mission_states, std::back_inserter(diff));

      if (!diff.empty()) {
        completed_mission_states = std::move(completed_missions);
      }
    }

    if (!diff.empty()) {
      auto mission_array = json::array();

      for (const auto mission : diff) {
        mission_array.push_back({{"type", SyncConfig::Type::Missions}, {"mid", mission}});
      }

      workers::queue_data(SyncConfig::Type::Missions, mission_array);
    }
  } else {
    spdlog::error("Failed to parse completed missions");
  }
}

static void officers(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::types::RankLevelShardsState;

  static std::unordered_map<uint64_t, RankLevelShardsState> officer_states;
  static std::mutex                                         officer_states_mtx;

  if (auto response = Digit::PrimeServer::Models::OfficersResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "officers", STR_FORMAT("Processing {} officers", response.officers_size()));

    auto officers_array = json::array();
    {
      std::scoped_lock lk(officer_states_mtx);

      for (const auto& officer : response.officers()) {
        const RankLevelShardsState officer_state{officer.rankindex(), officer.level(), officer.shardcount()};

        if (const auto& it = officer_states.find(officer.id());
            it == officer_states.end() || it->second != officer_state) {
          officer_states[officer.id()] = officer_state;
          officers_array.push_back({{"type", SyncConfig::Type::Officer},
                                    {"oid", officer.id()},
                                    {"rank", officer.rankindex()},
                                    {"level", officer.level()},
                                    {"shard_count", officer.shardcount()}});
            }
      }
    }

    if (!officers_array.empty()) {
      workers::queue_data(SyncConfig::Type::Officer, officers_array);
    }
  } else {
    spdlog::error("Failed to parse officers");
  }
}

static void research_trees_state(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  static std::unordered_map<int64_t, int32_t> research_states;
  static std::mutex                           research_states_mtx;

  if (auto response = Digit::PrimeServer::Models::ResearchTreesState(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "research trees state",
                         STR_FORMAT("Processing {} research projects", response.researchprojectlevels_size()));

    auto research_array = json::array();
    {
      std::scoped_lock lk(research_states_mtx);

      for (const auto& [id, level] : response.researchprojectlevels()) {
        if (const auto& it = research_states.find(id); it == research_states.end() || it->second != level) {
          research_states[id] = level;
          research_array.push_back({{"type", SyncConfig::Type::Research}, {"rid", id}, {"level", level}});
        }
      }
    }

    if (!research_array.empty()) {
      workers::queue_data(SyncConfig::Type::Research, research_array);
    }
  } else {
    spdlog::error("Failed to parse research trees state");
  }
}

static void resources(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::resource_states;
  using trackers::resource_states_mtx;
  static std::atomic_bool is_first_sync{true};

  if (auto response = Digit::PrimeServer::Models::ResourcesResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "resources", STR_FORMAT("Processing {} resources", response.resources_size()));

    auto resource_array = json::array();
    {
      std::scoped_lock lk(resource_states_mtx);

      for (const auto& resource : response.resources()) {
        if (const auto& it = resource_states.find(resource.id()); it == resource_states.end() || it->second != resource.currentamount()) {
          resource_states[resource.id()] = resource.currentamount();
          resource_array.push_back({{"type", SyncConfig::Type::Resources}, {"rid", resource.id()}, {"amount", resource.currentamount()}});
        }
      }
    }

    if (!resource_array.empty()) {
      const bool first_sync = is_first_sync.exchange(false, std::memory_order_acq_rel);
      workers::queue_data(SyncConfig::Type::Resources, resource_array, first_sync);
    }
  } else {
    spdlog::error("Failed to parse resources");
  }
}

static void resources_delta(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::resource_states;
  using trackers::resource_states_mtx;

  if (auto response = Digit::PrimeServer::Models::ResourcesDeltaResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "resources_delta", STR_FORMAT("Processing {} resource changes", response.resources_size()));

    auto resource_array = json::array();
    {
      std::scoped_lock lk(resource_states_mtx);

      for (const auto& resource : response.resources()) {
        if (const auto& it = resource_states.find(resource.id()); it == resource_states.end() || it->second != resource.amount()) {
          resource_states[resource.id()] = resource.amount();
          resource_array.push_back({{"type", SyncConfig::Type::Resources}, {"rid", resource.id()}, {"amount", resource.amount()}});
        }
      }
    }

    if (!resource_array.empty()) {
      workers::queue_data(SyncConfig::Type::Resources, resource_array);
    }

  } else {
    spdlog::error("Failed to parse resources delta");
  }
}

static void alliance_resources(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::resource_states_alliance;
  using trackers::resource_states_alliance_mtx;
  static std::atomic_bool is_first_sync{true};

  if (auto response = Digit::PrimeServer::Models::AllianceGetBankResourcesResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "alliance_resources", STR_FORMAT("Processing {} alliance resources", response.items_size()));

    auto resource_array = json::array();
    {
      std::scoped_lock lk(resource_states_alliance_mtx);

      for (const auto& item : response.items()) {
        if (item.has_commonparams()) {
          const auto id = item.commonparams().refid();
          const auto amount   = item.count();

          if (const auto& it = resource_states_alliance.find(id); it == resource_states_alliance.end() || it->second != amount) {
            resource_states_alliance[id] = amount;
            resource_array.push_back({{"type", "alliance_" + SyncConfig::Type::Resources}, {"rid", id}, {"amount", amount}});
          }
        }
      }
    }

    if (!resource_array.empty()) {
      const bool first_sync = is_first_sync.exchange(false, std::memory_order_acq_rel);
      workers::queue_data(SyncConfig::Type::Resources, resource_array, first_sync);
    }
  } else {
    spdlog::error("Failed to parse alliance resources");
  }
}

static void alliance_resources_delta(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::resource_states_alliance;
  using trackers::resource_states_alliance_mtx;

  if (auto response = Digit::PrimeServer::Models::AllianceResourcesDeltaResponse(); response.ParseFromString(*bytes)) {
    http::logging::trace("PROCESS", "alliance_resources_delta", STR_FORMAT("Processing {} alliance resource changes", response.resources_size()));

    auto resource_array = json::array();
    {
      std::scoped_lock lk(resource_states_alliance_mtx);

      for (const auto& resource : response.resources()) {
        if (const auto& it = resource_states_alliance.find(resource.id()); it == resource_states_alliance.end() || it->second != resource.amount()) {
          resource_states_alliance[resource.id()] = resource.amount();
          resource_array.push_back({{"type", "alliance_" + SyncConfig::Type::Resources}, {"rid", resource.id()}, {"amount", resource.amount()}});
        }
      }
    }

    if (!resource_array.empty()) {
      workers::queue_data(SyncConfig::Type::Resources, resource_array);
    }
  } else {
    spdlog::error("Failed to parse alliance resources delta");
  }
}

static void single_slot_locked(const Digit::PrimeServer::Models::EntitySlot& slot, nlohmann::json& slot_array)
{
  using json = nlohmann::json;

  json    slot_params;
  int64_t state_value = slot.has_slotitemid() ? slot.slotitemid().value() : -1;

  switch (slot.slottype()) {
    case Digit::PrimeServer::Models::SLOTTYPE_CONSUMABLE:
      if (slot.has_consumableslotparams()) {
        const auto& consumable = slot.consumableslotparams();
        slot_params["expiry_time"] =
            consumable.has_expirytime() ? json(consumable.expirytime().seconds()) : json(nullptr);
      }
      break;
    case Digit::PrimeServer::Models::SLOTTYPE_OFFICERPRESET:
      if (slot.has_officerpresetslotparams()) {
        const auto& preset = slot.officerpresetslotparams();
        slot_params        = {{"name", preset.name()}, {"order", preset.order()}, {"officer_ids", preset.officerids()}};
        state_value        = static_cast<int64_t>(std::hash<json>{}(slot_params));
      }
      break;
    case Digit::PrimeServer::Models::SLOTTYPE_FLEETCOMMANDER:
      if (slot.has_fleetcommanderslotparams()) {
        slot_params["order"] = slot.fleetcommanderslotparams().order();
      }
      break;
    case Digit::PrimeServer::Models::SLOTTYPE_SELECTABLESKILL:
      if (slot.has_selectableskillslotparams()) {
        const auto& skill = slot.selectableskillslotparams();
        slot_params["cooldown_expiration"] =
            skill.has_cooldownexpiration() ? json(skill.cooldownexpiration().seconds()) : json(nullptr);
      }
      break;
    case Digit::PrimeServer::Models::SLOTTYPE_FORBIDDENTECH:
      if (slot.has_forbiddentechslotparams()) {
        const auto& tech = slot.forbiddentechslotparams();
        slot_params      = {{"owner_id", tech.ownerid()}, {"tier", tech.tier()}, {"level", tech.level()}};
        state_value      = static_cast<int64_t>(std::hash<json>{}(slot_params));
      }
      break;
    case Digit::PrimeServer::Models::SLOTTYPE_FLEETPRESET:
      if (slot.has_fleetpresetslotparams()) {
        const auto& preset     = slot.fleetpresetslotparams();
        auto        setup_json = json::array();

        for (const auto& setup : preset.setups()) {
          setup_json.push_back({{"drydock_id", setup.drydockid()},
                                {"ship_id", setup.shipids()[0]},
                                {"officer_ids", setup.officerids()}});
        }

        slot_params = {{"name", preset.name()}, {"order", preset.order()}, {"setup", setup_json}};
        state_value = static_cast<int64_t>(std::hash<json>{}(slot_params));
      }
      break;
    default:
      return;
  }

  // Lock is not held because caller already does
  using trackers::slot_states;

  if (const auto& it = slot_states.find(slot.id()); it == slot_states.end() || it->second != state_value) {
    slot_states[slot.id()] = state_value;
    slot_array.push_back({{"type", SyncConfig::Type::Slots},
                          {"sid", slot.id()},
                          {"slot_type", slot.slottype()},
                          {"spec_id", slot.slotspecid()},
                          {"item_id", slot.has_slotitemid() ? json(slot.slotitemid().value()) : json(nullptr)},
                          {"params", slot_params}});
  }
}

static void entity_slots(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;

  if (auto response = Digit::PrimeServer::Models::EntitySlots(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "entity slots", STR_FORMAT("Processing {} slots", response.entityslots__size()));

    auto slot_array = json::array();
    {
      std::scoped_lock lk(trackers::slot_states_mtx);

      for (const auto& slot : response.entityslots_()) {
        single_slot_locked(slot, slot_array);
      }
    }

    if (!slot_array.empty()) {
      workers::queue_data(SyncConfig::Type::Slots, slot_array);
    }
  } else {
    spdlog::error("Failed to parse entity slots");
  }
}

static void entity_slots_data(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;

  if (auto response = Digit::PrimeServer::Models::EntitySlotsData(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "entity slots data",
                         STR_FORMAT("Processing {} slots", response.entityslots_size()));

    auto slot_array = json::array();
    {
      std::scoped_lock lk(trackers::slot_states_mtx);

      for (const auto& slots : response.entityslots()) {
        http::logging::trace("PROCESS", "entity slots data",
                             STR_FORMAT("Processing {} slots for entity {}", slots.slots_size(),
                                        static_cast<int32_t>(slots.entitytype())));

        for (const auto& slot : slots.slots()) {
          single_slot_locked(slot, slot_array);
        }
      }
    }

    if (!slot_array.empty()) {
      workers::queue_data(SyncConfig::Type::Slots, slot_array);
    }
  } else {
    spdlog::error("Failed to parse entity slots data");
  }
}

static void forbidden_tech(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::types::RankLevelShardsState;

  static std::unordered_map<uint64_t, RankLevelShardsState> tech_states;
  static std::mutex                                         tech_states_mtx;

  if (auto response = Digit::PrimeServer::Models::ForbiddenTechsResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "techs",
                         STR_FORMAT("Processing {} forbidden/chaos techs", response.forbiddentechs_size()));

    auto tech_array = json::array();
    {
      std::scoped_lock lk(tech_states_mtx);

      for (const auto& tech : response.forbiddentechs()) {
        const RankLevelShardsState tech_state{tech.tier(), tech.level(), tech.shardcount()};

        if (const auto& it = tech_states.find(tech.id()); it == tech_states.end() || it->second != tech_state) {
          tech_states[tech.id()] = tech_state;
          tech_array.push_back({{"type", SyncConfig::Type::Tech},
                                {"fid", tech.id()},
                                {"tier", tech.tier()},
                                {"level", tech.level()},
                                {"shard_count", tech.shardcount()}});
        }
      }
    }

    if (!tech_array.empty()) {
      workers::queue_data(SyncConfig::Type::Tech, tech_array);
    }
  } else {
    spdlog::error("Failed to parse forbidden techs");
  }
}

static void active_officer_traits(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using trackers::types::pairhash;

  static std::unordered_map<std::pair<int64_t, int64_t>, int32_t, pairhash> trait_states;
  static std::mutex                                                         trait_states_mtx;

  if (auto response = Digit::PrimeServer::Models::OfficerTraitsResponse(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "active officer traits",
                         STR_FORMAT("Processing {} active officer traits", response.activeofficertraits_size()));

    auto trait_array = json::array();
    {
      std::scoped_lock lk(trait_states_mtx);

      for (const auto& [officer_id, officer_traits] : response.activeofficertraits()) {
        for (const auto& trait : officer_traits.activetraits() | std::views::values) {
          const auto& key = std::make_pair(officer_id, trait.traitid());

          if (const auto& it = trait_states.find(key); it == trait_states.end() || it->second != trait.level()) {
            trait_states[key] = trait.level();
            trait_array.push_back({{"type", SyncConfig::Type::Traits},
                                   {"oid", officer_id},
                                   {"tid", trait.traitid()},
                                   {"level", trait.level()}});
          }
        }
      }
    }

    if (!trait_array.empty()) {
      workers::queue_data(SyncConfig::Type::Traits, trait_array);
    }
  } else {
    spdlog::error("Failed to parse active officer traits");
  }
}

// Store offers arrive in Platform responses. Prices are in Resource.amount; amountUlong is the fallback.
static int64_t store_offer_amount(const Digit::PrimePlatform::Content::Resource& resource)
{ return static_cast<int64_t>(resource.amount() != 0 ? resource.amount() : resource.amountulong()); }

// Platform ids are strings; emit numeric ids as numbers like the rest of the sync data.
static nlohmann::json store_offer_id(const std::string& id)
{
  int64_t     value = 0;
  const auto* end   = id.data() + id.size();
  if (const auto [ptr, ec] = std::from_chars(id.data(), end, value); !id.empty() && ec == std::errc{} && ptr == end) {
    return value;
  }

  return id;
}

static const Digit::PrimePlatform::Content::ContentItem*
store_offer_consumable(const Digit::PrimePlatform::Content::BundleContent& content)
{
  for (const auto* list : {&content.static_(), &content.showcase(), &content.gifts()}) {
    for (const auto& item : list->items()) {
      if (item.type() == "consumable") {
        return &item;
      }
    }
  }

  return nullptr;
}

// Emits one snapshot of the faction store's consumable offers (favor levels) whenever its content changes.
static void store_offers(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;

  static constexpr std::string_view category           = "factions";
  static size_t                     store_offers_state = 0;
  static std::mutex                 store_offers_mtx;

  if (auto response = Digit::PrimePlatform::Content::Bundles(); response.ParseFromString(*bytes)) {

    http::logging::trace("PROCESS", "store offers", STR_FORMAT("Processing {} store offers", response.bundles__size()));

    // A store offers group carries whole categories; skip groups without the faction store.
    struct Offer {
      const Digit::PrimePlatform::Content::Bundle*      bundle;
      const Digit::PrimePlatform::Content::ContentItem* consumable;
    };
    std::vector<Offer> bundles;
    bool               has_category = false;
    for (const auto& bundle : response.bundles_()) {
      if (bundle.category() == category) {
        has_category = true;
        if (const auto* consumable = store_offer_consumable(bundle.content()); consumable != nullptr) {
          bundles.push_back({&bundle, consumable});
        }
      }
    }
    if (!has_category) {
      return;
    }

    // Sort by bundle id, then consumable id, so the snapshot (and therefore the de-dup hash) does not depend on server
    // order.
    std::ranges::sort(bundles, [](const Offer& a, const Offer& b) {
      return std::pair{a.bundle->bundleid(), std::string_view{a.consumable->id()}}
             < std::pair{b.bundle->bundleid(), std::string_view{b.consumable->id()}};
    });

    auto offers = json::array();
    for (const auto& [bundle, consumable] : bundles) {
      auto cost = json::array();
      for (const auto& entry : bundle->cost()) {
        for (const auto& resource : entry.cost_()) {
          cost.push_back({{"rid", store_offer_id(resource.id())}, {"amount", store_offer_amount(resource)}});
        }
      }

      // For faction_standing requirements the amount is a zero-based tier index.
      auto req = json::array();
      for (const auto& resource : bundle->requirements()) {
        req.push_back({{"type", resource.type()},
                       {"id", store_offer_id(resource.id())},
                       {"amount", store_offer_amount(resource)}});
      }

      const auto& details = bundle->offerdetails();
      offers.push_back({{"bid", bundle->bundleid()},
                        {"cid", store_offer_id(consumable->id())},
                        {"cost", cost},
                        {"req", req},
                        {"valid_count", details.validcount()},
                        {"same_remaining_count", details.sameremainingcount()}});
    }

    json event = {{"type", SyncConfig::Type::StoreOffers}, {"category", category}, {"offers", offers}};

    // Queue under the lock so the remembered snapshot and the queue order agree.
    const auto       state_value = std::hash<json>{}(event);
    std::scoped_lock lk(store_offers_mtx);
    if (store_offers_state != state_value) {
      store_offers_state = state_value;
      workers::queue_data(SyncConfig::Type::StoreOffers, json::array({event}));
    }
  } else {
    spdlog::error("Failed to parse store offers");
  }
}

namespace deployed_fleets
{
  using Digit::PrimeServer::Models::ClientModifierType;
  using Digit::PrimeServer::Models::DeployedFleetState;

  static const char* state_name(DeployedFleetState state)
  {
    switch (state) {
      case Digit::PrimeServer::Models::DEPLOYEDFLEETSTATE_IDLE:
        return "Idle";
      case Digit::PrimeServer::Models::DEPLOYEDFLEETSTATE_MOVING:
        return "Moving";
      case Digit::PrimeServer::Models::DEPLOYEDFLEETSTATE_WARPING:
        return "Warping";
      case Digit::PrimeServer::Models::DEPLOYEDFLEETSTATE_BATTLING:
        return "Battling";
      case Digit::PrimeServer::Models::DEPLOYEDFLEETSTATE_RECALL:
        return "Recall";
      // docked at a mining node, not the station
      case Digit::PrimeServer::Models::DEPLOYEDFLEETSTATE_DOCKED:
        return "OnNode";
      case Digit::PrimeServer::Models::DEPLOYEDFLEETSTATE_PREBATTLE:
        return "PreBattle";
      default:
        return "Unknown";
    }
  }

  // 1234 not 1234.0, and 14.364 not 14.36400032043457
  static nlohmann::json number(float value)
  {
    if (std::trunc(value) == value && std::abs(value) < 9.0e15f) {
      return static_cast<int64_t>(value);
    }

    char   buffer[32];
    double widened = value;
    if (const auto [end, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value); ec == std::errc{}) {
      // Floating-point from_chars requires macOS 26; use the existing JSON parser on supported clients.
      auto parsed = nlohmann::json::parse(std::begin(buffer), end, nullptr, false);
      if (parsed.is_number()) {
        return parsed;
      }
    }
    return widened;
  }

  using FloatMap = google::protobuf::Map<int32_t, float>;

  static std::optional<float> lookup(const FloatMap& map, ClientModifierType key)
  {
    if (const auto it = map.find(key); it != map.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  // leave missing stats out instead of sending 0
  static void put(nlohmann::json& record, const char* name, const FloatMap& map, ClientModifierType key)
  {
    if (const auto value = lookup(map, key); value && std::isfinite(*value)) {
      record[name] = number(*value);
    }
  }

  // summed over the fleet's ships: shipStats has the max, shipAttributes the damage taken
  static void put_points(nlohmann::json& record, const char* current_name, const char* max_name,
                         const Digit::PrimeServer::Models::DeployedFleet& fleet, ClientModifierType max_key,
                         ClientModifierType damage_key)
  {
    double total_max    = 0;
    double total_damage = 0;
    bool   found        = false;

    for (const auto& [ship_id, stats] : fleet.shipstats()) {
      const auto max = lookup(stats.shipattributes_(), max_key);
      if (!max) {
        continue;
      }

      found = true;
      total_max += *max;
      if (const auto it = fleet.shipattributes().find(ship_id); it != fleet.shipattributes().end()) {
        total_damage += lookup(it->second.shipattributes_(), damage_key).value_or(0.0f);
      }
    }

    if (found) {
      record[current_name] = std::llround(std::max(0.0, total_max - total_damage));
      record[max_name]     = std::llround(total_max);
    }
  }
} // namespace deployed_fleets

static void my_deployed_fleets(std::unique_ptr<std::string>&& bytes)
{
  using json = nlohmann::json;
  using namespace Digit::PrimeServer::Models;
  using deployed_fleets::put;

  auto response = MyDeployedFleetsResponse();
  if (!response.ParseFromString(*bytes)) {
    spdlog::error("Failed to parse deployed fleets");
    return;
  }

  http::logging::trace("PROCESS", "deployed fleets", STR_FORMAT("Processing {} fleets", response.fleets_size()));

  // the server resends every fleet on each update, so only send the ones that changed
  static std::unordered_map<int64_t, size_t> last_sent;
  static std::mutex                          last_sent_mtx;

  const int64_t now =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  auto fleet_array = json::array();

  for (const auto& [fleet_id, fleet] : response.fleets()) {
    json record = {{"type", SyncConfig::Type::Fleets},
                   {"fid", fleet_id},
                   {"src", "server"},
                   {"server_state", static_cast<int>(fleet.state())},
                   {"server_state_name", deployed_fleets::state_name(fleet.state())},
                   {"node_id", fleet.nodeid()}};

    if (fleet.hullids_size() > 0) {
      // widen through uint32 like the client records
      record["hull_id"] = static_cast<int64_t>(static_cast<uint32_t>(fleet.hullids(0)));
    }

    if (fleet.has_nodeaddress()) {
      record["system_id"] = fleet.nodeaddress().system();
    }

    deployed_fleets::put_points(record, "hull_hp", "hull_max", fleet, CLIENTMODIFIERTYPE_MODHULLHPMAX,
                                CLIENTMODIFIERTYPE_SHIPHULLDAMAGE);
    deployed_fleets::put_points(record, "shield_hp", "shield_max", fleet, CLIENTMODIFIERTYPE_MODSHIELDHPMAX,
                                CLIENTMODIFIERTYPE_SHIPSHIELDDAMAGE);

    const auto& stats = fleet.stats();
    put(record, "cargo_cap", stats, CLIENTMODIFIERTYPE_MODCARGOCAPACITY);
    put(record, "warp_range", stats, CLIENTMODIFIERTYPE_MODWARPDISTANCE);
    put(record, "strength", fleet.attributes(), CLIENTMODIFIERTYPE_FLEETSTRENGTH);

    // a count only, no other players' ids
    record["pursued_by"] = fleet.pursuedbynum();

    const auto       state_value = std::hash<json>{}(record);
    std::scoped_lock lk(last_sent_mtx);
    if (auto& last = last_sent[fleet_id]; last != state_value) {
      last = state_value;
      record["t"] = now;
      fleet_array.push_back(std::move(record));
      fleets::request_reread(fleet_id);
    }
  }

  // forget fleets that left (docked, destroyed), so they're sent again when they're back
  {
    std::scoped_lock lk(last_sent_mtx);
    std::erase_if(last_sent, [&](const auto& entry) {
      if (response.fleets().contains(entry.first)) {
        return false;
      }
      fleets::request_reread(entry.first);
      return true;
    });
  }

  if (!fleet_array.empty()) {
    workers::queue_data(SyncConfig::Type::Fleets, fleet_array, /*is_first_sync=*/false);
  }
}

namespace json
{
  static void battle_result_headers(const nlohmann::json& section)
  {
    http::logging::trace("PROCESS", "battle headers", STR_FORMAT("Processing {} battle headers", section.size()));

    std::vector<uint64_t> battle_ids;
    battle_ids.reserve(section.size());

    for (const auto& battle : section) {
      const auto id = battle["id"].get<uint64_t>();
      battle_ids.push_back(id);
    }

    queue_battle_ids(battle_ids);
  }

  static void resources(const nlohmann::json& section)
  {
    using json = nlohmann::json;
    using trackers::resource_states;
    using trackers::resource_states_mtx;
    static std::atomic_bool is_first_sync{true};

    http::logging::trace("PROCESS", "resources", STR_FORMAT("Processing {} resources (JSON)", section.size()));

    auto resource_array = json::array();
    {
      std::scoped_lock lk(resource_states_mtx);

      for (const auto& [str_id, resource] : section.get<json::object_t>()) {
        auto id     = std::stoll(str_id);
        auto amount = resource["current_amount"].get<int64_t>();

        if (const auto& it = resource_states.find(id); it == resource_states.end() || it->second != amount) {
          resource_states[id] = amount;
          resource_array.push_back({{"type", SyncConfig::Type::Resources}, {"rid", id}, {"amount", amount}});
        }
      }
    }

    if (!resource_array.empty()) {
      const bool first_sync = is_first_sync.exchange(false, std::memory_order_acq_rel);
      workers::queue_data(SyncConfig::Type::Resources, resource_array, first_sync);
    }
  }

  static void starbase_modules(const nlohmann::json& section)
  {
    using json = nlohmann::json;
    using trackers::module_states;
    using trackers::module_states_mtx;

    http::logging::trace("PROCESS", "starbase modules", STR_FORMAT("Processing {} buildings (JSON)", section.size()));

    auto starbase_array = json::array();
    {
      std::scoped_lock lk(module_states_mtx);

      for (const auto& module : section.get<json::object_t>() | std::views::values) {
        const auto id    = module["id"].get<int64_t>();
        const auto level = module["level"].get<int32_t>();

        if (const auto& it = module_states.find(id); it == module_states.end() || it->second != level) {
          module_states[id] = level;
          starbase_array.push_back({{"type", SyncConfig::Type::Buildings}, {"bid", id}, {"level", level}});
        }
      }
    }

    if (!starbase_array.empty()) {
      workers::queue_data(SyncConfig::Type::Buildings, starbase_array);
    }
  }

  static void ships(const nlohmann::json& section)
  {
    using json = nlohmann::json;

    http::logging::trace("PROCESS", "ships", STR_FORMAT("Processing {} ships (JSON)", section.size()));

    std::vector<ShipRecord> records;
    records.reserve(section.size());

    for (const auto& ship : section.get<json::object_t>() | std::views::values) {
      records.push_back({ship["id"].get<int64_t>(), ship["hull_id"].get<int64_t>(), ship["tier"].get<int32_t>(),
                         ship["level"].get<int32_t>(), ship["level_percentage"].get<double_t>(),
                         ship["components"].get<std::vector<int64_t>>()});
    }

    sync_ships(records);
  }

  static void parse(std::unique_ptr<std::string>&& bytes)
  {
    using json = nlohmann::json;
    const auto& sync_options = Config::Get().sync_options;

    try {
      const auto result = json::parse(bytes->begin(), bytes->end());

      for (const auto& [key, section] : result.items()) {
        if (key == "battle_result_headers") {
          if (!sync_options.battlelogs) {
            continue;
          }

          battle_result_headers(section);

        } else if (key == "resources") {
          if (!sync_options.resources) {
            continue;
          }

          resources(section);

        } else if (key == "starbase_modules") {
          if (!sync_options.buildings) {
            continue;
          }

          starbase_modules(section);

        } else if (key == "ships") {
          if (!sync_options.ships) {
            continue;
          }

          ships(section);
        }
      }
    } catch (const json::exception& e) {
      spdlog::error("Error parsing json: {}", e.what());
    }
  }
} // namespace json

namespace rtc
{
  static void entity_slots(std::unique_ptr<std::string>&& json_payload)
  {
    using json = nlohmann::json;

    try {
      auto data = json::parse(*json_payload);
      http::logging::trace("PROCESS", "entity slots (RTC)", "Processing entity slot update");

      const auto item    = data["slot_item_id"];
      const auto item_id = item.is_null() ? -1 : item.get<int64_t>();

      json    slot_params;
      int64_t state_value = item_id;

      const auto type = data["slot_type"].get<int32_t>();
      switch (type) {
        case Digit::PrimeServer::Models::SLOTTYPE_CONSUMABLE:
          if (const auto& expiry_time = data["consumable_slot_params"]["expiry_time"]; expiry_time.is_null()) {
            slot_params["expiry_time"] = nullptr;
          } else {
            const auto timestamp =
                parse_timestamp(data["consumable_slot_params"]["expiry_time"].get_ref<const std::string&>());
            if (timestamp.has_value()) {
              slot_params["expiry_time"] = timestamp.value().time_since_epoch().count();
            } else {
              slot_params["expiry_time"] = nullptr;
            }
          }
          break;
        case Digit::PrimeServer::Models::SLOTTYPE_OFFICERPRESET:
          if (const auto& preset = data["officer_preset_slot_params"]; !preset.is_null()) {
            slot_params = preset;
            state_value = static_cast<int64_t>(std::hash<json>{}(slot_params));
          }
          break;
        case Digit::PrimeServer::Models::SLOTTYPE_FLEETCOMMANDER:
          if (const auto& params = data["fleet_commander_slot_params"]; !params.is_null()) {
            slot_params["order"] = params["order"];
          }
          break;
        case Digit::PrimeServer::Models::SLOTTYPE_SELECTABLESKILL:
          if (const auto& params = data["selectable_skill_slot_params"]; !params.is_null()) {
            if (const auto& expiry_time = params["cooldown_expiration"]; expiry_time.is_null()) {
              slot_params["cooldown_expiration"] = nullptr;
            } else {
              const auto timestamp = parse_timestamp(params["cooldown_expiration"].get_ref<const std::string&>());
              if (timestamp.has_value()) {
                slot_params["cooldown_expiration"] = timestamp.value().time_since_epoch().count();
              } else {
                slot_params["cooldown_expiration"] = nullptr;
              }
            }
          }
          break;
        case Digit::PrimeServer::Models::SLOTTYPE_FORBIDDENTECH:
          if (const auto& tech = data["forbidden_tech_slot_params"]; !tech.is_null()) {
            slot_params = tech;
            state_value = static_cast<int64_t>(std::hash<json>{}(slot_params));
          }
          break;
        case Digit::PrimeServer::Models::SLOTTYPE_FLEETPRESET:
          if (const auto& params = data["fleet_preset_slot_params"]; !params.is_null()) {
            auto setup_json = json::array();
            for (const auto& setup : params["setups"]) {
              setup_json.push_back(
                  {{"drydock_id", setup["d"]}, {"ship_id", setup["s"][0]}, {"officer_ids", setup["o"]}});
            }

            slot_params = {{"name", params["name"]}, {"order", params["order"]}, {"setup", setup_json}};
            state_value = static_cast<int64_t>(std::hash<json>{}(slot_params));
          }
          break;
        default:
          return;
      }

      const auto id = data["slot_id"].get<int64_t>();
      {
        using trackers::slot_states;
        using trackers::slot_states_mtx;

        std::scoped_lock lk(slot_states_mtx);

        if (const auto& it = slot_states.find(id); it == slot_states.end() || it->second != state_value) {
          slot_states[id] = state_value;

          auto slot_array = json::array();
          slot_array.push_back({{"type", SyncConfig::Type::Slots},
                                {"sid", id},
                                {"slot_type", type},
                                {"spec_id", data["slot_spec_id"]},
                                {"item_id", item},
                                {"params", slot_params}});

          workers::queue_data(SyncConfig::Type::Slots, slot_array);
        }
      }
    } catch (json::exception& e) {
      spdlog::error("Failed to parse slots JSON: {}", e.what());
    }
  }
} // namespace rtc

} // namespace processors

static void HandleEntityGroup(EntityGroup* entity_group)
{
  if (entity_group == nullptr || entity_group->Group == nullptr || entity_group->Group->bytes == nullptr
      || entity_group->Group->Length <= 0) {
    return;
  }

  const auto byteCount = static_cast<size_t>(entity_group->Group->Length);
  const auto *bytesPtr = reinterpret_cast<const char*>(entity_group->Group->bytes->m_Items);

  // Helper to run processing asynchronously with exception handling
  auto submit_async = [bytesPtr, byteCount]<typename T>(T&& func) {
    auto payload = std::make_unique<std::string>(bytesPtr, byteCount);

    try {
      std::thread([f = std::forward<T>(func), p = std::move(payload)]() mutable {
        try {
          f(std::move(p));
        } catch (const std::exception& e) {
          spdlog::error("Exception in HandleEntityGroup: {}", e.what());
        } catch (...) {
          spdlog::error("Unknown exception in HandleEntityGroup");
        }
      }).detach();
    } catch (const std::exception& e) {
      spdlog::error("Failed to spawn async task: {}", e.what());
    } catch (...) {
      spdlog::error("Failed to spawn async task: unknown exception");
    }
  };

  const auto& sync_options = Config::Get().sync_options;

  switch (entity_group->Type_) {
    // away assignments
    case EntityGroup::Type::AwayAssignmentsList:
      if (sync_options.away_assignments) {
        submit_async(processors::away_assignments_list);
      }
      break;
    case EntityGroup::Type::AwayAssignmentsInstance:
      if (sync_options.away_assignments) {
        submit_async(processors::away_assignment_instance);
      }
      break;

    // battlelogs
    case EntityGroup::Type::BattleResultHeaders:
      if (sync_options.battlelogs) {
        submit_async(processors::battle_result_headers);
      }
      break;
    case EntityGroup::Type::BattleReport:
      if (sync_options.battlelogs) {
        submit_async(processors::battle_report);
      }
      break;
    case EntityGroup::Type::UserProfiles:
      if (sync_options.battlelogs) {
        submit_async(trackers::names::cache_player_names);
      }
      if (sync_options.profile) {
        // Skipped if the local profile is not ready; a later response retries.
        auto* const local = processors::local_user_profile();
        if (auto self = processors::local_user_id(local); !self.empty()) {
          submit_async([self = std::move(self), level = local->Level](std::unique_ptr<std::string>&& bytes) {
            processors::own_profile(std::move(bytes), self, level);
          });
        }
      }
      break;
    case EntityGroup::Type::AllianceProfiles:
      if (sync_options.battlelogs) {
        submit_async(trackers::names::cache_alliance_names);
      }
      break;

    // buffs
    case EntityGroup::Type::GlobalActiveBuffs:
      if (sync_options.buffs) {
        submit_async(processors::global_active_buffs);
      }
      break;
    case EntityGroup::Type::AllianceGetGameProperties:
      if (sync_options.buffs) {
        submit_async(processors::alliance_games_props);
      }
      break;

    // buildings
    case EntityGroup::Type::StarbaseModules:
      if (sync_options.buildings) {
        submit_async(processors::starbase_modules);
      }
      break;

    // haven
    case EntityGroup::Type::PlanetaryBaseData:
      if (sync_options.haven) {
        static std::once_flag haven_init_base_flag;
        std::call_once(haven_init_base_flag, [&] {
          submit_async(processors::planetary_base_data);
        });
      }
      break;

    case EntityGroup::Type::PlanetaryMapData:
      if (sync_options.haven) {
        static std::once_flag haven_init_map_flag;
        std::call_once(haven_init_map_flag, [&] {
          submit_async(processors::planetary_map_data);
        });
      }
      break;

    case EntityGroup::Type::PlanetaryMapProsperity:
      if (sync_options.haven) {
        static std::once_flag haven_init_prosperity_flag;
        std::call_once(haven_init_prosperity_flag, [&] {
          submit_async(processors::planetary_map_prosperity);
        });
      }
      break;

    case EntityGroup::Type::PlanetaryMapResponse:
      if (sync_options.haven) {
        submit_async(processors::planetary_map);
      }
      break;

    // inventory
    case EntityGroup::Type::PlayerInventories:
      if (sync_options.inventory) {
        submit_async(processors::player_inventories);
      }
      break;

    // jobs
    case EntityGroup::Type::Jobs:
      if (sync_options.jobs) {
        submit_async(processors::jobs);
      }
      break;

    // missions
    case EntityGroup::Type::ActiveMissions:
      if (sync_options.missions) {
        submit_async(processors::active_missions);
      }
      break;
    case EntityGroup::Type::CompletedMissions:
      if (sync_options.missions) {
        submit_async(processors::completed_missions);
      }
      break;

    // officer
    case EntityGroup::Type::Officers:
      if (sync_options.officers) {
        submit_async(processors::officers);
      }
      break;

    // research
    case EntityGroup::Type::ResearchTreesState:
      if (sync_options.research) {
        submit_async(processors::research_trees_state);
      }
      break;

    // resources
    case EntityGroup::Type::Resources:
      if (sync_options.resources) {
        submit_async(processors::resources);
      }
      break;
    case EntityGroup::Type::ResourcesDelta:
      if (sync_options.resources) {
        submit_async(processors::resources_delta);
      }
      break;
    case EntityGroup::Type::AllianceGetBankResources:
      if (sync_options.resources) {
        submit_async(processors::alliance_resources);
      }
      break;
    case EntityGroup::Type::ResourcesDeltaAlliance:
      if (sync_options.resources) {
        submit_async(processors::alliance_resources_delta);
      }
      break;

    // ships
    case EntityGroup::Type::Ships:
      if (sync_options.ships) {
        submit_async(processors::player_ships);
      }
      break;

    // slots
    case EntityGroup::Type::EntitySlots:
      if (sync_options.slots) {
        submit_async(processors::entity_slots);
      }
      break;
    case EntityGroup::Type::EntitySlotsData:
      if (sync_options.slots) {
        submit_async(processors::entity_slots_data);
      }
      break;

    // tech
    case EntityGroup::Type::ForbiddenTechs:
    if (sync_options.tech) {
      submit_async(processors::forbidden_tech);
    }
    break;

    // traits
    case EntityGroup::Type::ActiveOfficerTraits:
      if (sync_options.traits) {
        submit_async(processors::active_officer_traits);
      }
      break;

    // fleets
    case EntityGroup::Type::MyDeployedFleets:
      if (sync_options.fleets) {
        submit_async(processors::my_deployed_fleets);
      }
      break;

    // misc
    case EntityGroup::Type::Json:
      submit_async(processors::json::parse);
      break;

    default:
      break;
  }
}

// Platform responses (store offers among them) reach the same ProcessResultInternal detour as game server responses:
// both model registries share one compiled method. Their entity group types use the Platform enum, so they must not
// go through HandleEntityGroup.
static bool IsPlatformResponse(ServiceResponse* service_response)
{
  static const Il2CppClass* platform_class = nullptr;

  const auto* const response = reinterpret_cast<Il2CppObject*>(service_response);
  if (response == nullptr || response->klass == nullptr) {
    return false;
  }
  if (response->klass == platform_class) {
    return true;
  }

  const char* const name_space = il2cpp_class_get_namespace(response->klass);
  const char* const name       = il2cpp_class_get_name(response->klass);
  if (name_space != nullptr && name != nullptr && std::strcmp(name_space, "Digit.Platform.Models") == 0
      && std::strcmp(name, "ServiceResponse") == 0) {
    platform_class = response->klass;
    return true;
  }

  return false;
}

static void HandlePlatformResponse(ServiceResponse* service_response)
{
  auto* const        response       = reinterpret_cast<Il2CppObject*>(service_response);
  static auto* const response_class = response->klass;
  static auto        entity_groups  = IL2CppClassHelper{response_class}.GetProperty("EntityGroups");
  if (response->klass != response_class) {
    return;
  }

  auto* const groups = entity_groups.GetRaw<RepeatedField_EntityGroup>(response);
  if (groups == nullptr) {
    return;
  }

  for (int i = 0; i < groups->Count; ++i) {
    auto* const group = reinterpret_cast<Il2CppObject*>(groups->get_Item(i));
    if (group == nullptr || group->klass == nullptr) {
      continue;
    }

    static auto* const group_class = group->klass;
    static auto        group_type  = IL2CppClassHelper{group_class}.GetProperty("Type");
    static auto        group_bytes = IL2CppClassHelper{group_class}.GetProperty("Group");
    if (group->klass != group_class) {
      continue;
    }

    const auto* const type = group_type.Get<int32_t>(group);
    if (type == nullptr || *type != Digit::Platform::Models::EntityGroup::TYPE_STOREOFFERS) {
      continue;
    }

    auto* const bytes = group_bytes.GetRaw<ByteString>(group);
    if (bytes == nullptr || bytes->bytes == nullptr || bytes->Length <= 0) {
      continue;
    }

    auto payload = std::make_unique<std::string>(reinterpret_cast<const char*>(bytes->bytes->m_Items),
                                                 static_cast<size_t>(bytes->Length));
    try {
      std::thread([p = std::move(payload)]() mutable {
        try {
          processors::store_offers(std::move(p));
        } catch (const std::exception& e) {
          spdlog::error("Exception in HandlePlatformResponse: {}", e.what());
        } catch (...) {
          spdlog::error("Unknown exception in HandlePlatformResponse");
        }
      }).detach();
    } catch (const std::exception& e) {
      spdlog::error("Failed to spawn async task: {}", e.what());
    } catch (...) {
      spdlog::error("Failed to spawn async task: unknown exception");
    }
  }
}

namespace fleets
{
constexpr int kFleetSlotCount = 10;

static const char* state_name(FleetState state)
{
  switch (state) {
    case FleetState::IdleInSpace:
      return "IdleInSpace";
    case FleetState::Docked:
      return "Docked";
    case FleetState::Mining:
      return "Mining";
    case FleetState::Destroyed:
      return "Destroyed";
    case FleetState::TieringUp:
      return "TieringUp";
    case FleetState::Repairing:
      return "Repairing";
    case FleetState::Battling:
      return "Battling";
    case FleetState::WarpCharging:
      return "WarpCharging";
    case FleetState::Warping:
      return "Warping";
    case FleetState::Impulsing:
      return "Impulsing";
    case FleetState::Capturing:
      return "Capturing";
    case FleetState::AutoHunting:
      return "AutoHunting";
    case FleetState::Outposting:
      return "Outposting";
    default:
      return "Unknown";
  }
}

static int64_t unix_seconds()
{
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

template <typename T> static void put(nlohmann::json& out, const char* key, const std::optional<T>& value)
{
  if (value) {
    out[key] = *value;
  }
}

// Game thread only. Missing members are left out.
static void add_live_state(nlohmann::json& out, FleetPlayerData* fleet, FleetState state)
{
  const auto is_mining    = fleet->IsMining();
  const auto is_repairing = fleet->IsRepairing();

  const auto is_home      = fleet->IsHome();
  const auto is_destroyed = fleet->IsDestroyed();

  put(out, "is_home", is_home);
  put(out, "is_deployed", fleet->IsDeployed());
  put(out, "is_mining", is_mining);
  put(out, "is_repairing", is_repairing);
  put(out, "is_damaged", fleet->IsDamaged());
  put(out, "is_destroyed", is_destroyed);
  put(out, "drydock", fleet->DrydockID());
  // the client only updates this at the station; in space the server's hull points are the live value
  if (is_home.value_or(false) || is_destroyed.value_or(false)) {
    if (const auto damage = fleet->HullDamage()) {
      out["hull_damage"] = std::llround(*damage);
    }
  }
  put(out, "cargo_fill", fleet->CargoResourceFillLevel());

  if (auto* hold = fleet->GetCargoHoldData()) {
    if (auto* current = hold->CurrentCargo()) {
      put(out, "cargo", current->CurrentValue());
      put(out, "cargo_max", current->MaxValue());
    }
    if (auto* protected_cargo = hold->ProtectedCargoProgress()) {
      put(out, "protected_cargo", protected_cargo->CurrentValue());
    }
  }

  if (is_mining.value_or(false)) {
    if (auto* slot = fleet->MiningData()) {
      put(out, "node_id", slot->ParentId());
      put(out, "node_resource", slot->ResourceId());
      put(out, "amount_mined", slot->AmountMined());
      put(out, "occupied_since", ManagedRead::DateTimeToUnix(slot->OccupyTimeUtc()));
      if (auto* node = slot->PointData()) {
        put(out, "node_amount", node->Amount());
      }
    }
    put(out, "mining_rate", fleet->GetMiningRate());

    if (auto* timer = fleet->MiningSlotTimer()) {
      put(out, "mining_due", ManagedRead::DateTimeToUnix(timer->DueTime()));
    }
  }

  if (is_repairing.value_or(false)) {
    if (auto* timer = fleet->Timer()) {
      put(out, "repair_due", ManagedRead::DateTimeToUnix(timer->DueTime()));
    }
  }

  if (state == FleetState::WarpCharging || state == FleetState::Warping || state == FleetState::Impulsing) {
    if (auto* timer = fleet->Timer()) {
      put(out, "travel_due", ManagedRead::DateTimeToUnix(timer->DueTime()));
    }
  }
}

// Game thread only. The values the client fills in late, as last sent per fleet; a re-read is only sent when they
// changed. Values that move on their own are left out: cargo while mining, hull while repairing.
static std::unordered_map<uint64_t, size_t> sent_late;

static size_t late_fields(const nlohmann::json& record)
{
  auto fields = nlohmann::json::object();
  for (const auto* key :
       {"is_damaged", "is_destroyed", "is_repairing", "repair_due", "travel_due", "hull_damage", "mining_due", "node_id",
        "cargo_max"}) {
    if (const auto it = record.find(key); it != record.end()) {
      fields[key] = *it;
    }
  }
  if (record.value("is_repairing", false)) {
    fields.erase("hull_damage");
  }
  return std::hash<nlohmann::json>{}(fields);
}

// Game thread only: reads IL2CPP properties of a FleetPlayerData that is valid for the current frame.
static nlohmann::json record(int slot, FleetPlayerData* fleet, FleetState state, FleetState previous)
{
  auto*         hull    = fleet->Hull;
  // HullSpec::Id is read as a 32-bit long; hull ids run past 2^31 (e.g. Kumari 3005922948), so take
  // the value as unsigned before widening or those come out negative in the record.
  const int64_t hull_id = hull ? static_cast<int64_t>(static_cast<uint32_t>(hull->Id)) : 0;

  nlohmann::json out = {{"type", SyncConfig::Type::Fleets},
                        {"fid", fleet->Id},
                        {"slot", slot},
                        {"hull_id", hull_id},
                        {"level", fleet->Level},
                        {"state", static_cast<int>(state)},
                        {"state_name", state_name(state)},
                        {"prev_state", static_cast<int>(previous)},
                        {"prev_state_name", state_name(previous)},
                        {"t", unix_seconds()},
                        {"src", "client"}};
  try {
    add_live_state(out, fleet, state);
  } catch (const std::exception& e) {
    spdlog::warn("Sync: fleet live state read failed for {}: {}", fleet->Id, e.what());
  }

  sent_late[fleet->Id] = late_fields(out);
  return out;
}

// Game thread only. A snapshot is the complete list of occupied slots, so it is sent as a first sync.
static void queue_snapshot(const char* reason)
{
  auto* manager = FleetsManager::Instance();
  if (!manager || !manager->HasFleetService()) {
    return;
  }

  auto fleet_array = nlohmann::json::array();
  for (int slot = 0; slot < kFleetSlotCount; ++slot) {
    if (auto* fleet = manager->GetFleetPlayerData(slot); fleet) {
      fleet_array.push_back(record(slot, fleet, fleet->CurrentState, fleet->PreviousState));
    }
  }

  if (!fleet_array.empty()) {
    http::logging::debug("PROCESS", "fleets", STR_FORMAT("Fleet snapshot ({}): {} fleets", reason, fleet_array.size()));
    workers::queue_data(SyncConfig::Type::Fleets, fleet_array, true);
  }
}

static void on_transition(const fleet_watch::Transition& transition)
{
  if (!transition.fleet) {
    return;
  }

  auto fleet_array = nlohmann::json::array();
  fleet_array.push_back(
      record(transition.after.slot, transition.fleet, transition.after.state, transition.before.state));
  workers::queue_data(SyncConfig::Type::Fleets, fleet_array);
}

static void on_baseline()
{
  snapshot_requested.store(false, std::memory_order_relaxed);
  queue_snapshot("baseline");
}

// Game thread only.
static void reread_requested()
{
  std::vector<uint64_t> due;
  {
    std::scoped_lock lk(reread_mtx);
    if (reread_at.empty()) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(reread_at, [&](const auto& entry) {
      if (entry.second > now) {
        return false;
      }
      due.push_back(entry.first);
      return true;
    });
  }
  if (due.empty()) {
    return;
  }

  auto* manager = FleetsManager::Instance();
  if (!manager || !manager->HasFleetService()) {
    return;
  }

  auto fleet_array = nlohmann::json::array();
  for (int slot = 0; slot < kFleetSlotCount; ++slot) {
    try {
      auto* fleet = manager->GetFleetPlayerData(slot);
      if (!fleet || std::ranges::find(due, fleet->Id) == due.end()) {
        continue;
      }
      const auto before = sent_late[fleet->Id];
      auto       entry  = record(slot, fleet, fleet->CurrentState, fleet->PreviousState);
      if (late_fields(entry) != before) {
        fleet_array.push_back(std::move(entry));
      }
    } catch (const std::exception& e) {
      spdlog::warn("Sync: fleet re-read failed for slot {}: {}", slot, e.what());
    }
  }

  if (!fleet_array.empty()) {
    workers::queue_data(SyncConfig::Type::Fleets, fleet_array);
  }
}

static void tick()
{
  reread_requested();

  if (!snapshot_requested.load(std::memory_order_relaxed)
      || !snapshot_requested.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  queue_snapshot("first sync");
}

static void install()
{
  if (!Config::Get().sync_options.fleets) {
    return;
  }

  if (!fleet_watch::Subscribe({.on_transition = on_transition, .on_baseline = on_baseline})) {
    spdlog::warn("Sync: fleet state subscriber could not be installed; fleet sync disabled");
    return;
  }

  register_screen_manager_update_callback(tick);
  spdlog::info("Sync: fleet state sync enabled");
}
} // namespace fleets

namespace hooks
{
// IL2CPP value type: keep this parameter by value so following arguments retain their platform ABI positions.
struct CentrifugoInfo {
  Il2CppString* Channel;
  int32_t       Offset;
};

static_assert(sizeof(CentrifugoInfo) == 16);

static void* RtcParser_ParseFinalPayload(auto original, void* _this, CentrifugoInfo centrifugoInfo,
                                         RealtimeDataPayload* realtimeDataPayload, void* finalPayload)
{
  if (realtimeDataPayload != nullptr && realtimeDataPayload->Target != nullptr
      && realtimeDataPayload->DataType != nullptr && realtimeDataPayload->Data != nullptr) {
    if (const auto type_string = to_string(realtimeDataPayload->DataType); std::stoi(type_string) == DataType::JSON) {
      const auto target   = to_string(realtimeDataPayload->Target);
      const auto rtc_data = to_string(realtimeDataPayload->Data);

      // spdlog::debug("Received RTC payload for target '{}': {}", target, rtc_data);
      auto payload = std::make_unique<std::string>(rtc_data);

      std::thread([p = std::move(payload)]() mutable {
        try {
          processors::rtc::entity_slots(std::move(p));
        } catch (const std::exception& e) {
          spdlog::error("Exception in RtcParser_ParseFinalPayload: {}", e.what());
        } catch (...) {
          spdlog::error("Unknown exception in RtcParser_ParseFinalPayload");
        }
      }).detach();
    }
  }

  return original(_this, centrifugoInfo, realtimeDataPayload, finalPayload);
}

static void GameServerModelRegistry_ProcessResultInternal(auto original, void* _this, void* parsing_context,
                                                          ServiceResponse* service_response, MethodInfo* method)
{
  // Only the store offers option looks at Platform responses; everyone else keeps the unchanged path below.
  if (Config::Get().sync_options.store_offers && IsPlatformResponse(service_response)) {
    HandlePlatformResponse(service_response);
    return original(_this, parsing_context, service_response, method);
  }

  auto* const entity_groups = service_response->EntityGroups;
  for (int i = 0; i < entity_groups->Count; ++i) {
    auto* const entity_group = entity_groups->get_Item(i);
    HandleEntityGroup(entity_group);
  }

  return original(_this, parsing_context, service_response, method);
}

static void PrimeApp_InitPrimeServer(auto original, void* _this, Il2CppString* gameServerUrl,
                                     Il2CppString* gatewayServerUrl, Il2CppString* sessionId,
                                     Il2CppString* serverRegion)
{
  original(_this, gameServerUrl, gatewayServerUrl, sessionId, serverRegion);
  http::headers::instanceSessionId = to_string(sessionId);
  http::headers::gameServerUrl     = to_string(gameServerUrl);
}

static void GameServer_Initialise(auto original, void* _this, Il2CppString* sessionId, Il2CppString* gameVersion,
                                  bool encryptRequests, Il2CppString* serverRegion)
{
  original(_this, sessionId, gameVersion, encryptRequests, serverRegion);
  http::headers::primeVersion = to_string(gameVersion);
}

static void GameServer_SetInstanceIdHeader(auto original, void* _this, int32_t instanceId)
{
  original(_this, instanceId);
  http::headers::instanceId = instanceId;
}

} // namespace hooks

void InstallSyncPatches()
{
  trackers::load_previously_sent_logs();

  if (auto game_server_model_registry =
          il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Core", "GameServerModelRegistry");
      !game_server_model_registry.isValidHelper()) {
    ErrorMsg::MissingHelper("Core", "GameServerModelRegistry");
  } else {
    if (auto *const ptr = game_server_model_registry.GetMethod("ProcessResultInternal"); ptr == nullptr) {
      ErrorMsg::MissingMethod("GameServerModelRegistry", "ProcessResultInternal");
    } else {
      SPUD_STATIC_DETOUR(ptr, hooks::GameServerModelRegistry_ProcessResultInternal);
    }
  }

  if (auto prime_app = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.Core", "PrimeApp");
      !prime_app.isValidHelper()) {
    ErrorMsg::MissingHelper("Core", "PrimeApp");
  } else {
    if (auto *const ptr = prime_app.GetMethod("InitPrimeServer"); ptr == nullptr) {
      ErrorMsg::MissingMethod("PrimeApp", "InitPrimeServer");
    } else {
      SPUD_STATIC_DETOUR(ptr, hooks::PrimeApp_InitPrimeServer);
    }
  }

  if (auto game_server =
          il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Core", "GameServer");
      !game_server.isValidHelper()) {
    ErrorMsg::MissingHelper("Core", "GameServer");
  } else {
    // This hook only captures the game version for the X-PRIME-VERSION sync header
    // (headers::primeVersion has a static fallback). It is optional so users can turn
    // it off when another mod detours the same method — a native and a managed detour
    // on one prologue corrupt each other and crash at login.
    if (!Config::Get().installGameVersionHook) {
      spdlog::info("Sync: skipping GameServer::Initialise hook (patches.game_version = false)");
    } else if (auto *const ptr = game_server.GetMethod("Initialise"); ptr == nullptr) {
      ErrorMsg::MissingMethod("GameServer", "Initialise");
    } else {
      SPUD_STATIC_DETOUR(ptr, hooks::GameServer_Initialise);
    }

    if (auto *const ptr = game_server.GetMethod("SetInstanceIdHeader"); ptr == nullptr) {
      ErrorMsg::MissingMethod("GameServer", "SetInstanceIdHeader");
    } else {
      SPUD_STATIC_DETOUR(ptr, hooks::GameServer_SetInstanceIdHeader);
    }
  }

  if (auto slot_assign_parser = il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Parsers", "SlotAssignRtcParser");
    !slot_assign_parser.isValidHelper()) {
    ErrorMsg::MissingHelper("Parsers", "SlotAssignRtcParser");
    } else {
      if (auto* const ptr = slot_assign_parser.GetMethod("ParseFinalPayload"); ptr == nullptr) {
        ErrorMsg::MissingMethod("SlotAssignRtcParser", "ParseFinalPayload");
      } else {
        SPUD_STATIC_DETOUR(ptr, hooks::RtcParser_ParseFinalPayload);
      }
    }

  if (auto slot_clear_parser = il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Parsers", "SlotClearRtcParser");
    !slot_clear_parser.isValidHelper()) {
    ErrorMsg::MissingHelper("Parsers", "SlotClearRtcParser");
    } else {
      if (auto* const ptr = slot_clear_parser.GetMethod("ParseFinalPayload"); ptr == nullptr) {
        ErrorMsg::MissingMethod("SlotClearRtcParser", "ParseFinalPayload");
      } else {
        SPUD_STATIC_DETOUR(ptr, hooks::RtcParser_ParseFinalPayload);
      }
    }

  fleets::install();

  std::thread(workers::ship_sync_data).detach();
  std::thread(workers::ship_combat_log_data).detach();
}

// Uploads to sync targets, one worker thread and HTTP session per target, and requests to the game server.

#include "patches/parts/sync/sync.h"

#include "version.h"

#include <cpr/cpr.h>
#include <curl/curl.h>
#include <spdlog/fmt/fmt.h>

#include <memory>
#include <ranges>
#include <thread>
#include <unordered_map>

#if _WIN32
#include <rpc.h>
#include <winrt/windows.foundation.h>
#else
#include <uuid/uuid.h>
#endif

namespace data_sync
{
namespace
{
  const std::string kPoweredBy = "stfc community mod/" VER_FILE_VERSION_STR;
  // The game's Unity build, sent as the game client sends it.
  const std::string kUnityVersion = "6000.0.59f2-deadlock-fix-test";

  // Bounds for queued uploads. Each entry is one JSON batch; at the 10 s request timeout a dead target falls
  // behind by a few batches per minute, so 256 covers a long outage before the oldest go.
  constexpr size_t kUploadQueueCapacity = 256;
  constexpr size_t kTargetQueueCapacity = 256;

#if _WIN32
  // Every thread that may touch WinRT through cpr's Windows backend needs its own apartment.
  struct WinRtApartment {
    WinRtApartment()
    { winrt::init_apartment(); }
    ~WinRtApartment()
    { winrt::uninit_apartment(); }
  };
#endif

  // Written by game hooks on the main thread and read by worker threads.
  struct GameSession {
    std::mutex  mutex;
    std::string server_url;
    std::string session_id;
    // Until the game reports its own version.
    std::string version     = "1.000.51336";
    int32_t     instance_id = 0;
  } session;

  struct Upload {
    SyncConfig::Type type;
    std::string      body;
    bool             first_sync;
  };

  BoundedQueue<Upload> uploads{"upload", kUploadQueueCapacity};

  struct Request {
    std::string label; // "target (type)"
    std::string body;
    bool        first_sync;
  };

  struct TargetWorker {
    cpr::Session          session;
    BoundedQueue<Request> requests{"target", kTargetQueueCapacity};
  };

  void LogFailure(std::string_view label, std::string_view what)
  { spdlog::error("Failed to send {} sync data: {}", label, what); }

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

  void RunTarget(TargetWorker& worker)
  {
#if _WIN32
    WinRtApartment apartment;
#endif
    for (;;) {
      auto request = worker.requests.Pop();
      try {
        auto& headers = worker.session.GetHeader();
        if (request.first_sync) {
          headers.insert_or_assign("X-PRIME-SYNC", "2");
          logging::trace("UPLOAD", request.label, "Adding X-Prime-Sync header for initial sync");
        } else {
          headers.erase("X-PRIME-SYNC");
        }
        worker.session.SetBody(cpr::Body{request.body});
        logging::debug("UPLOAD", request.label, "Sending data to " + worker.session.GetFullRequestUrl());

        const auto response = worker.session.Post();
        if (response.status_code == 0) {
          logging::error("UPLOAD", request.label, "Failed to send request: " + response.error.message);
        } else if (response.status_code == 501) {
          logging::error("UPLOAD", request.label,
                         fmt::format("Sync target does not support this operation ({}, after {:.0f} ms). Consider "
                                     "turning off this upload category for the target.",
                                     response.status_line, response.elapsed * 1'000));
        } else if (response.status_code >= 400) {
          logging::error("UPLOAD", request.label,
                         fmt::format("Failed to communicate with server: {} (after {:.0f} ms)", response.status_line,
                                     response.elapsed * 1'000));
        } else {
          logging::debug(
              "UPLOAD", request.label,
              fmt::format("Response: {} ({:.0f} ms elapsed)", response.status_line, response.elapsed * 1'000));
        }
#if _WIN32
      } catch (const winrt::hresult_error& error) {
        LogFailure(request.label, winrt::to_string(error.message()));
#endif
      } catch (const std::exception& error) {
        LogFailure(request.label, error.what());
      } catch (...) {
        LogFailure(request.label, "Unknown error occurred");
      }
    }
  }

  // Workers live for the process; the game exits without telling the mod, so there is nothing to join.
  TargetWorker& WorkerFor(const std::string& name)
  {
    static std::unordered_map<std::string, std::unique_ptr<TargetWorker>> workers;
    static std::mutex                                                     workers_mutex;

    std::scoped_lock lock(workers_mutex);
    if (auto it = workers.find(name); it != workers.end())
      return *it->second;

    const auto& target = Config::Get().sync_targets.at(name);
    auto        worker = std::make_unique<TargetWorker>();
    auto&       http   = worker->session;
    http.SetUrl(target.url);
    http.SetUserAgent("stfc community mod " VER_FILE_VERSION_STR " (libcurl/" LIBCURL_VERSION ")");
    http.SetAcceptEncoding(cpr::AcceptEncoding{});
    http.SetHttpVersion(cpr::HttpVersion{cpr::HttpVersionCode::VERSION_1_1});
    http.SetRedirect(cpr::Redirect{3, true, false, cpr::PostRedirectFlags::POST_ALL});
#ifndef _MODDBG
    // Debug builds wait indefinitely so a target can be stepped through in a debugger.
    http.SetConnectTimeout(cpr::ConnectTimeout{3'000});
    http.SetTimeout(cpr::Timeout{10'000});
#endif
    spdlog::debug("sync target '{}': proxy='{}', verify_ssl={}", name, target.proxy, target.verify_ssl);
    if (!target.proxy.empty())
      http.SetProxies({{"http", target.proxy}, {"https", target.proxy}});
    if (!target.verify_ssl)
      http.SetSslOptions(cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true}));
    http.SetHeader({
        {"Content-Type", "application/json"},
        {"X-Powered-By", kPoweredBy},
        {"stfc-sync-token", target.token},
    });

    auto& result = *worker;
    std::thread(RunTarget, std::ref(result)).detach();
    workers.emplace(name, std::move(worker));
    return result;
  }

  void SendToTargets(const Upload& upload)
  {
    const auto& targets = Config::Get().sync_targets;
    if (targets.empty()) {
      static std::once_flag warned;
      std::call_once(warned, [] { logging::warn("UPLOAD", "GLOBAL", "No target found, will not attempt to send"); });
      return;
    }

    for (const auto& [name, target] : targets) {
      if (!target.enabled(upload.type))
        continue;
      const auto label = fmt::format("{} ({})", name, to_string(upload.type));
      try {
        auto& worker = WorkerFor(name);
        worker.requests.Push({label, upload.body, upload.first_sync});
        logging::trace("UPLOAD", label, fmt::format("Queued request (queue size: {})", worker.requests.Size()));
      } catch (const std::exception& error) {
        spdlog::error("Failed to send sync data to target '{}': {}", label, error.what());
      }
    }
  }

  void RunUploads()
  {
#if _WIN32
    WinRtApartment apartment;
#endif
    for (;;) {
      const auto upload = uploads.Pop();
      try {
        SendToTargets(upload);
      } catch (const std::exception& error) {
        LogFailure(to_string(upload.type), error.what());
      } catch (...) {
        LogFailure(to_string(upload.type), "Unknown error during sending of sync data");
      }
    }
  }

  cpr::Session& GameServerSession()
  {
    static cpr::Session   http;
    static std::once_flag configured;
    std::call_once(configured, [] {
      const auto& options = Config::Get().sync_options;
      http.SetAcceptEncoding(cpr::AcceptEncoding{});
      http.SetHttpVersion(cpr::HttpVersion{cpr::HttpVersionCode::VERSION_1_1});
      spdlog::debug("scopely session: proxy='{}', verify_ssl={}", options.proxy, options.verify_ssl);
      if (!options.proxy.empty())
        http.SetProxies({{"https", options.proxy}});
      if (!options.verify_ssl)
        http.SetSslOptions(
            cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true}));
      http.SetUserAgent("UnityPlayer/" + kUnityVersion + " (UnityWebRequest/1.0, libcurl/8.10.1-DEV)");
      http.SetHeader({
          {"Accept", "application/json"},
          {"Content-Type", "application/json"},
          {"X-PRIME-SYNC", "0"},
          {"X-Unity-Version", kUnityVersion},
          {"X-Powered-By", kPoweredBy},
      });
    });
    return http;
  }
} // namespace

namespace logging
{
  void error(std::string_view type, std::string_view target, std::string_view text)
  {
    if (Config::Get().sync_logging)
      spdlog::error("SYNC-{} - {}: {}", type, target, text);
  }

  void warn(std::string_view type, std::string_view target, std::string_view text)
  {
    if (Config::Get().sync_logging)
      spdlog::warn("SYNC-{} - {}: {}", type, target, text);
  }

  void info(std::string_view type, std::string_view target, std::string_view text)
  {
    if (Config::Get().sync_logging)
      spdlog::info("SYNC-{} - {}: {}", type, target, text);
  }

  void debug(std::string_view type, std::string_view target, std::string_view text)
  {
    if (Config::Get().sync_logging && Config::Get().sync_debug)
      spdlog::debug("SYNC-{} - {}: {}", type, target, text);
  }

  void trace(std::string_view type, std::string_view target, std::string_view text)
  {
    if (Config::Get().sync_logging && Config::Get().sync_debug)
      spdlog::trace("SYNC-{} - {}: {}", type, target, text);
  }
} // namespace logging

void Queue(SyncConfig::Type type, const nlohmann::json& records, bool first_sync)
{
  // Another category's first sync: the targets get a full fleet snapshot with it.
  if (first_sync && type != SyncConfig::Type::Fleets)
    RequestFleetSnapshot();
  uploads.Push({type, records.dump(), first_sync});
  logging::debug("QUEUE", to_string(type), fmt::format("Added {} entries to sync queue", records.size()));
}

void StartUploads()
{ std::thread(RunUploads).detach(); }

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

std::string RequestGameData(const std::string& path, const std::string& body)
{
  if (Config::Get().sync_targets.empty()) {
    static std::once_flag warned;
    std::call_once(warned,
                   [] { logging::warn("UPLOAD", "GLOBAL", "No target found, will not attempt to retrieve data"); });
    return {};
  }

  std::string url, session_id, version;
  int32_t     instance_id;
  {
    std::scoped_lock lock(session.mutex);
    url         = WithPath(session.server_url, path);
    session_id  = session.session_id;
    version     = session.version;
    instance_id = session.instance_id;
  }

  // One session, one request at a time; only the battle-log thread calls this today.
  static std::mutex request_mutex;
  std::scoped_lock  lock(request_mutex);
  auto&             http    = GameServerSession();
  auto&             headers = http.GetHeader();
  http.SetUrl(url);
  headers.insert_or_assign("X-TRANSACTION-ID", NewUuid());
  headers.insert_or_assign("X-AUTH-SESSION-ID", session_id);
  headers.insert_or_assign("X-PRIME-VERSION", version);
  headers.insert_or_assign("X-Instance-ID", fmt::format("{:03}", instance_id));
  http.SetBody(body);

  const auto response = http.Post();
  if (response.status_code == 0) {
    logging::error("DOWNLOAD", path, "Failed to send request: " + response.error.message);
    return {};
  }
  if (response.status_code >= 400) {
    logging::error("DOWNLOAD", path, "Failed to communicate with server: " + response.status_line);
    return {};
  }

  const auto type = response.header.find("Content-Type");
  logging::debug("DOWNLOAD", path,
                 fmt::format("Response: {} ({}), {:.0f} ms elapsed,", response.status_line,
                             type == response.header.end() ? "unknown" : type->second, response.elapsed * 1'000));
  return response.text;
}
} // namespace data_sync

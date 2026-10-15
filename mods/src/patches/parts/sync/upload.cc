// Uploads to sync targets: one worker thread, HTTP session and bounded queue per target.

#include "patches/parts/sync/sync.h"
#include "version.h"

#include <cpr/cpr.h>
#include <curl/curl.h>
#include <spdlog/fmt/fmt.h>

#include <map>
#include <memory>
#include <thread>

namespace data_sync
{
namespace
{
  // Each entry is one JSON batch. At the 10 s request timeout a dead target falls behind by a few batches a minute,
  // so 256 covers a long outage before the oldest go.
  constexpr size_t kTargetQueueCapacity = 256;

  struct Request {
    SyncConfig::Type type;
    std::string      body;
    bool             first_sync;
  };

  class Target
  {
  public:
    Target(std::string name, const SyncTargetConfig& config)
        : name_(std::move(name))
        , config_(config)
    {
      Configure();
      std::thread(&Target::Run, this).detach();
    }

    void Push(Request request)
    { requests_.Push(std::move(request)); }

  private:
    // A fresh session for config_: one that had a proxy or relaxed TLS keeps them otherwise.
    void Configure()
    {
      const auto& config = config_;
      http_              = std::make_unique<cpr::Session>();
      auto& http         = *http_;
      http.SetUrl(config.url);
      http.SetUserAgent("stfc community mod " VER_FILE_VERSION_STR " (libcurl/" LIBCURL_VERSION ")");
      http.SetAcceptEncoding(cpr::AcceptEncoding{});
      http.SetHttpVersion(cpr::HttpVersion{cpr::HttpVersionCode::VERSION_1_1});
      http.SetRedirect(cpr::Redirect{3, true, false, cpr::PostRedirectFlags::POST_ALL});
#ifndef _MODDBG
      // Debug builds wait indefinitely so a target can be stepped through in a debugger.
      http.SetConnectTimeout(cpr::ConnectTimeout{3'000});
      http.SetTimeout(cpr::Timeout{10'000});
#endif
      spdlog::debug("sync target '{}': proxy='{}', verify_ssl={}", name_, config.proxy, config.verify_ssl);
      if (!config.proxy.empty())
        http.SetProxies({{"http", config.proxy}, {"https", config.proxy}});
      if (!config.verify_ssl)
        http.SetSslOptions(
            cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true}));
      http.SetHeader({
          {"Content-Type", "application/json"},
          {"X-Powered-By", "stfc community mod/" VER_FILE_VERSION_STR},
          {"stfc-sync-token", config.token},
      });
    }

    // A settings reload can change the target's address, token or proxy; the session follows before the next
    // upload. A target removed from the settings is no longer given uploads.
    void FollowSettings()
    {
      const auto& targets = Config::Get().sync.targets;
      const auto  it      = targets.find(name_);
      if (it == targets.end())
        return;
      const auto& now = it->second;
      if (now.url == config_.url && now.token == config_.token && now.proxy == config_.proxy
          && now.verify_ssl == config_.verify_ssl)
        return;
      config_ = now;
      Configure();
    }

    void Run()
    {
      for (;;) {
        auto request = requests_.Pop();
        FollowSettings();
        const auto label = fmt::format("{} ({})", name_, to_string(request.type));
        try {
          Send(label, std::move(request));
        } catch (const std::exception& error) {
          spdlog::error("Failed to send {} sync data: {}", label, error.what());
        } catch (...) {
          spdlog::error("Failed to send {} sync data: unknown error", label);
        }
      }
    }

    void Send(const std::string& label, Request request)
    {
      auto& headers = http_->GetHeader();
      if (request.first_sync) {
        headers.insert_or_assign("X-PRIME-SYNC", "2");
        logging::trace("UPLOAD", label, "Adding X-Prime-Sync header for initial sync");
      } else {
        headers.erase("X-PRIME-SYNC");
      }
      http_->SetBody(cpr::Body{std::move(request.body)});
      logging::debug("UPLOAD", label, "Sending data to {}", http_->GetFullRequestUrl());

      const auto response = http_->Post();
      const auto elapsed  = response.elapsed * 1'000;
      if (response.status_code == 0)
        logging::error("UPLOAD", label, "Failed to send request: {}", response.error.message);
      else if (response.status_code == 501)
        logging::error("UPLOAD", label,
                       "Sync target does not support this operation ({}, after {:.0f} ms). Consider turning off this "
                       "upload category for the target.",
                       response.status_line, elapsed);
      else if (response.status_code >= 400)
        logging::error("UPLOAD", label, "Failed to communicate with server: {} (after {:.0f} ms)", response.status_line,
                       elapsed);
      else
        logging::debug("UPLOAD", label, "Response: {} ({:.0f} ms elapsed)", response.status_line, elapsed);
    }

    std::string                   name_;
    SyncTargetConfig              config_;
    std::unique_ptr<cpr::Session> http_;
    BoundedQueue<Request>         requests_{"target", kTargetQueueCapacity};
  };

  // Started on first use, so a target added to the settings later still gets a worker. Never destroyed, see sync.h.
  Target& TargetFor(const std::string& name, const SyncTargetConfig& config)
  {
    static auto&     targets = *new std::map<std::string, Target*, std::less<>>;
    static auto&     mutex   = *new std::mutex;
    std::scoped_lock lock(mutex);
    auto&            target = targets[name];
    if (!target)
      target = new Target(name, config);
    return *target;
  }
} // namespace

void Queue(SyncConfig::Type type, const nlohmann::json& records, bool first_sync)
{
  // Another category's first sync: the targets get a full fleet snapshot with it.
  if (first_sync && type != SyncConfig::Type::kFleets)
    RequestFleetSnapshot();
  std::string body;
  for (const auto& [name, config] : Config::Get().sync.targets) {
    if (!config.enabled(type))
      continue;
    if (body.empty())
      body = records.dump();
    TargetFor(name, config).Push({type, body, first_sync});
  }
  logging::debug("QUEUE", to_string(type), "Added {} entries to sync queue", records.size());
}
} // namespace data_sync

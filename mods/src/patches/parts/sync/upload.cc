// One owner for canonical records, target cursors and session identity. HTTP runs outside its lock.
#include "patches/parts/sync/sync.h"
#include "version.h"
#include <cpr/cpr.h>
#include <curl/curl.h>
#include <thread>

namespace data_sync
{
namespace
{
  class Target;
  struct Owner {
    std::mutex                                     mutex;
    std::condition_variable                        ready;
    RecordStore                                    records;
    GameSession                                    session;
    std::map<std::string, std::unique_ptr<Target>> targets;
  };

  class Target
  {
  public:
    Target(Owner& owner, std::string name)
        : worker_([this, &owner, name = std::move(name)] { Run(owner, name); })
    {
    }
    Delivery delivery; // Accessed only under Owner::mutex.
  private:
    void Run(Owner& owner, const std::string& name)
    {
      std::unique_ptr<cpr::Session> http;
      uint64_t                      configured = 0;
      for (;;) {
        std::unique_lock lock(owner.mutex);
        auto             upload = delivery.Next(owner.records, Clock::now());
        if (!upload) {
          owner.ready.wait_for(lock, std::chrono::seconds(1));
          continue;
        }
        const auto config   = delivery.Config();
        const auto revision = delivery.Revision();
        lock.unlock();
        const auto label   = fmt::format("{} ({})", name, upload->Type());
        bool       success = false;
        try {
          if (!http || configured != revision) {
            http       = std::make_unique<cpr::Session>();
            configured = revision;
            http->SetUrl(config.url);
            http->SetUserAgent("stfc community mod " VER_FILE_VERSION_STR " (libcurl/" LIBCURL_VERSION ")");
            http->SetAcceptEncoding(cpr::AcceptEncoding{});
            http->SetHttpVersion(cpr::HttpVersion{cpr::HttpVersionCode::VERSION_1_1});
            http->SetRedirect(cpr::Redirect{3, true, false, cpr::PostRedirectFlags::POST_ALL});
#ifndef _MODDBG
            http->SetConnectTimeout(cpr::ConnectTimeout{3'000});
            http->SetTimeout(cpr::Timeout{10'000});
#endif
            if (!config.proxy.empty())
              http->SetProxies({{"http", config.proxy}, {"https", config.proxy}});
            if (!config.verify_ssl)
              http->SetSslOptions(
                  cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true}));
            http->SetHeader({{"Content-Type", "application/json"},
                             {"X-Powered-By", "stfc community mod/" VER_FILE_VERSION_STR},
                             {"stfc-sync-token", config.token}});
          }
          auto& headers = http->GetHeader();
          if (upload->first_sync)
            headers.insert_or_assign("X-PRIME-SYNC", "2");
          else
            headers.erase("X-PRIME-SYNC");
          const auto records = upload->Records();
          http->SetBody(cpr::Body{records.dump()});
          const auto response = http->Post();
          success             = response.status_code >= 200 && response.status_code < 300;
          if (!success)
            logging::error("UPLOAD", label, "Upload failed ({}): {}; retained for retry", response.status_code,
                           response.error.message);
          else
            logging::debug("UPLOAD", label, "Uploaded {} records in {:.0f} ms", records.size(),
                           response.elapsed * 1'000);
        } catch (const std::exception& e) {
          spdlog::error("Sync target {}: {}", label, e.what());
        }
        // A target took another category's first sync: fleet targets get a fresh fleet snapshot with it.
        if (success && upload->first_sync && upload->stream != Stream::kFleets)
          RequestFleetSnapshot();
        lock.lock();
        delivery.Complete(std::move(*upload), success, owner.records.generation, Clock::now());
      }
    }
    // The owner lives until process exit. Joinable workers belong to it and cannot outlive their storage.
    std::thread worker_;
  };
  Owner& Sync()
  {
    static auto& owner = *new Owner;
    return owner;
  }
} // namespace
void Publish(Stream stream, RecordSet records, uint64_t generation, Update mode)
{
  auto& owner = Sync();
  {
    std::scoped_lock lock(owner.mutex);
    if (generation != owner.records.generation)
      return;
    if (!owner.records.Apply(stream, std::move(records), mode))
      return;
  }
  owner.ready.notify_all();
}
bool HasBattle(uint64_t id, uint64_t generation)
{
  auto&            owner = Sync();
  std::scoped_lock lock(owner.mutex);
  return generation == owner.records.generation && owner.records.Contains(Stream::kBattles, id);
}
void FollowSettings()
{
  static const Config* applied = nullptr;
  const auto&          config  = Config::Get();
  if (applied == &config)
    return;
  applied     = &config;
  auto& owner = Sync();
  {
    std::scoped_lock lock(owner.mutex);
    for (auto& [name, target] : owner.targets) {
      const auto now = config.sync.targets.find(name);
      target->delivery.Configure(now == config.sync.targets.end() ? SyncTargetConfig{} : now->second);
    }
    for (const auto& [name, options] : config.sync.targets) {
      auto& target = owner.targets[name];
      if (!target)
        target = std::make_unique<Target>(owner, name);
      target->delivery.Configure(options);
    }
  }
  owner.ready.notify_all();
}
GameSession Session()
{
  auto&            owner = Sync();
  std::scoped_lock lock(owner.mutex);
  return owner.session;
}
void SetGameServer(std::string url, std::string session_id)
{
  auto& owner = Sync();
  {
    std::scoped_lock lock(owner.mutex);
    owner.records.Reset();
    owner.session.generation = owner.records.generation;
    owner.session.server_url = std::move(url);
    owner.session.session_id = std::move(session_id);
    for (const auto& [name, target] : owner.targets)
      target->delivery.Reset(owner.records.generation);
  }
  owner.ready.notify_all();
}
void SetGameVersion(std::string version)
{
  auto&            owner = Sync();
  std::scoped_lock lock(owner.mutex);
  owner.session.version = std::move(version);
}
void SetInstanceId(int32_t id)
{
  auto&            owner = Sync();
  std::scoped_lock lock(owner.mutex);
  owner.session.instance_id = id;
}
} // namespace data_sync

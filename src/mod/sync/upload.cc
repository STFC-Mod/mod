// Each sync target's upload thread: takes the next upload from its Delivery and POSTs it, outside the state lock.

#include "sync/sync.h"
#include "version.h"

#include <cpr/cpr.h>
#include <curl/curl.h>

#include <memory>

namespace data_sync
{
namespace
{
  // Set up once per target configuration; a reload that changes the target makes a new one. Owned by the target's
  // thread alone.
  std::unique_ptr<cpr::Session> NewTargetSession(const SyncTargetConfig& config)
  {
    auto http = std::make_unique<cpr::Session>();
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
      http->SetSslOptions(cpr::Ssl(cpr::ssl::VerifyHost{false}, cpr::ssl::VerifyPeer{false}, cpr::ssl::NoRevoke{true}));
    http->SetHeader({{"Content-Type", "application/json"},
                     {"X-Powered-By", "stfc community mod/" VER_FILE_VERSION_STR},
                     {"stfc-sync-token", config.token}});
    return http;
  }
} // namespace

// The registry owns the target; this thread only borrows it per upload, so a removed target finishes its request
// and then the thread ends, without the game thread joining it.
void UploadTarget(std::weak_ptr<Delivery> target, std::string name)
{
  auto&                         state = State();
  std::unique_ptr<cpr::Session> http;
  uint64_t                      configured = 0;
  for (;;) {
    std::unique_lock lock(state.mutex);
    const auto       delivery = target.lock();
    if (!delivery)
      return;
    auto upload = delivery->Next(state.records, Clock::now());
    if (!upload) {
      // New records notify; a retry coming due does not, so this also wakes every second to look again.
      state.ready.wait_for(lock, std::chrono::seconds(1));
      continue;
    }
    // Copied, because a reload can reconfigure the target while the request runs. Complete then sees the newer
    // revision and drops this upload's result.
    const auto config   = delivery->Config();
    const auto revision = delivery->Revision();
    lock.unlock();

    const auto label   = fmt::format("{} ({})", name, upload->Type());
    bool       success = false;
    try {
      if (!http || configured != revision) {
        http       = NewTargetSession(config);
        configured = revision;
      }
      auto& headers = http->GetHeader();
      if (upload->first_sync)
        headers.insert_or_assign("X-PRIME-SYNC", "2");
      else
        headers.erase("X-PRIME-SYNC");
      const auto records = upload->Records();
      // Game strings can hold invalid UTF-8 (a lone surrogate from a player name). Throwing on it would fail this
      // upload on every retry and block the stream for good, so it is replaced instead.
      http->SetBody(cpr::Body{records.dump(-1, ' ', false, json::error_handler_t::replace)});
      const auto response = http->Post();
      success             = response.status_code >= 200 && response.status_code < 300;
      if (success)
        logging::Debug("upload", label, "sent {} records in {:.0f} ms", records.size(), response.elapsed * 1'000);
      else
        logging::Error("upload", label, "failed ({}): {}; kept for retry", response.status_code,
                       response.error.message);
    } catch (const std::exception& e) {
      // Not behind [sync] logging: an exception here is a fault in the mod, not a target being unreachable.
      spdlog::error("[Sync] upload {} failed: {}; kept for retry", label, e.what());
    }
    // A target took another category's first sync: fleet targets get a fresh fleet snapshot with it.
    if (success && upload->first_sync && upload->stream != Stream::kFleets)
      RequestFleetSnapshot();
    lock.lock();
    delivery->Complete(std::move(*upload), success, state.records.Generation(), Clock::now());
  }
}
} // namespace data_sync

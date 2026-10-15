#pragma once

// Internal to data sync. Game data is diffed against what was last sent on one processing thread, queued, and
// uploaded per target on worker threads. Battle logs are fetched from the game server on their own thread,
// because each needs extra requests to resolve player and alliance names.
//
// The threads are detached and run until the process ends, so everything they share is allocated once and never
// destroyed: static destructors at exit must not free state another thread is still using.

#include "config.h"

#include "prime/server.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

NLOHMANN_JSON_NAMESPACE_BEGIN
template <> struct adl_serializer<SyncConfig::Type> {
  static void to_json(json& j, const SyncConfig::Type t)
  {
    j = nullptr;
    for (const auto& opt : SyncOptions)
      if (opt.type == t)
        j = opt.type_str;
  }
};
NLOHMANN_JSON_NAMESPACE_END

namespace data_sync
{
// Sync messages only show up with [sync] logging enabled; debug and trace also need [sync] debug. The message is
// only formatted when it will be logged.
namespace logging
{
  template <typename... Args>
  void error(std::string_view type, std::string_view target, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Config::Get().sync.logging)
      spdlog::error("SYNC-{} - {}: {}", type, target, fmt::format(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void debug(std::string_view type, std::string_view target, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Config::Get().sync.logging && Config::Get().sync.debug)
      spdlog::debug("SYNC-{} - {}: {}", type, target, fmt::format(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void trace(std::string_view type, std::string_view target, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Config::Get().sync.logging && Config::Get().sync.debug)
      spdlog::trace("SYNC-{} - {}: {}", type, target, fmt::format(format, std::forward<Args>(args)...));
  }
} // namespace logging

// A FIFO shared between threads that drops its oldest entry when full. A slow or unreachable target must not
// grow memory without bound while the game keeps producing updates.
template <typename T> class BoundedQueue
{
public:
  BoundedQueue(const char* name, size_t capacity)
      : name_(name)
      , capacity_(capacity)
  {
  }

  void Push(T item)
  {
    {
      std::scoped_lock lock(mutex_);
      if (items_.size() >= capacity_) {
        items_.pop_front();
        // Every 100th drop, so a dead target does not flood the log.
        if (dropped_++ % 100 == 0)
          spdlog::warn("[Sync] {} queue is full ({} entries); dropped {} so far", name_, capacity_, dropped_);
      }
      items_.push_back(std::move(item));
    }
    ready_.notify_one();
  }

  // Blocks until an item is available.
  T Pop()
  {
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [this] { return !items_.empty(); });
    T item = std::move(items_.front());
    items_.pop_front();
    return item;
  }

private:
  const char*             name_;
  size_t                  capacity_;
  size_t                  dropped_ = 0;
  std::mutex              mutex_;
  std::condition_variable ready_;
  std::deque<T>           items_;
};

// upload.cc

// Queues changed records of one category for every target that wants it. `first_sync` marks the first full
// upload of a category in this session (sent as X-PRIME-SYNC: 2).
void Queue(SyncConfig::Type type, const nlohmann::json& records, bool first_sync = false);

// records.cc. Processing thread only.

// Turns one server response into records. Its bytes are an entity group's payload.
using Processor = void (*)(const std::string& bytes);
// The processor for an entity group kind, or null when the mod does not sync it or no target wants it.
Processor ProcessorFor(EntityGroup::Kind kind);
void      ProcessRealtimeSlot(const std::string& bytes);
// Platform responses only, so not routed by EntityGroup::Kind.
void ProcessStoreOffers(const std::string& bytes);
// The local player's own record from a UserProfiles response. `self` and `level` are read on the game thread when
// the response arrives, since only the game thread may call into the game.
void ProcessOwnProfile(const std::string& bytes, const std::string& self, int32_t level);
// A new login (or a switch of account or server): forgets what was sent, so the new account gets its first full
// upload and nothing of the old one is reported gone.
void StartNewSession(const std::string& bytes);
// The game's record of the local player, or nullptr. Calls into the game.
UserProfile* LocalUserProfile();

// fleets.cc. Client fleet records are read on the game thread, from fleet_watch and the frame tick.

// Subscribes to fleet_watch when a target wants fleets.
void InstallFleetSync();
// Sync's frame work: polls fleet_watch, then re-reads and snapshots what was asked for. Returns at once when fleet
// sync is off.
void UpdateFleetSync();
// Asks the game thread for a full fleet snapshot, sent as a first sync. Any thread.
void RequestFleetSnapshot();
// Asks the game thread to re-read a fleet a second from now and resend it if its late values changed. Any thread.
void RereadFleet(uint64_t fleet_id);

// battlelogs.cc

void StartBattleLogs();
// Queues the battles that were not sent before. Processing thread only.
void QueueNewBattles(const std::vector<uint64_t>& battle_ids);
// Fills the name caches from profile responses the game requested itself.
void CachePlayerNames(const std::string& bytes);
void CacheAllianceNames(const std::string& bytes);
// The game session, captured from the game's own server setup, for the battle-log requests.
void SetGameServer(std::string url, std::string session_id);
void SetGameVersion(std::string version);
void SetInstanceId(int32_t instance_id);
} // namespace data_sync

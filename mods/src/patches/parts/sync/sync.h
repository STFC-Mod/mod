#pragma once

// Internal to data sync. Game data is diffed against what was last sent on one processing thread, queued, and
// uploaded per target on worker threads. Battle logs are fetched from the game server on their own thread,
// because each needs extra requests to resolve player and alliance names.

#include "config.h"

#include <google/protobuf/repeated_field.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

NLOHMANN_JSON_NAMESPACE_BEGIN
template <typename T> struct adl_serializer<google::protobuf::RepeatedField<T>> {
  static void to_json(json& j, const google::protobuf::RepeatedField<T>& proto)
  {
    j = json::array();
    for (const auto& v : proto)
      j.push_back(v);
  }
};

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
// Sync messages only show up with [sync] logging enabled; debug and trace also need [sync] debug.
namespace logging
{
  void error(std::string_view type, std::string_view target, std::string_view text);
  void warn(std::string_view type, std::string_view target, std::string_view text);
  void info(std::string_view type, std::string_view target, std::string_view text);
  void debug(std::string_view type, std::string_view target, std::string_view text);
  void trace(std::string_view type, std::string_view target, std::string_view text);
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

  size_t Size()
  {
    std::scoped_lock lock(mutex_);
    return items_.size();
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
void StartUploads();

// The game session, captured from the game's own server setup.
void SetGameServer(std::string url, std::string session_id);
void SetGameVersion(std::string version);
void SetInstanceId(int32_t instance_id);
// POSTs to the game server as the game client would, for data the client did not request itself. Empty on error.
std::string RequestGameData(const std::string& path, const std::string& body);

// fleets.cc. Client fleet records are read on the game thread, from fleet_watch and the frame tick.

// Subscribes to fleet_watch when a target wants fleets.
void InstallFleetSync();
// Asks the game thread for a full fleet snapshot, sent as a first sync. Any thread.
void RequestFleetSnapshot();
// Asks the game thread to re-read a fleet a second from now and resend it if its late values changed. Any thread.
void RereadFleet(uint64_t fleet_id);

// battlelogs.cc

void StartBattleLogs();
// Queues the battles that were not sent before.
void QueueNewBattles(const std::vector<uint64_t>& battle_ids);
// Fills the name caches from profile responses the game requested itself.
void CachePlayerNames(const std::string& bytes);
void CacheAllianceNames(const std::string& bytes);
} // namespace data_sync

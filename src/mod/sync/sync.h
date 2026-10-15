#pragma once

// Internal sync records, delivery cursors and game-session jobs. Network workers share one process-lifetime
// owner; each upload captures its session and target configuration before leaving the owner's lock.

#include "config.h"

#include "game/server.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace data_sync
{
// Sync messages only show up with [sync] logging enabled; debug and trace also need [sync] debug. The message is
// only formatted when it will be logged.
namespace logging
{
  inline bool Enabled(spdlog::level::level_enum level)
  {
    if (!spdlog::should_log(level))
      return false;
    const auto config = Config::Sync::Get();
    return config->logging && (level == spdlog::level::err || config->debug);
  }

  template <typename... Args>
  void error(std::string_view type, std::string_view target, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Enabled(spdlog::level::err))
      spdlog::error("SYNC-{} - {}: {}", type, target, fmt::format(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void debug(std::string_view type, std::string_view target, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Enabled(spdlog::level::debug))
      spdlog::debug("SYNC-{} - {}: {}", type, target, fmt::format(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void trace(std::string_view type, std::string_view target, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Enabled(spdlog::level::trace))
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

  std::optional<T> Push(T item)
  {
    std::optional<T> dropped;
    {
      std::scoped_lock lock(mutex_);
      if (items_.size() >= capacity_) {
        dropped = std::move(items_.front());
        items_.pop_front();
        // Every 100th drop, so a dead target does not flood the log.
        if (dropped_++ % 100 == 0)
          spdlog::warn("[Sync] {} queue is full ({} entries); dropped {} so far", name_, capacity_, dropped_);
      }
      items_.push_back(std::move(item));
    }
    ready_.notify_one();
    return dropped;
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

// Separate streams preserve the wire protocol's player/alliance resources and active/completed missions.
enum class Stream {
  kAway,
  kBattles,
  kBuffs,
  kBuildings,
  kEmerald,
  // Client fleet changes, a journal like battles: one entry per transition, re-read or snapshot.
  kFleets,
  // The server's view of deployed fleets, one record per fleet.
  kDeployedFleets,
  kHaven,
  kHavenPieces,
  kHavenProsperity,
  kInventory,
  kJobs,
  kActiveMissions,
  kCompletedMissions,
  kOfficers,
  kProfile,
  kResearch,
  kResources,
  kAllianceResources,
  kShips,
  kSlots,
  kStoreOffers,
  kTech,
  kTraits,
  kCount
};
// Keep recursive JSON ordering out of each record producer's translation unit.
struct RecordKeyLess {
  bool operator()(const nlohmann::json& a, const nlohmann::json& b) const;
};
using RecordSet = std::map<nlohmann::json, nlohmann::json, RecordKeyLess>;
// Records are immutable and shared by current state and target cursors, so targets do not copy whole JSON trees.
using Snapshot                         = std::map<nlohmann::json, std::shared_ptr<const nlohmann::json>, RecordKeyLess>;
using Clock                            = std::chrono::steady_clock;
inline constexpr size_t kRecentBattles = 300;
// Fleet changes are a journal too, bounded like battles.
inline constexpr size_t kRecentFleetChanges = 300;

// Full lists establish the initial-sync marker; Replace also prunes entities absent from the list.
enum class Update { kMerge, kFull, kReplace };

class RecordStore
{
public:
  void     Reset();
  bool     Apply(Stream stream, RecordSet records, Update mode = Update::kMerge);
  bool     Contains(Stream stream, const nlohmann::json& key) const;
  uint64_t generation = 0;

private:
  friend class Delivery;
  struct Collection {
    Snapshot records;
    uint64_t version = 0;
    bool     full    = false;
    // Journal keys in arrival order, oldest first.
    std::deque<nlohmann::json> order;
  };
  std::array<std::optional<Collection>, static_cast<size_t>(Stream::kCount)> streams_;
};

struct Upload {
  Stream                                             stream;
  uint64_t                                           generation, revision, version;
  Snapshot                                           snapshot;
  std::vector<std::shared_ptr<const nlohmann::json>> records;
  bool                                               first_sync = false;
  nlohmann::json                                     Records() const;
  const char*                                        Type() const;
};

class Delivery
{
public:
  void Reset(uint64_t generation)
  {
    generation_ = generation;
    cursors_    = {};
  }
  void                  Configure(const SyncTargetConfig& config);
  std::optional<Upload> Next(const RecordStore& store, Clock::time_point now);
  // Only a successful response for the captured session and configuration advances the cursor.
  void                    Complete(Upload upload, bool success, uint64_t generation, Clock::time_point now);
  const SyncTargetConfig& Config() const
  { return config_; }
  uint64_t Revision() const
  { return revision_; }

private:
  struct Cursor {
    Snapshot             accepted, attempted;
    uint64_t             version = 0;
    bool                 full    = false;
    Clock::time_point    retry_at{};
    std::chrono::seconds backoff{1};
  };
  SyncTargetConfig                                        config_;
  uint64_t                                                revision_ = 0, generation_ = 0;
  size_t                                                  next_ = 0;
  std::array<Cursor, static_cast<size_t>(Stream::kCount)> cursors_;
};

struct GameSession {
  uint64_t    generation = 0;
  std::string server_url, session_id;
  std::string version     = "1.000.51336";
  int32_t     instance_id = 0;
};
struct Input {
  uint64_t    generation;
  std::string bytes;
  // Captured on the game thread for owner-qualified Haven map responses and the own profile.
  std::string user_id;
  // The local player's level, captured with user_id for the own profile: its server record carries level 0.
  int32_t level = 0;
};
using Processor = void (*)(const Input&);
Processor ProcessorFor(EntityGroup::Kind kind);
void      ProcessRealtimeSlot(const Input& input);
void      ProcessStoreOffers(const Input& input);
void      Publish(Stream stream, RecordSet records, uint64_t generation, Update mode = Update::kMerge);
void      FollowSettings();
// The local player's own record from a UserProfiles response; needs user_id and level.
void ProcessOwnProfile(const Input& input);
// Jobs only need the generation; Session() also copies the request headers.
uint64_t    SessionGeneration();
GameSession Session();
void        SetGameServer(std::string url, std::string session_id);
void        SetGameVersion(std::string version);
void        SetInstanceId(int32_t instance_id);
// Journals are cached per session, so every target can receive them without a global sent-ID filter.
void StartBattleLogs();
void QueueNewBattles(const std::vector<uint64_t>& ids, uint64_t generation);
bool HasBattle(uint64_t id, uint64_t generation);
void CachePlayerNames(const Input& input);
void CacheAllianceNames(const Input& input);

// fleets.cc. Client fleet records are read on the game thread, from fleet_watch and the frame tick.

// Subscribes to fleet_watch when a target wants fleets.
void InstallFleetSync();
// Sync's frame work for fleets: polls fleet_watch, then re-reads and snapshots what was asked for. Returns at once
// when fleet sync is off.
void UpdateFleetSync();
// Asks the game thread for a full fleet snapshot, sent as a first sync. Any thread.
void RequestFleetSnapshot();
// Asks the game thread to re-read a fleet a second from now and resend it if its late values changed. Any thread.
void RereadFleet(uint64_t fleet_id);
} // namespace data_sync

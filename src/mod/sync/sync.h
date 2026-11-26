#pragma once

// Sync internals, shared by the files in sync/ and its tests. Features never include this; kSync is the only entry
// point. Records, delivery cursors and the game session live in one process-lifetime state; each upload captures
// its session and target settings before leaving that state's lock.

#include "config.h"

#include "game/server.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// Not `sync`: POSIX already declares ::sync().
namespace data_sync
{
using json  = nlohmann::json;
using Clock = std::chrono::steady_clock;

// --- Logging ---

// Sync messages show up only with [sync] logging on; debug and trace also need [sync] debug. Server drift and
// unreachable targets are routine for sync, so even its errors stay behind the switch. The message is formatted
// only when it will be logged. `what` is the activity (upload, download, process), `about` its subject.
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
  void Error(std::string_view what, std::string_view about, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Enabled(spdlog::level::err))
      spdlog::error("[Sync] {} {}: {}", what, about, fmt::format(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void Debug(std::string_view what, std::string_view about, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Enabled(spdlog::level::debug))
      spdlog::debug("[Sync] {} {}: {}", what, about, fmt::format(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void Trace(std::string_view what, std::string_view about, fmt::format_string<Args...> format, Args&&... args)
  {
    if (Enabled(spdlog::level::trace))
      spdlog::trace("[Sync] {} {}: {}", what, about, fmt::format(format, std::forward<Args>(args)...));
  }
} // namespace logging

// --- Thread-safe containers ---

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

  // Returns the entry dropped to make room, if any, so its owner can forget it.
  std::optional<T> Push(T item)
  {
    std::optional<T> dropped;
    {
      std::scoped_lock lock(mutex_);
      if (items_.size() >= capacity_) {
        dropped = std::move(items_.front());
        items_.pop_front();
        // A dropped entry is a real fault, so this is not behind [sync] logging; every 100th, so a dead target
        // does not flood the log.
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

// Names seen in battles and profile responses, kept for [sync] resolver_cache_ttl seconds. Filled by the
// processing thread from responses the game requested and by the battle-log thread from its own requests, so it
// locks. A new session's generation forgets the old session's names.
template <typename Key, typename Value> class NameCache
{
public:
  // A busy server shows a few thousand players a day; past this size, expired entries are dropped first.
  static constexpr size_t kCapacity = 10'000;

  std::optional<Value> Find(const Key& key, uint64_t generation)
  {
    std::scoped_lock lock(mutex_);
    if (!Follow(generation))
      return std::nullopt;
    const auto it = entries_.find(key);
    if (it == entries_.end())
      return std::nullopt;
    if (it->second.expires_at <= Clock::now()) {
      entries_.erase(it);
      return std::nullopt;
    }
    return it->second.value;
  }

  void Store(const Key& key, Value value, uint64_t generation)
  {
    const auto       expires_at = Clock::now() + std::chrono::seconds(Config::Sync::Get()->resolver_cache_ttl);
    std::scoped_lock lock(mutex_);
    if (!Follow(generation))
      return;
    if (entries_.size() >= kCapacity) {
      std::erase_if(entries_, [now = Clock::now()](const auto& entry) { return entry.second.expires_at <= now; });
      // Everything still valid (a long TTL and a lot of browsing): start over rather than grow past the bound.
      if (entries_.size() >= kCapacity)
        entries_.clear();
    }
    entries_.insert_or_assign(key, Entry{.value = std::move(value), .expires_at = expires_at});
  }

  size_t Size()
  {
    std::scoped_lock lock(mutex_);
    return entries_.size();
  }

private:
  struct Entry {
    Value             value;
    Clock::time_point expires_at;
  };

  // False for an older session's late request. A newer one clears the cache. Callers hold the lock.
  bool Follow(uint64_t generation)
  {
    if (generation < generation_)
      return false;
    if (generation != generation_) {
      entries_.clear();
      generation_ = generation;
    }
    return true;
  }

  uint64_t                       generation_ = 0;
  std::mutex                     mutex_;
  std::unordered_map<Key, Entry> entries_;
};

// --- Records ---

// One stream per wire record type. Player and alliance resources, and active and completed missions, are separate
// streams because the wire protocol keeps them apart.
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

// Keeps recursive JSON ordering out of each record producer's translation unit.
struct RecordKeyLess {
  bool operator()(const json& a, const json& b) const;
};
using RecordSet = std::map<json, json, RecordKeyLess>;
// Records are immutable and shared by current state and target cursors, so targets do not copy whole JSON trees.
using Snapshot = std::map<json, std::shared_ptr<const json>, RecordKeyLess>;

// Journals are large and only the recent ones matter to a target; older ones are forgotten.
inline constexpr size_t kRecentBattles = 300;
// Fleet changes are a journal too, bounded like battles.
inline constexpr size_t kRecentFleetChanges = 300;

// The battle journals one target has acknowledged, kept across sessions so a login does not send it, or fetch from
// the game server, what it already holds. Most recent last. The fingerprint identifies the endpoint they were
// delivered to: a target whose URL changed starts over. The bound is twice the journal's own: an id still in the
// game's header list (the fetch queue and journal store hold kRecentBattles) must not be pruned while the list is
// full, or it would be fetched and sent again at every login.
class AckedBattles
{
public:
  static constexpr size_t kMax = 2 * kRecentBattles;

  const std::string& Fingerprint() const
  { return fingerprint_; }
  const std::deque<uint64_t>& Ids() const
  { return ids_; }
  bool Contains(uint64_t id) const
  { return set_.contains(id); }
  // Appends a new id and prunes the oldest beyond kMax; false for one already held, which keeps its place.
  bool Add(uint64_t id);
  // Forgets every id and names the endpoint the next ones belong to.
  void Reset(std::string fingerprint);
  // Keeps the most recent kMax of `ids`, oldest first.
  void Restore(std::string fingerprint, const std::vector<uint64_t>& ids);

private:
  std::string                  fingerprint_;
  std::deque<uint64_t>         ids_;
  std::unordered_set<uint64_t> set_;
};
using AckedByTarget = std::map<std::string, AckedBattles>;

// A stable name for a target's URL (FNV-1a, 64 bits as 16 hex digits), so that the URL itself is not stored.
std::string UrlFingerprint(std::string_view url);
// The file format: {"version":1,"targets":{"<name>":{"fingerprint":"...","ids":[...]}}}.
json          AckedToJson(const AckedByTarget& acked);
AckedByTarget AckedFromJson(const json& document);
// A missing file is empty without a problem. An unreadable or malformed one is empty, with `problem` set; so is one
// from a newer version, which also sets `newer` so that the caller does not overwrite it.
AckedByTarget LoadAcked(const std::filesystem::path& file, std::string& problem, bool& newer);
// Writes through a temporary file and a rename, so a crash leaves the old file or the new one. False with `problem`.
bool SaveAcked(const std::filesystem::path& file, const AckedByTarget& acked, std::string& problem);

// The battle ids a session has been told about, newest first, kept so that a target that comes on later can have the
// ones it lacks fetched. Several message sources report the list and none is known to be complete, so reports are
// merged, never replaced.
struct HeaderList {
  uint64_t              generation = 0;
  std::vector<uint64_t> ids;
};
// Merges a report into the list: its ids come first in their order, followed by the known ones it did not repeat,
// without duplicates and cut to kRecentBattles. An empty report changes nothing; a new generation starts over.
void MergeHeaderList(HeaderList& list, uint64_t generation, const std::vector<uint64_t>& ids);

// Full lists establish the initial-sync marker; Replace also prunes entities absent from the list.
enum class Update { kMerge, kFull, kReplace };

// The current session's records, per stream. Its generation changes with every session, which invalidates every
// target's cursors and any work queued for the old session.
class RecordStore
{
public:
  void     Reset();
  bool     Apply(Stream stream, RecordSet records, Update mode = Update::kMerge);
  bool     Contains(Stream stream, const json& key) const;
  uint64_t Generation() const
  { return generation_; }

private:
  friend class Delivery;
  // `version` moves on with every change, so a target can tell at a glance that it has nothing new. `full` says a
  // complete list has arrived this session; until then no target can be sent a full baseline. A journal's `order`
  // holds its keys, oldest first, for pruning to its bound.
  struct Collection {
    Snapshot         records;
    uint64_t         version = 0;
    bool             full    = false;
    std::deque<json> order;
  };
  uint64_t                                                                   generation_ = 0;
  std::array<std::optional<Collection>, static_cast<size_t>(Stream::kCount)> streams_;
};

// One request to one target. `generation` and `revision` are the session and target configuration it was built
// for; `version` is the stream version it acknowledges, or 0 when it is partial. `snapshot` is what the target
// holds once it succeeds, and `records` what is sent.
struct Upload {
  Stream                                   stream;
  uint64_t                                 generation, revision, version;
  Snapshot                                 snapshot;
  std::vector<std::shared_ptr<const json>> records;
  bool                                     first_sync = false;
  json                                     Records() const;
  const char*                              Type() const;
};

// One target's view of the records: what it has acknowledged, what is in flight, and when to retry.
class Delivery
{
public:
  void Configure(const SyncTargetConfig& config);
  // For a target removed from the settings: an upload still in flight can no longer acknowledge anything.
  void                  Retire();
  std::optional<Upload> Next(const RecordStore& store, Clock::time_point now);
  // Only a successful response for the captured session and configuration advances the cursor.
  void                    Complete(Upload upload, bool success, uint64_t generation, Clock::time_point now);
  const SyncTargetConfig& Config() const
  { return config_; }
  uint64_t Revision() const
  { return revision_; }
  // The battle journals this target acknowledged, across sessions. Configure drops them when the URL changes.
  const AckedBattles& Acked() const
  { return acked_; }
  // Takes what a file held for this target, once, unless the endpoint differs. Later calls do nothing.
  void AdoptAcked(const AckedBattles& stored);
  bool AckedAdopted() const
  { return acked_adopted_; }
  // True once after the acknowledged ids changed, for the saver.
  bool TakeAckedDirty();
  // True once after something changed what this target lacks: its battle logs came on, its endpoint changed, or its
  // record was adopted. The battle queue then decides again for the last header list.
  bool TakeBattleReplay();

private:
  struct Cursor {
    Snapshot             accepted, attempted;
    uint64_t             version = 0;
    bool                 full    = false;
    Clock::time_point    retry_at{};
    std::chrono::seconds backoff{1};
  };
  // revision_ changes with the configuration, so an upload built for an older one cannot acknowledge anything.
  // Streams are visited round-robin from next_, so one stream that always has news cannot starve the rest.
  SyncTargetConfig config_;
  uint64_t         revision_ = 0, generation_ = 0;
  size_t           next_ = 0;
  // Not part of the cursors: they start over with every session, these do not.
  AckedBattles acked_;
  bool         acked_adopted_ = false, acked_dirty_ = false, battle_replay_ = false;
  std::array<Cursor, static_cast<size_t>(Stream::kCount)> cursors_;
};

// What to save: each target in the settings with the journals it holds, or with what the file held for it before it
// adopted them. A target that left the settings is not carried over. `dirty` is set when a target's record changed.
AckedByTarget CollectAcked(const std::map<std::string, std::shared_ptr<Delivery>>& targets, const AckedByTarget& stored,
                           bool& dirty);
// Whether at least one target syncs battle logs and every such target has acknowledged journal `id`.
bool EveryBattleTargetAcked(const std::map<std::string, std::shared_ptr<Delivery>>& targets, uint64_t id);

// --- Session and shared state ---

// What the battle-log thread needs to ask the game server for data as the game client would.
struct GameSession {
  uint64_t    generation = 0;
  std::string server_url, session_id;
  // The X-PRIME-VERSION header until GameServer.Initialise reports the real one, which it does not when [patches]
  // game_version is off. Bump it when the server starts rejecting it.
  std::string version     = "1.000.51336";
  int32_t     instance_id = 0;
};

// Shared by the game thread, the processing thread and the network workers. Leaked on purpose: the detached
// workers may still run during process exit, after static destructors.
struct SharedState {
  std::mutex                                       mutex;
  std::condition_variable                          ready;
  RecordStore                                      records;
  GameSession                                      session;
  std::map<std::string, std::shared_ptr<Delivery>> targets;
  // records.Generation(), readable without the lock: the game thread reads it for every response, while workers
  // can hold the lock to copy whole snapshots.
  std::atomic<uint64_t> generation = 0;
};
SharedState& State();

// --- Entry points between the files ---

struct Input {
  uint64_t    generation;
  std::string bytes;
  // Captured on the game thread for owner-qualified Haven map responses and the own profile.
  std::string user_id;
  // The local player's level, captured with user_id for the own profile: its server record carries level 0.
  int32_t level = 0;
};
using Processor = void (*)(const Input&);

// The protobuf `Message` a response holds, or nullopt after logging that `about` does not parse.
template <typename Message> std::optional<Message> Parse(const Input& input, std::string_view about)
{
  Message message;
  if (message.ParseFromString(input.bytes))
    return message;
  logging::Error("process", about, "the response does not parse");
  return std::nullopt;
}

// records.cc. Null for responses no target wants.
Processor ProcessorFor(EntityGroup::Kind kind);
void      ProcessRealtimeSlot(const Input& input);
void      ProcessStoreOffers(const Input& input);
// The local player's own record from a UserProfiles response; needs user_id and level.
void ProcessOwnProfile(const Input& input);

// sync.cc. Publish drops records from an older session.
void Publish(Stream stream, RecordSet records, uint64_t generation, Update mode = Update::kMerge);
bool HasBattle(uint64_t id, uint64_t generation);
// Battle journals acknowledged across sessions. True when at least one target syncs battle logs and every such
// target has acknowledged `id`. Any thread.
bool BattleAckedEverywhere(uint64_t id);
// A target's upload thread takes its record from the file before its first upload, and saves any changes later.
void     AdoptBattleAcks(const std::string& name, Delivery& delivery);
void     FlushBattleAcks();
void     FollowSettings();
uint64_t SessionGeneration();
// Also copies the request headers, unlike SessionGeneration.
GameSession Session();

// upload.cc: one thread per target, until the target leaves the settings.
void UploadTarget(std::weak_ptr<Delivery> target, std::string name);

// battlelogs.cc. Journals are cached per session; each target also remembers across sessions which ones it acknowledged
// (AckedBattles), so a journal replays only to targets that lack it and is fetched only while one does.
// RunBattleLogs is the battle-log thread and never returns.
void RunBattleLogs();
void QueueNewBattles(const std::vector<uint64_t>& ids, uint64_t generation);
// Decides again for the last header list of this session, after a target came on or lost its record. Processing thread.
void RequeueLastBattles(const Input& input);
void CachePlayerNames(const Input& input);
void CacheAllianceNames(const Input& input);

// fleets.cc. Client fleet records are read on the game thread, from sync's frame work.

// Sync's frame work for fleets: once a target wants fleets, watches the fleet slots, then re-reads and snapshots what
// was asked for. Returns at once until then.
void UpdateFleetSync();
// Asks the game thread for a full fleet snapshot, sent as a first sync. Any thread.
void RequestFleetSnapshot();
// Asks the game thread to re-read a fleet a second from now and resend it if its late values changed. Any thread.
void RereadFleet(int64_t fleet_id);
} // namespace data_sync

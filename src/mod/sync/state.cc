// Canonical record state and per-target acknowledgement, without threads or HTTP. Callers hold the state mutex.

#include "sync/sync.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <ranges>
#include <utility>

namespace data_sync
{
bool RecordKeyLess::operator()(const json& a, const json& b) const
{ return a < b; }

namespace
{
  // When a stream's uploads carry the full-sync header: never, on the first complete list a target receives, or
  // on every upload.
  enum class FullSync { kNever, kInitial, kAlways };

  // How a stream goes on the wire. `removed` and `id` name the record a target receives when an entity it
  // acknowledged disappears. A journal keeps its most recent `journal` entries; each is delivered on its own and
  // never merged with a later one.
  struct Spec {
    Stream stream;
    bool SyncConfig::* enabled;
    const char*        type;
    FullSync           full_sync = FullSync::kNever;
    const char*        removed   = nullptr;
    const char*        id        = nullptr;
    size_t             journal   = 0;
  };
  constexpr std::array kStreams{
      Spec{Stream::kAway, &SyncConfig::away_assignments, "away_assignment", FullSync::kInitial,
           "collected_away_assignment", "aid"},
      Spec{Stream::kBattles, &SyncConfig::battlelogs, "battlelog", FullSync::kNever, nullptr, nullptr, kRecentBattles},
      Spec{Stream::kBuffs, &SyncConfig::buffs, "buff", FullSync::kInitial, "expired_buff", "bid"},
      Spec{Stream::kBuildings, &SyncConfig::buildings, "module"},
      // The emerald chain is the alliance loyalty tier, which sync sites list with the buffs it grants.
      Spec{Stream::kEmerald, &SyncConfig::buffs, "emerald_chain"},
      Spec{Stream::kFleets, &SyncConfig::fleets, "fleet", FullSync::kNever, nullptr, nullptr, kRecentFleetChanges},
      Spec{Stream::kDeployedFleets, &SyncConfig::fleets, "fleet"},
      Spec{Stream::kHaven, &SyncConfig::haven, "haven", FullSync::kInitial},
      Spec{Stream::kHavenPieces, &SyncConfig::haven, "haven_pieces", FullSync::kInitial},
      Spec{Stream::kHavenProsperity, &SyncConfig::haven, "haven_prosperity"},
      Spec{Stream::kInventory, &SyncConfig::inventory, "inventory", FullSync::kInitial},
      Spec{Stream::kJobs, &SyncConfig::jobs, "job", FullSync::kInitial, "completed_job", "uuid"},
      Spec{Stream::kActiveMissions, &SyncConfig::missions, "active_mission"},
      Spec{Stream::kCompletedMissions, &SyncConfig::missions, "mission"},
      Spec{Stream::kOfficers, &SyncConfig::officers, "officer"},
      Spec{Stream::kProfile, &SyncConfig::profile, "profile", FullSync::kInitial},
      Spec{Stream::kResearch, &SyncConfig::research, "research"},
      Spec{Stream::kResources, &SyncConfig::resources, "resource", FullSync::kInitial},
      Spec{Stream::kAllianceResources, &SyncConfig::resources, "alliance_resource", FullSync::kInitial},
      Spec{Stream::kShips, &SyncConfig::ships, "ship", FullSync::kInitial},
      Spec{Stream::kSlots, &SyncConfig::slots, "slot"},
      Spec{Stream::kStoreOffers, &SyncConfig::store_offers, "store_offer"},
      Spec{Stream::kTech, &SyncConfig::tech, "ft"},
      Spec{Stream::kTraits, &SyncConfig::traits, "trait"},
  };
  // Indexed by Stream.
  static_assert(kStreams.size() == static_cast<size_t>(Stream::kCount));
  static_assert([] {
    for (size_t i = 0; i < kStreams.size(); ++i)
      if (kStreams[i].stream != static_cast<Stream>(i))
        return false;
    return true;
  }());

  // New entities a target may have in flight, unacknowledged, before more are introduced. Each could be a server
  // write that needs a matching removal later, so this bounds the removals a target can owe.
  constexpr size_t kMaxUnacknowledgedEntities = 1024;

  // Ship level progress drifts between responses; a change under 0.01 is not worth an upload. Profile XP rides along
  // but is not part of the key: an XP-only change waits for the next level, Power or Haven change. So does the read
  // time of a server fleet record: an unchanged fleet is not sent again.
  bool Unchanged(Stream stream, const json& record, const json& old)
  {
    if (record == old)
      return true;
    if (stream == Stream::kProfile || stream == Stream::kDeployedFleets) {
      const char* along      = stream == Stream::kProfile ? "xp" : "t";
      auto        comparable = record;
      comparable[along]      = old.at(along);
      return comparable == old;
    }
    if (stream != Stream::kShips)
      return false;
    const auto& progress = old.at("level_percentage");
    const bool  drift    = std::fabs(record.at("level_percentage").get<double>() - progress.get<double>()) < 0.01;
    if (!drift)
      return false;
    auto comparable                = record;
    comparable["level_percentage"] = progress;
    return comparable == old;
  }
} // namespace

json Upload::Records() const
{
  auto result = json::array();
  for (const auto& record : records)
    if (record->is_array())
      result.insert(result.end(), record->begin(), record->end());
    else
      result.push_back(*record);
  return result;
}

const char* Upload::Type() const
{ return kStreams[static_cast<size_t>(stream)].type; }

void RecordStore::Reset()
{
  ++generation_;
  streams_ = {};
}

bool RecordStore::Contains(Stream stream, const json& key) const
{
  const auto& records = streams_[static_cast<size_t>(stream)];
  return records && records->records.contains(key);
}

bool RecordStore::Apply(Stream stream, RecordSet records, Update mode)
{
  auto& slot    = streams_[static_cast<size_t>(stream)];
  bool  changed = !slot;
  if (changed)
    slot.emplace();
  if (mode != Update::kMerge && !slot->full) {
    slot->full = true;
    changed    = true;
  }
  auto&       current = slot->records;
  const auto& spec    = kStreams[static_cast<size_t>(stream)];
  for (auto& [key, record] : records) {
    if (record.is_array())
      for (auto& item : record)
        item["type"] = spec.type;
    else
      record["type"] = spec.type;
    const auto old   = current.find(key);
    const bool added = old == current.end();
    if (!added && Unchanged(stream, record, *old->second))
      continue;
    changed = true;
    if (spec.journal && added)
      slot->order.push_back(key);
    current.insert_or_assign(key, std::make_shared<const json>(std::move(record)));
  }
  // `records` was moved from per entry above, but its keys are intact.
  if (mode == Update::kReplace)
    changed |= std::erase_if(current, [&](const auto& entry) { return !records.contains(entry.first); }) != 0;
  // Journals are bounded; other streams contain the current account's entities.
  while (slot->order.size() > spec.journal) {
    changed |= current.erase(slot->order.front()) != 0;
    slot->order.pop_front();
  }
  if (changed)
    ++slot->version;
  return changed;
}

void Delivery::Configure(const SyncTargetConfig& config)
{
  if (config == config_)
    return;
  const bool had_battles = config_.battlelogs;
  config_                = config;
  ++revision_;
  cursors_ = {};
  // A different endpoint has not seen what the old one acknowledged. A new token alone keeps the record.
  bool endpoint_changed = false;
  if (auto fingerprint = UrlFingerprint(config.url); fingerprint != acked_.Fingerprint()) {
    acked_dirty_ |= !acked_.Ids().empty();
    acked_.Reset(std::move(fingerprint));
    endpoint_changed = true;
  }
  // The journals fetched or skipped so far were decided without this target's wants.
  battle_replay_ |= config.battlelogs && (!had_battles || endpoint_changed);
}

void Delivery::AdoptAcked(const AckedBattles& stored)
{
  if (acked_adopted_)
    return;
  acked_adopted_ = true;
  if (stored.Fingerprint() == acked_.Fingerprint())
    acked_.Restore(stored.Fingerprint(), {stored.Ids().begin(), stored.Ids().end()});
  battle_replay_ |= config_.battlelogs;
}

bool Delivery::TakeBattleReplay()
{ return std::exchange(battle_replay_, false); }

bool Delivery::TakeAckedDirty()
{ return std::exchange(acked_dirty_, false); }

bool AckedBattles::Add(uint64_t id)
{
  if (!set_.insert(id).second)
    return false;
  ids_.push_back(id);
  while (ids_.size() > kMax) {
    set_.erase(ids_.front());
    ids_.pop_front();
  }
  return true;
}

void AckedBattles::Reset(std::string fingerprint)
{
  fingerprint_ = std::move(fingerprint);
  ids_.clear();
  set_.clear();
}

void AckedBattles::Restore(std::string fingerprint, const std::vector<uint64_t>& ids)
{
  Reset(std::move(fingerprint));
  for (const auto id : ids)
    Add(id);
}

std::string UrlFingerprint(std::string_view url)
{
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : url) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return fmt::format("{:016x}", hash);
}

json AckedToJson(const AckedByTarget& acked)
{
  auto targets = json::object();
  for (const auto& [name, battles] : acked)
    targets[name] = {{"fingerprint", battles.Fingerprint()}, {"ids", battles.Ids()}};
  return {{"version", 1}, {"targets", std::move(targets)}};
}

// The file's format version, if it has one: an unsigned number. A wrong-typed version is not one (json::value would
// throw on it).
static std::optional<uint64_t> AckedFileVersion(const json& document)
{
  if (!document.is_object())
    return std::nullopt;
  const auto version = document.find("version");
  if (version == document.end() || !version->is_number_unsigned())
    return std::nullopt;
  return version->get<uint64_t>();
}

AckedByTarget AckedFromJson(const json& document)
{
  AckedByTarget result;
  if (AckedFileVersion(document) != 1 || !document.contains("targets") || !document.at("targets").is_object())
    return result;
  for (const auto& [name, entry] : document.at("targets").items()) {
    if (!entry.is_object() || !entry.contains("fingerprint") || !entry.at("fingerprint").is_string()
        || !entry.contains("ids") || !entry.at("ids").is_array())
      continue;
    std::vector<uint64_t> ids;
    for (const auto& id : entry.at("ids"))
      if (id.is_number_unsigned())
        ids.push_back(id.get<uint64_t>());
    result[name].Restore(entry.at("fingerprint").get<std::string>(), ids);
  }
  return result;
}

AckedByTarget LoadAcked(const std::filesystem::path& file, std::string& problem, bool& newer)
{
  problem.clear();
  newer = false;
  std::error_code error;
  if (!std::filesystem::exists(file, error))
    return {};
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    problem = "the file cannot be read";
    return {};
  }
  const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  const auto        document = json::parse(text, nullptr, false);
  const auto        version  = document.is_discarded() ? std::nullopt : AckedFileVersion(document);
  if (version && *version > 1) {
    problem = "the file was written by a newer version and is left as it is";
    newer   = true;
    return {};
  }
  if (version != 1) {
    problem = "the file is not in the expected format";
    return {};
  }
  return AckedFromJson(document);
}

bool SaveAcked(const std::filesystem::path& file, const AckedByTarget& acked, std::string& problem)
{
  problem.clear();
  auto temporary = file;
  temporary += ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << AckedToJson(acked).dump(-1, ' ', false, json::error_handler_t::replace);
    out.flush();
    if (!out) {
      problem = "the temporary file cannot be written";
      return false;
    }
  }
  std::error_code error;
  std::filesystem::rename(temporary, file, error);
  if (error) {
    problem = "the file cannot be replaced";
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}

AckedByTarget CollectAcked(const std::map<std::string, std::shared_ptr<Delivery>>& targets, const AckedByTarget& stored,
                           bool& dirty)
{
  AckedByTarget result;
  for (const auto& [name, target] : targets) {
    dirty |= target->TakeAckedDirty();
    const auto before = stored.find(name);
    if (!target->AckedAdopted() && before != stored.end())
      result.emplace(name, before->second);
    else if (!target->Acked().Ids().empty())
      result.emplace(name, target->Acked());
  }
  return result;
}

void MergeHeaderList(HeaderList& list, uint64_t generation, const std::vector<uint64_t>& ids)
{
  if (list.generation != generation)
    list = {generation, {}};
  if (ids.empty())
    return;
  std::vector<uint64_t>        merged;
  std::unordered_set<uint64_t> seen;
  const std::vector<uint64_t>  known = list.ids;
  for (const auto* source : {&ids, &known})
    for (const auto id : *source)
      if (merged.size() < kRecentBattles && seen.insert(id).second)
        merged.push_back(id);
  list.ids = std::move(merged);
}

bool EveryBattleTargetAcked(const std::map<std::string, std::shared_ptr<Delivery>>& targets, uint64_t id)
{
  bool any = false;
  for (const auto& target : targets | std::views::values) {
    if (!target->Config().battlelogs)
      continue;
    any = true;
    if (!target->Acked().Contains(id))
      return false;
  }
  return any;
}

void Delivery::Retire()
{
  config_ = {};
  ++revision_;
}

std::optional<Upload> Delivery::Next(const RecordStore& store, Clock::time_point now)
{
  // A new session starts every cursor over.
  if (generation_ != store.Generation()) {
    generation_ = store.Generation();
    cursors_    = {};
  }
  for (size_t count = 0; count < kStreams.size(); ++count) {
    const size_t i          = (next_ + count) % kStreams.size();
    const auto&  collection = store.streams_[i];
    auto&        cursor     = cursors_[i];
    const auto&  spec       = kStreams[i];
    if (!collection || !(config_.*spec.enabled) || now < cursor.retry_at)
      continue;
    const auto& current = collection->records;
    const bool  journal = spec.journal != 0;
    // The last upload failed, perhaps after the server wrote it, so every record but journals is sent again.
    const bool uncertain = cursor.retry_at != Clock::time_point{};
    // Journals are acknowledged one at a time, so their cursor is compared by content rather than version. Pruned
    // journals leave the cursor too, or it would never match again.
    if (journal)
      std::erase_if(cursor.accepted, [&](const auto& entry) { return !current.contains(entry.first); });
    // A battle journal acknowledged in an earlier session counts as delivered when it reappears in the store.
    if (spec.stream == Stream::kBattles)
      for (const auto& [key, record] : current)
        if (key.is_number_unsigned() && acked_.Contains(key.get<uint64_t>()))
          cursor.accepted.emplace(key, record);
    if (!uncertain && (journal ? current == cursor.accepted : collection->version == cursor.version))
      continue;
    Upload upload{
        .stream     = spec.stream,
        .generation = store.Generation(),
        .revision   = revision_,
        .version    = collection->version,
        .first_sync = spec.full_sync == FullSync::kAlways
                      || (spec.full_sync == FullSync::kInitial && collection->full && !cursor.full),
    };
    bool blocked = false;
    for (const auto& [key, record] : current) {
      const auto acknowledged = cursor.accepted.find(key);
      const bool accepted     = acknowledged != cursor.accepted.end();
      if (spec.removed && !accepted && !cursor.attempted.contains(key)) {
        // Bound uncertain writes by waiting for acknowledgement before introducing more entities.
        if (cursor.attempted.size() >= kMaxUnacknowledgedEntities) {
          blocked = true;
          continue;
        }
        cursor.attempted.emplace(key, record);
      }
      if (!journal)
        upload.snapshot.emplace(key, record);
      // Records are shared and immutable, so an unchanged one is the same pointer.
      if (upload.first_sync || (uncertain && !journal) || !accepted || acknowledged->second != record) {
        upload.records.push_back(record);
        // Journals are large; one per request keeps a slow target making progress.
        if (journal) {
          upload.snapshot.emplace(key, record);
          // Fleet journal keys are [sequence, snapshot]. A snapshot lists every fleet, so it is a first sync.
          upload.first_sync = key.is_array() && key.back() == true;
          break;
        }
      }
    }
    // A timeout may hide a successful server write. Remove records from attempted uploads too; attempted contains
    // only new, unacknowledged keys, so the two maps are disjoint.
    if (spec.removed)
      for (const auto* previous : {&cursor.accepted, &cursor.attempted})
        for (const auto& [key, record] : *previous)
          if (!current.contains(key))
            upload.records.push_back(
                std::make_shared<const json>(json{{"type", spec.removed}, {spec.id, record->at(spec.id)}}));
    if (blocked) {
      // A partial list cannot claim to be a full baseline or acknowledge the current stream version.
      upload.first_sync = false;
      upload.version    = 0;
    }
    // Nothing differs from what the target holds (say, a change and its revert): catch up without a request.
    if (upload.records.empty() && !upload.first_sync) {
      cursor.accepted = current;
      cursor.version  = collection->version;
      continue;
    }
    next_ = (i + 1) % kStreams.size();
    return upload;
  }
  return std::nullopt;
}

void Delivery::Complete(Upload upload, bool success, uint64_t generation, Clock::time_point now)
{
  if (upload.generation != generation || upload.revision != revision_)
    return;
  auto& cursor = cursors_[static_cast<size_t>(upload.stream)];
  // Backoff runs 1 s doubling to a minute; a success resets it.
  if (!success) {
    cursor.retry_at = now + cursor.backoff;
    cursor.backoff  = std::min(cursor.backoff * 2, std::chrono::seconds(60));
    return;
  }
  // A journal upload carries only the journal it sent; every other upload carries the whole stream.
  if (kStreams[static_cast<size_t>(upload.stream)].journal) {
    for (auto& [key, record] : upload.snapshot) {
      if (upload.stream == Stream::kBattles && key.is_number_unsigned()) {
        acked_dirty_ |= acked_.Add(key.get<uint64_t>());
      }
      cursor.accepted.insert_or_assign(key, std::move(record));
    }
  } else
    cursor.accepted = std::move(upload.snapshot);
  cursor.full |= upload.first_sync;
  cursor.version = upload.version;
  cursor.attempted.clear();
  cursor.retry_at = {};
  cursor.backoff  = std::chrono::seconds(1);
}
} // namespace data_sync

// Canonical record state and per-target acknowledgement, without threads or HTTP.
#include "sync/sync.h"
#include <cmath>

namespace data_sync
{
namespace
{
  using json = nlohmann::json;
  struct Spec {
    bool SyncConfig::* enabled;
    const char*        type;
    const char*        removed      = nullptr;
    const char*        id           = nullptr;
    bool               initial_full = false, always_full = false;
    // Recent entries kept for a journal: every entry is delivered on its own and never merged with a later one.
    size_t journal = 0;
  };
  constexpr std::array kStreams{
      Spec{&SyncConfig::away_assignments, "away_assignment", "collected_away_assignment", "aid", true},
      Spec{&SyncConfig::battlelogs, "battlelog", nullptr, nullptr, false, false, kRecentBattles},
      Spec{&SyncConfig::buffs, "buff", "expired_buff", "bid", true},
      Spec{&SyncConfig::buildings, "module"},
      Spec{&SyncConfig::buffs, "emerald_chain"},
      Spec{&SyncConfig::fleets, "fleet", nullptr, nullptr, false, false, kRecentFleetChanges},
      Spec{&SyncConfig::fleets, "fleet"},
      Spec{&SyncConfig::haven, "haven", nullptr, nullptr, true},
      Spec{&SyncConfig::haven, "haven_pieces", nullptr, nullptr, true},
      Spec{&SyncConfig::haven, "haven_prosperity"},
      Spec{&SyncConfig::inventory, "inventory", nullptr, nullptr, true},
      Spec{&SyncConfig::jobs, "job", "completed_job", "uuid", true},
      Spec{&SyncConfig::missions, "active_mission"},
      Spec{&SyncConfig::missions, "mission"},
      Spec{&SyncConfig::officers, "officer"},
      Spec{&SyncConfig::profile, "profile", nullptr, nullptr, true},
      Spec{&SyncConfig::research, "research"},
      Spec{&SyncConfig::resources, "resource", nullptr, nullptr, true},
      Spec{&SyncConfig::resources, "alliance_resource", nullptr, nullptr, true},
      Spec{&SyncConfig::ships, "ship", nullptr, nullptr, true},
      Spec{&SyncConfig::slots, "slot"},
      Spec{&SyncConfig::store_offers, "store_offer"},
      Spec{&SyncConfig::tech, "ft"},
      Spec{&SyncConfig::traits, "trait"},
  };
  static_assert(kStreams.size() == static_cast<size_t>(Stream::kCount));
} // namespace
nlohmann::json Upload::Records() const
{
  auto result = nlohmann::json::array();
  for (const auto& record : records) {
    if (record->is_array())
      for (const auto& item : *record)
        result.push_back(item);
    else
      result.push_back(*record);
  }
  return result;
}
const char* Upload::Type() const
{ return kStreams[static_cast<size_t>(stream)].type; }
void RecordStore::Reset()
{
  ++generation;
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
  if (!slot)
    slot.emplace();
  if (mode != Update::kMerge && !slot->full) {
    slot->full = true;
    changed    = true;
  }
  auto&        current = slot->records;
  const size_t journal = kStreams[static_cast<size_t>(stream)].journal;
  for (auto& [key, record] : records) {
    const auto type = kStreams[static_cast<size_t>(stream)].type;
    if (record.is_array())
      for (auto& item : record)
        item["type"] = type;
    else
      record["type"] = type;
    const auto old = current.find(key);
    if (old != current.end()) {
      if (record == *old->second)
        continue;
      if (stream == Stream::kShips
          && std::fabs(record.at("level_percentage").get<double>() - old->second->at("level_percentage").get<double>())
                 < 0.01) {
        auto comparable                = record;
        comparable["level_percentage"] = old->second->at("level_percentage");
        if (comparable == *old->second)
          continue;
      }
      // XP rides along but is not part of the key: an XP-only change waits for the next level, Power or Haven change.
      if (stream == Stream::kProfile) {
        auto comparable  = record;
        comparable["xp"] = old->second->at("xp");
        if (comparable == *old->second)
          continue;
      }
      // The read time of a server fleet record rides along too: an unchanged fleet is not sent again.
      if (stream == Stream::kDeployedFleets) {
        auto comparable = record;
        comparable["t"] = old->second->at("t");
        if (comparable == *old->second)
          continue;
      }
    }
    changed = true;
    if (journal && old == current.end())
      slot->order.push_back(key);
    current.insert_or_assign(key, std::make_shared<const json>(std::move(record)));
  }
  if (mode == Update::kReplace)
    changed |= std::erase_if(current, [&](const auto& entry) { return !records.contains(entry.first); }) != 0;
  // Journals are bounded; other streams contain the current account's entities.
  while (journal && slot->order.size() > journal) {
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
  config_ = config;
  ++revision_;
  cursors_ = {};
}
std::optional<Upload> Delivery::Next(const RecordStore& store, Clock::time_point now)
{
  if (generation_ != store.generation) {
    generation_ = store.generation;
    cursors_    = {};
  }
  for (size_t count = 0; count < kStreams.size(); ++count) {
    const size_t i          = (next_ + count) % kStreams.size();
    const auto&  collection = store.streams_[i];
    auto&        cursor     = cursors_[i];
    const auto&  spec       = kStreams[i];
    if (!collection || !(config_.*spec.enabled) || now < cursor.retry_at)
      continue;
    const auto& current   = collection->records;
    const bool  journal   = spec.journal != 0;
    const bool  uncertain = cursor.retry_at != Clock::time_point{};
    if (journal)
      std::erase_if(cursor.accepted, [&](const auto& entry) { return !current.contains(entry.first); });
    if (!uncertain && (journal ? current == cursor.accepted : collection->version == cursor.version))
      continue;
    Upload upload{static_cast<Stream>(i), store.generation, revision_, collection->version, {}};
    upload.first_sync = spec.always_full || (spec.initial_full && collection->full && !cursor.full);
    bool blocked      = false;
    for (const auto& [key, record] : current) {
      const bool accepted = cursor.accepted.contains(key);
      if (spec.removed && !accepted && !cursor.attempted.contains(key)) {
        // Bound uncertain writes by waiting for acknowledgement before introducing more entities.
        if (cursor.attempted.size() >= 1024) {
          blocked = true;
          continue;
        }
        cursor.attempted.emplace(key, record);
      }
      if (!journal)
        upload.snapshot.emplace(key, record);
      if (upload.first_sync || (uncertain && !journal) || !accepted || cursor.accepted.at(key) != record) {
        upload.records.push_back(record);
        if (journal) {
          upload.snapshot.emplace(key, record);
          // Fleet journal keys are [sequence, snapshot]. A snapshot lists every fleet, so it is a first sync.
          upload.first_sync = key.is_array() && key.back() == true;
          break;
        }
      }
    }
    // A timeout may hide a successful server write. Remove records from attempted uploads too.
    if (spec.removed) {
      // Attempted contains only new, unacknowledged keys, so these maps are disjoint.
      for (const auto* previous : {&cursor.accepted, &cursor.attempted})
        for (const auto& [key, record] : *previous)
          if (!current.contains(key))
            upload.records.push_back(
                std::make_shared<const json>(json{{"type", spec.removed}, {spec.id, record->at(spec.id)}}));
    }
    if (blocked) {
      // A partial list cannot claim to be a full baseline or acknowledge the current stream version.
      upload.first_sync = false;
      upload.version    = 0;
    }
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
  if (!success) {
    cursor.retry_at = now + cursor.backoff;
    cursor.backoff  = std::min(cursor.backoff * 2, std::chrono::seconds(60));
    return;
  }
  if (kStreams[static_cast<size_t>(upload.stream)].journal)
    for (auto& [key, record] : upload.snapshot)
      cursor.accepted.insert_or_assign(key, std::move(record));
  else
    cursor.accepted = std::move(upload.snapshot);
  cursor.full |= upload.first_sync;
  cursor.version = upload.version;
  cursor.attempted.clear();
  cursor.retry_at = {};
  cursor.backoff  = std::chrono::seconds(1);
}
} // namespace data_sync

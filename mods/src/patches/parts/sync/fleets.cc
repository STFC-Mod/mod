// Live fleet state for the `fleets` category: one record per FleetState transition from fleet_watch, a full snapshot
// as a first sync, and late re-reads after server updates. Everything that reads the game runs on the game thread
// and only queues; uploads stay on the upload thread.

#include "patches/parts/sync/sync.h"

#include "patches/fleet_watch.h"

#include <prime/fleet.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace data_sync
{
namespace
{
  using json = nlohmann::json;

  constexpr int kFleetSlotCount = 10;

  // Set from any thread when another category's first sync is queued; the game thread answers with a full fleet
  // snapshot.
  std::atomic_bool snapshot_requested{false};

  // Fleets the server just sent new data for. The client fills in some values (mining and travel timers, damage) from
  // that data, so the game thread re-reads these fleets shortly after.
  std::mutex                                                          reread_mutex;
  std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> reread_at;

  // Set once fleet_watch accepted the subscription. Sync's frame work then drives fleet_watch.
  bool fleet_sync_installed = false;

  const char* StateName(FleetState state)
  {
    switch (state) {
      case FleetState::IdleInSpace:
        return "IdleInSpace";
      case FleetState::Docked:
        return "Docked";
      case FleetState::Mining:
        return "Mining";
      case FleetState::Destroyed:
        return "Destroyed";
      case FleetState::TieringUp:
        return "TieringUp";
      case FleetState::Repairing:
        return "Repairing";
      case FleetState::Battling:
        return "Battling";
      case FleetState::WarpCharging:
        return "WarpCharging";
      case FleetState::Warping:
        return "Warping";
      case FleetState::Impulsing:
        return "Impulsing";
      case FleetState::Capturing:
        return "Capturing";
      case FleetState::AutoHunting:
        return "AutoHunting";
      case FleetState::Outposting:
        return "Outposting";
      default:
        return "Unknown";
    }
  }

  int64_t UnixSeconds()
  {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  template <typename T> void Put(json& out, const char* key, const std::optional<T>& value)
  {
    if (value)
      out[key] = *value;
  }

  // Absent objects and unset times are left out; members missing from the client read as zero.
  void AddLiveState(json& out, FleetPlayerData* fleet, FleetState state)
  {
    const auto is_mining    = fleet->IsMining();
    const auto is_repairing = fleet->IsRepairing();

    const auto is_home      = fleet->IsHome();
    const auto is_destroyed = fleet->IsDestroyed();

    out["is_home"]      = is_home;
    out["is_deployed"]  = fleet->IsDeployed();
    out["is_mining"]    = is_mining;
    out["is_repairing"] = is_repairing;
    out["is_damaged"]   = fleet->IsDamaged();
    out["is_destroyed"] = is_destroyed;
    out["drydock"]      = fleet->DrydockID();
    // The client only updates this at the station; in space the server's hull points are the live value.
    if (is_home || is_destroyed)
      if (const auto damage = fleet->HullDamage())
        out["hull_damage"] = std::llround(*damage);
    out["cargo_fill"] = fleet->CargoResourceFillLevel();

    if (auto* hold = fleet->CargoHold()) {
      if (auto* current = hold->CurrentCargo()) {
        out["cargo"]     = current->CurrentValue();
        out["cargo_max"] = current->MaxValue();
      }
      if (auto* protected_cargo = hold->ProtectedCargoProgress())
        out["protected_cargo"] = protected_cargo->CurrentValue();
    }

    if (is_mining) {
      if (auto* slot = fleet->MiningData()) {
        out["node_id"]       = slot->ParentId();
        out["node_resource"] = slot->ResourceId();
        out["amount_mined"]  = slot->AmountMined();
        Put(out, "occupied_since", slot->OccupyTimeUtc().UnixSeconds());
        if (auto* node = slot->PointData())
          out["node_amount"] = node->Amount();
      }
      out["mining_rate"] = fleet->GetMiningRate();
      if (auto* timer = fleet->MiningSlotTimer())
        Put(out, "mining_due", timer->DueTime().UnixSeconds());
    }

    if (is_repairing)
      if (auto* timer = fleet->Timer())
        Put(out, "repair_due", timer->DueTime().UnixSeconds());

    if (state == FleetState::WarpCharging || state == FleetState::Warping || state == FleetState::Impulsing)
      if (auto* timer = fleet->Timer())
        Put(out, "travel_due", timer->DueTime().UnixSeconds());
  }

  // The values the client fills in late, as last sent per fleet; a re-read is only sent when they changed. Values
  // that move on their own are left out: cargo while mining, hull while repairing.
  std::unordered_map<uint64_t, size_t> sent_late;

  size_t LateFields(const json& record)
  {
    auto fields = json::object();
    for (const auto* key : {"is_damaged", "is_destroyed", "is_repairing", "repair_due", "travel_due", "hull_damage",
                            "mining_due", "node_id", "cargo_max"})
      if (const auto it = record.find(key); it != record.end())
        fields[key] = *it;
    if (record.value("is_repairing", false))
      fields.erase("hull_damage");
    return std::hash<json>{}(fields);
  }

  // Reads a FleetPlayerData that is valid for the current frame.
  json Record(int slot, FleetPlayerData* fleet, FleetState state, FleetState previous)
  {
    auto* hull = fleet->Hull();
    // Hull ids are 32-bit and run past 2^31 (e.g. Kumari 3005922948); take the value as unsigned before widening so
    // those never come out negative in the record.
    const int64_t hull_id = hull ? static_cast<int64_t>(static_cast<uint32_t>(hull->Id())) : 0;

    json out = {{"type", SyncConfig::Type::kFleets},
                {"fid", fleet->Id()},
                {"slot", slot},
                {"hull_id", hull_id},
                {"level", fleet->Level()},
                {"state", static_cast<int>(state)},
                {"state_name", StateName(state)},
                {"prev_state", static_cast<int>(previous)},
                {"prev_state_name", StateName(previous)},
                {"t", UnixSeconds()},
                {"src", "client"}};
    AddLiveState(out, fleet, state);
    sent_late[fleet->Id()] = LateFields(out);
    return out;
  }

  // A snapshot is the complete list of occupied slots, so it is sent as a first sync.
  void QueueSnapshot(const char* reason)
  {
    auto* manager = FleetsManager::Instance();
    if (!manager || !manager->HasFleetService())
      return;

    auto records = json::array();
    for (int slot = 0; slot < kFleetSlotCount; ++slot)
      if (auto* fleet = manager->GetFleetPlayerData(slot))
        records.push_back(Record(slot, fleet, fleet->CurrentState(), fleet->PreviousState()));

    if (!records.empty()) {
      logging::debug("PROCESS", "fleets", "Fleet snapshot ({}): {} fleets", reason, records.size());
      Queue(SyncConfig::Type::kFleets, records, true);
    }
  }

  // Each transition is its own upload, so Mining -> Docked -> Repairing between two uploads still sends all three.
  void OnTransition(const fleet_watch::Transition& transition)
  {
    if (!transition.fleet)
      return;
    auto records = json::array();
    records.push_back(Record(transition.after.slot, transition.fleet, transition.after.state, transition.before.state));
    Queue(SyncConfig::Type::kFleets, records);
  }

  void OnBaseline()
  {
    snapshot_requested.store(false, std::memory_order_relaxed);
    QueueSnapshot("baseline");
  }

  void RereadDue()
  {
    std::vector<uint64_t> due;
    {
      std::scoped_lock lock(reread_mutex);
      if (reread_at.empty())
        return;
      const auto now = std::chrono::steady_clock::now();
      std::erase_if(reread_at, [&](const auto& entry) {
        if (entry.second > now)
          return false;
        due.push_back(entry.first);
        return true;
      });
    }
    if (due.empty())
      return;

    auto* manager = FleetsManager::Instance();
    if (!manager || !manager->HasFleetService())
      return;

    auto records = json::array();
    for (int slot = 0; slot < kFleetSlotCount; ++slot) {
      auto* fleet = manager->GetFleetPlayerData(slot);
      if (!fleet || std::ranges::find(due, fleet->Id()) == due.end())
        continue;
      const auto before = sent_late[fleet->Id()];
      auto       record = Record(slot, fleet, fleet->CurrentState(), fleet->PreviousState());
      if (LateFields(record) != before)
        records.push_back(std::move(record));
    }
    if (!records.empty())
      Queue(SyncConfig::Type::kFleets, records);
  }
} // namespace

void InstallFleetSync()
{
  if (!Config::Get().sync.options.fleets)
    return;
  if (!fleet_watch::Subscribe({.on_transition = OnTransition, .on_baseline = OnBaseline})) {
    spdlog::warn("Sync: fleet state subscriber could not be installed; fleet sync disabled");
    return;
  }
  fleet_sync_installed = true;
  spdlog::info("Sync: fleet state sync enabled");
}

void RequestFleetSnapshot()
{ snapshot_requested.store(true, std::memory_order_release); }

void RereadFleet(uint64_t fleet_id)
{
  std::scoped_lock lock(reread_mutex);
  reread_at.try_emplace(fleet_id, std::chrono::steady_clock::now() + std::chrono::seconds(1));
}

void UpdateFleetSync()
{
  if (!fleet_sync_installed)
    return;
  fleet_watch::Tick();
  RereadDue();
  if (snapshot_requested.load(std::memory_order_relaxed) && snapshot_requested.exchange(false))
    QueueSnapshot("first sync");
}
} // namespace data_sync

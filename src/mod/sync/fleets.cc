// [sync] fleets: watches the fleet slots, sends one record per FleetState change, a full snapshot as a first sync,
// and late re-reads after server updates. Everything that reads the game runs on the game thread from sync's frame
// work and only publishes; uploads stay on the target workers.

#include "sync/sync.h"

#include "game/fleet.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace data_sync
{
namespace
{
  using namespace std::chrono_literals;

  constexpr int kFleetSlotCount = 10;

  // Set from any thread when another category's first sync is queued; the game thread answers with a full fleet
  // snapshot.
  std::atomic_bool snapshot_requested{false};

  // Fleets the server just sent new data for. The client fills in some values (mining and travel timers, damage) from
  // that data, so the game thread re-reads these fleets shortly after.
  std::mutex                                     reread_mutex;
  std::unordered_map<int64_t, Clock::time_point> reread_at;

  // Set once fleet sync started watching. Sync's frame work then polls the fleets, and the processing thread's
  // re-read requests are kept.
  std::atomic_bool fleet_sync_installed{false};

  // --- Records ---

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

    if (auto* hold = fleet->CargoHoldData()) {
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
      if (auto* timer = fleet->miningSlotTimer())
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
  std::unordered_map<int64_t, size_t> sent_late;

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
    const int64_t hull_id = hull ? static_cast<int64_t>(static_cast<uint32_t>(hull->id())) : 0;

    json out = {{"fid", fleet->Id()},
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

  // Every batch is its own journal entry, delivered in order and never merged with a later one: Mining -> Docked ->
  // Repairing between two uploads still sends all three.
  void Append(json records, bool snapshot)
  {
    static uint64_t sequence = 0;
    Publish(Stream::kFleets, {{json::array({++sequence, snapshot}), std::move(records)}}, SessionGeneration());
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
      logging::Debug("process", "fleets", "{} snapshot of {} fleets", reason, records.size());
      Append(std::move(records), true);
    }
  }

  void RereadDue()
  {
    std::vector<int64_t> due;
    {
      std::scoped_lock lock(reread_mutex);
      if (reread_at.empty())
        return;
      const auto now = Clock::now();
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
      Append(std::move(records), false);
  }

  // --- Fleet watch ---
  //
  // Every slot is read once a second. A state change of the fleet a slot already held is sent as a record. A slot
  // that empties, fills or changes hands is not a transition: it schedules a snapshot once the slots have been quiet
  // for kFleetSettle, which covers the client filling the slots over several frames after login. State changes seen
  // while a snapshot is pending go out with it.

  constexpr auto kFleetPollEvery = 1s;
  constexpr auto kFleetSettle    = 5s;

  class FleetWatcher
  {
  public:
    void Tick(Clock::time_point now)
    {
      if (now < next_poll_)
        return;
      next_poll_    = now + kFleetPollEvery;
      auto* manager = FleetsManager::Instance();
      // Logged out: forget the slots, so the next login fills them in afresh.
      if (!manager || !manager->HasFleetService()) {
        slots_ = {};
        snapshot_at_.reset();
        return;
      }
      for (int slot = 0; slot < kFleetSlotCount; ++slot) {
        auto* fleet = manager->GetFleetPlayerData(slot);
        auto& known = slots_[slot];
        if (!fleet) {
          if (known.fleet_id)
            Moved(now, slot, 0);
          known = {};
          continue;
        }
        const auto id    = fleet->Id();
        const auto state = fleet->CurrentState();
        if (known.fleet_id != id) {
          Moved(now, slot, id);
          known = {id, state};
          continue;
        }
        if (known.state == state)
          continue;
        const auto before = std::exchange(known.state, state);
        if (!snapshot_at_)
          Append(json::array({Record(slot, fleet, state, before)}), false);
      }
      if (snapshot_at_ && now >= *snapshot_at_) {
        snapshot_at_.reset();
        snapshot_requested.store(false, std::memory_order_relaxed);
        QueueSnapshot("slots changed");
      }
    }

  private:
    struct Slot {
      int64_t    fleet_id = 0;
      FleetState state    = FleetState::Unknown;
    };

    void Moved(Clock::time_point now, int slot, int64_t fleet_id)
    {
      logging::Debug("process", "fleets", "slot {} now holds fleet {}; snapshot in {} s", slot, fleet_id,
                     kFleetSettle.count());
      snapshot_at_ = now + kFleetSettle;
    }

    std::array<Slot, kFleetSlotCount> slots_{};
    Clock::time_point                 next_poll_{};
    std::optional<Clock::time_point>  snapshot_at_;
  };

  FleetWatcher fleet_watcher;

  // Starts watching the first frame a target wants fleets, so a reload that turns them on starts it too. It then
  // keeps running, and delivery filters per target as for every other category.
  bool FleetSyncInstalled()
  {
    if (fleet_sync_installed)
      return true;
    if (!Config::Sync::Get()->options.fleets)
      return false;
    fleet_sync_installed = true;
    spdlog::info("[Sync] fleet state sync enabled");
    return true;
  }
} // namespace

void RequestFleetSnapshot()
{ snapshot_requested.store(true, std::memory_order_release); }

void RereadFleet(int64_t fleet_id)
{
  // Deployed fleets are processed whether or not a target wants them; nothing would drain these.
  if (!fleet_sync_installed)
    return;
  std::scoped_lock lock(reread_mutex);
  reread_at.try_emplace(fleet_id, Clock::now() + 1s);
}

void UpdateFleetSync()
{
  if (!FleetSyncInstalled())
    return;
  fleet_watcher.Tick(Clock::now());
  RereadDue();
  if (snapshot_requested.load(std::memory_order_relaxed) && snapshot_requested.exchange(false))
    QueueSnapshot("first sync");
}
} // namespace data_sync

#pragma once

#include <array>
#include <chrono>
#include <cstdint>

// Forget the mod's pending response correlations at both queue toggle transitions.
// Releases weak tracking handles; never clears or modifies native target queues.
void ResetQueueRecoveryTracking();

namespace action_queue
{
using Clock = std::chrono::steady_clock;

struct QueueState {
  bool         valid{};
  std::int64_t fleet{}, front{};
  float        attempt{};
  int          count{};
  bool         engaging{}, containsTarget{};
};

// The pending-target field describes the last successful response, not necessarily the outstanding request.
inline bool CanAdvance(const QueueState& state, std::int64_t failedFleet, std::int64_t failedTarget,
                       bool outstandingAtEntry, bool sameQueue, bool absentFromAllQueues)
{
  return state.valid && sameQueue && outstandingAtEntry && failedTarget != 0 && state.fleet == failedFleet
         && state.count > 0 && state.front != failedTarget && !state.engaging && !state.containsTarget
         && absentFromAllQueues;
}

// Handles provides weak identity without keeping native queues alive. Calls are serialized by the adapter.
template <class Handles> class Requests
{
public:
  using Object = typename Handles::Object;
  using Handle = typename Handles::Handle;

  std::uint64_t Remember(Object queue, const QueueState& state, Clock::time_point now)
  {
    auto* slot = &records_.front();
    for (auto& record : records_) {
      if (record.handle && (now - record.started >= lifetime || !Handles::Get(record.handle)))
        Release(record);
      if (record.fleet == state.fleet) {
        slot = &record;
        break;
      }
      if (record.started < slot->started)
        slot = &record;
    }
    Release(*slot);
    const auto serial = ++serial_;
    if (state.valid && state.count > 0 && state.front != 0)
      *slot = {Handles::New(queue), state.fleet, state.front, state.attempt, now, serial};
    return serial;
  }

  // A native attempt that did not dispatch must not authorize a later failure response.
  void Cancel(std::uint64_t serial)
  {
    for (auto& record : records_)
      if (record.serial == serial)
        Release(record);
  }

  bool Consume(Object queue, const QueueState& state, std::int64_t target, Clock::time_point now)
  {
    for (auto& record : records_) {
      if (record.fleet != state.fleet)
        continue;
      if (!record.handle || now - record.started >= lifetime || !Handles::Get(record.handle)) {
        Release(record);
        return false;
      }
      if (Handles::Get(record.handle) != queue || record.target != target || record.attempt != state.attempt)
        return false;
      Release(record);
      return true;
    }
    return false;
  }

  void Clear()
  {
    for (auto& record : records_)
      Release(record);
  }

private:
  struct Record {
    Handle            handle{};
    std::int64_t      fleet{}, target{};
    float             attempt{};
    Clock::time_point started{};
    std::uint64_t     serial{};
  };
  static void Release(Record& record)
  {
    if (record.handle)
      Handles::Free(record.handle);
    record = {};
  }
  static constexpr auto lifetime = std::chrono::seconds(30);
  std::array<Record, 8> records_{};
  std::uint64_t         serial_{};
};
} // namespace action_queue

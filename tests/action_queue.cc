// The Kir'Shara queue fixes' decisions and request store, against fake weak handles. They do not model the game's
// ABI or network timing.

#include "features/action_queue.h"

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <iostream>
#include <map>
#include <utility>

using namespace action_queue;
using namespace std::chrono_literals;

namespace
{
// The shape of il2cpp::GcHandle, over a map that stands in for the GC. Collect clears every handle to an object.
class FakeHandle
{
public:
  static inline unsigned                serial{};
  static inline std::map<unsigned, int> live;

  FakeHandle() = default;
  static FakeHandle Weak(int object)
  {
    live[++serial] = object;
    return FakeHandle(serial);
  }
  ~FakeHandle()
  { Reset(); }
  FakeHandle(FakeHandle&& other) noexcept
      : id_(std::exchange(other.id_, 0))
  {
  }
  FakeHandle& operator=(FakeHandle&& other) noexcept
  {
    if (this != &other) {
      Reset();
      id_ = std::exchange(other.id_, 0);
    }
    return *this;
  }
  explicit operator bool() const
  { return id_ != 0; }
  int Get() const
  {
    auto it = live.find(id_);
    return it == live.end() ? 0 : it->second;
  }
  static void Collect(int object)
  {
    for (auto& [_, value] : live)
      if (value == object)
        value = 0;
  }

private:
  explicit FakeHandle(unsigned id)
      : id_(id)
  {
  }
  void Reset()
  {
    if (!id_)
      return;
    // Outside the assert, so the handle is released even when asserts are compiled out.
    [[maybe_unused]] const auto erased = live.erase(id_);
    assert(erased == 1);
    id_ = 0;
  }
  unsigned id_ = 0;
};

using Requests = action_queue::Requests<FakeHandle>;
const auto now = Clock::time_point{} + 100s;
// Fleet 10's queue when it dispatches its attempt on target 100, and after target 100 has gone.
const RetryState dispatch{.valid = true, .fleet = 10, .front = 100, .attempt = 2.5f, .count = 2};
const RetryState remaining{.valid = true, .fleet = 10, .front = 200, .attempt = 2.5f, .count = 1};

// A copy of `value` with one change, for the cases that differ from a baseline in a single field.
template <typename T, typename Change> T With(T value, Change change)
{
  change(value);
  return value;
}

// A queue of `count` targets of which only the head is captured.
QueueSnapshot Queue(int count, std::int64_t head)
{
  QueueSnapshot queue{.present = true, .player_fleet_id = 42, .count = count, .head_target_id = head};
  if (count > 0) {
    queue.target_ids[0]         = head;
    queue.captured_target_count = count;
  }
  return queue;
}

// A fully captured queue of `targets`.
QueueSnapshot Queue(std::initializer_list<std::int64_t> targets)
{
  const auto    count = static_cast<int>(targets.size());
  QueueSnapshot queue{.present               = true,
                      .player_fleet_id       = 42,
                      .count                 = count,
                      .head_target_id        = count > 0 ? *targets.begin() : 0,
                      .captured_target_count = count};
  std::ranges::copy(targets, queue.target_ids.begin());
  return queue;
}

void TestRecovery()
{
  Requests requests;
  // Removed target: exactly one immediate handoff. Pending-success state is intentionally not an input.
  requests.Remember(1, dispatch, now);
  assert(CanAdvance(remaining, 10, 100, true, true, true));
  assert(requests.Consume(1, remaining, 100, now + 1s));
  assert(!requests.Consume(1, remaining, 100, now + 2s));
  assert(FakeHandle::live.empty());

  // Native retry remains responsible while the target is still queued, even after reordering.
  assert(!CanAdvance(With(remaining, [](auto& s) { s.contains_target = true; }), 10, 100, true, true, true));
  assert(!CanAdvance(dispatch, 10, 100, true, true, true));
  // Cross-fleet target cleanup must retain native false-branch behavior without consuming the request.
  requests.Remember(1, dispatch, now);
  assert(!CanAdvance(remaining, 10, 100, true, true, false));
  assert(requests.Consume(1, remaining, 100, now + 1s));
  // Invalid identity, missing/empty queue, ongoing engagement and absent outstanding request fail closed.
  assert(!CanAdvance(remaining, 11, 100, true, true, true));
  assert(!CanAdvance(remaining, 10, 100, false, true, true));
  assert(!CanAdvance(remaining, 10, 100, true, false, true));
  assert(!CanAdvance(remaining, 10, 0, true, true, true));
  assert(!CanAdvance(With(remaining, [](auto& s) { s.valid = false; }), 10, 100, true, true, true));
  assert(!CanAdvance(With(remaining, [](auto& s) { s.count = 0; }), 10, 100, true, true, true));
  assert(!CanAdvance(With(remaining, [](auto& s) { s.engaging = true; }), 10, 100, true, true, true));

  // Latest target/attempt supersedes old failures; pointer reuse after collection cannot resurrect identity.
  requests.Remember(1, dispatch, now);
  assert(!requests.Consume(2, remaining, 100, now));
  auto newer    = dispatch;
  newer.front   = 300;
  newer.attempt = 3;
  requests.Remember(1, newer, now + 1s);
  assert(!requests.Consume(1, remaining, 100, now + 2s));
  requests.Remember(1, dispatch, now);
  assert(!requests.Consume(1, With(remaining, [](auto& s) { s.attempt = 9; }), 100, now));
  FakeHandle::Collect(1);
  assert(!requests.Consume(1, remaining, 100, now));
  assert(FakeHandle::live.empty());

  // Canceling an older reentrant call cannot cancel the newer dispatch.
  auto old = requests.Remember(1, dispatch, now);
  requests.Remember(1, dispatch, now + 1s);
  requests.Cancel(old);
  assert(requests.Consume(1, remaining, 100, now + 2s));
  auto failed = requests.Remember(1, dispatch, now);
  requests.Cancel(failed);
  assert(!requests.Consume(1, remaining, 100, now));

  requests.Remember(1, dispatch, now);
  assert(!requests.Consume(1, remaining, 100, now + 30s));
  assert(FakeHandle::live.empty());
  // Bounded capacity, independent fleets and session cleanup release all handles.
  for (int fleet = 1; fleet <= 9; ++fleet) {
    requests.Remember(fleet, With(dispatch, [&](auto& s) { s.fleet = fleet; }), now + std::chrono::seconds(fleet));
    assert(FakeHandle::live.size() <= 8);
  }
  assert(!requests.Consume(1, With(remaining, [](auto& s) { s.fleet = 1; }), 100, now + 10s));
  assert(requests.Consume(9, With(remaining, [](auto& s) { s.fleet = 9; }), 100, now + 10s));
  requests.Clear();
  requests.Clear();
  assert(FakeHandle::live.empty());
  assert(!requests.Consume(2, remaining, 100, now));
}

void TestThinProtection()
{
  // Native engage results have stable diagnostic names.
  static_assert(EngageResultName(0) == "success");
  static_assert(EngageResultName(1) == "skip-target");
  static_assert(EngageResultName(2) == "stop");
  static_assert(EngageResultName(-1) == "not-attempted");
  static_assert(EngageResultName(99) == "unknown");

  // Destroyed-head processing is exact and gated on the setting.
  {
    const auto queue = Queue(2, 101);

    assert(ShouldProcessDestroyedHead(true, true, 101, queue));
    assert(!ShouldProcessDestroyedHead(false, true, 101, queue));
    assert(!ShouldProcessDestroyedHead(true, false, 101, queue));
    assert(!ShouldProcessDestroyedHead(true, true, 202, queue));
    assert(!ShouldProcessDestroyedHead(true, true, 0, queue));
  }

  // Destroyed-head processing refuses active queue latches.
  {
    auto queue = Queue(2, 101);

    queue.is_engaging = true;
    assert(!ShouldProcessDestroyedHead(true, true, 101, queue));

    queue.is_engaging            = false;
    queue.last_engaged_target_id = 101;
    assert(!ShouldProcessDestroyedHead(true, true, 101, queue));

    queue.last_engaged_target_id = 0;
    queue.pending_target_id      = 101;
    assert(!ShouldProcessDestroyedHead(true, true, 101, queue));
  }

  // A local kill and repeated disposal are no-ops once the exact target is absent.
  {
    const auto next_head = Queue(1, 202);
    const auto empty     = Queue(0, 0);

    assert(!ShouldProcessDestroyedHead(true, true, 101, next_head));
    assert(!ShouldProcessDestroyedHead(true, true, 101, empty));
  }

  // A native prune leaves a stranded new head to resume.
  {
    const auto before = Queue({101, 202, 303, 404});
    const auto after  = Queue({303, 404});

    assert(IsNativePruneResumeCandidate(true, true, before, after));
  }

  // Temporary unavailability without a native removal is not a candidate.
  {
    const auto before = Queue({101, 202});
    const auto after  = Queue({101, 202});

    assert(!IsNativePruneResumeCandidate(true, true, before, after));
  }

  // Resuming requires an idle fleet and rejects latches on the surviving suffix.
  {
    const auto before = Queue({101, 202, 303});

    auto after = Queue({202, 303});
    assert(!IsNativePruneResumeCandidate(false, true, before, after));
    assert(!IsNativePruneResumeCandidate(true, false, before, after));

    after.is_engaging = true;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));

    after.is_engaging            = false;
    after.last_engaged_target_id = 202;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));

    after.last_engaged_target_id = 0;
    after.pending_target_id      = 202;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));
  }

  // Resuming accepts no-target sentinels and stale latches from the removed prefix.
  {
    const auto before = Queue({101, 202, 303, 404});

    auto after                   = Queue({303, 404});
    after.last_engaged_target_id = 101;
    after.pending_target_id      = 202;
    assert(IsNativePruneResumeCandidate(true, true, before, after));

    after.last_engaged_target_id = -1;
    after.pending_target_id      = -1;
    assert(IsNativePruneResumeCandidate(true, true, before, after));
  }

  // Resuming rejects unrelated positive native latches.
  {
    const auto before = Queue({101, 202, 303});

    auto after              = Queue({303});
    after.pending_target_id = 909;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));

    after.pending_target_id      = 0;
    after.last_engaged_target_id = 909;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));
  }

  // A manual clear and repeated watchdog callbacks are no-ops.
  {
    const auto before = Queue({101, 202});
    const auto empty  = Queue(0, 0);
    const auto stable = Queue({202});

    assert(!IsNativePruneResumeCandidate(true, true, before, empty));
    assert(!IsNativePruneResumeCandidate(true, true, stable, stable));
  }

  // Resuming fails closed on reordering, truncation and fleet replacement.
  {
    const auto before = Queue({101, 202, 303, 404});

    auto after = Queue({404, 303});
    assert(!IsNativePruneResumeCandidate(true, true, before, after));

    after                   = Queue({303, 404});
    after.targets_truncated = true;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));

    after                 = Queue({303, 404});
    after.player_fleet_id = 84;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));
  }

  // The resume postcondition requires the exact same idle surviving suffix.
  {
    const auto expected = Queue({303, 404});

    assert(IsStableResumePostcondition(expected, expected));

    auto confirmed          = expected;
    confirmed.target_ids[1] = 505;
    assert(!IsStableResumePostcondition(expected, confirmed));

    confirmed             = expected;
    confirmed.is_engaging = true;
    assert(!IsStableResumePostcondition(expected, confirmed));

    confirmed                   = expected;
    confirmed.pending_target_id = 303;
    assert(!IsStableResumePostcondition(expected, confirmed));

    auto stale_latch                   = expected;
    stale_latch.last_engaged_target_id = 101;
    stale_latch.pending_target_id      = 202;
    assert(IsStableResumePostcondition(stale_latch, stale_latch));

    confirmed                   = stale_latch;
    confirmed.pending_target_id = 101;
    assert(!IsStableResumePostcondition(stale_latch, confirmed));

    stale_latch.pending_target_id = 303;
    assert(!IsStableResumePostcondition(stale_latch, stale_latch));

    confirmed                 = expected;
    confirmed.player_fleet_id = 84;
    assert(!IsStableResumePostcondition(expected, confirmed));

    confirmed                       = expected;
    confirmed.count                 = 1;
    confirmed.head_target_id        = 404;
    confirmed.target_ids[0]         = 404;
    confirmed.target_ids[1]         = 0;
    confirmed.captured_target_count = 1;
    assert(!IsStableResumePostcondition(expected, confirmed));
  }
}

void TestAddressGuard()
{
  const Address base{.valid = true, .galaxy = 0, .system = 71088, .planet = 71237, .instance = 710};
  const Address system{.valid = true, .galaxy = 0, .system = 925162490, .planet = 0, .instance = 710};
  // The reproduced case: an impulsing fleet reported at its starbase planet, in another system, while its deployment
  // and its target are both still in space.
  const AddressEvidence reproduced{.enabled            = true,
                                   .player_state_event = true,
                                   .owned_queue        = true,
                                   .owned_deployment   = true,
                                   .planning_recall    = false,
                                   .player_state       = kFleetImpulsing,
                                   .deployed_state     = kDeployedMoving,
                                   .removal_reason     = 0,
                                   .reported           = base,
                                   .deployed           = system,
                                   .target             = system};
  assert(UseDeployedAddress(reproduced));
  for (int state : {1, 64, 512})
    assert(UseDeployedAddress(With(reproduced, [&](auto& e) { e.player_state = state; })));
  // Destruction, dock/repair, and an intentional warp must retain native clearing.
  for (int state : {-1, 0, 2, 4, 8, 16, 32, 128, 256})
    assert(!UseDeployedAddress(With(reproduced, [&](auto& e) { e.player_state = state; })));
  for (int state : {-1, 2, 4, 5, 99})
    assert(!UseDeployedAddress(With(reproduced, [&](auto& e) { e.deployed_state = state; })));
  for (int reason : {-1, 1, 2, 3, 4, 5})
    assert(!UseDeployedAddress(With(reproduced, [&](auto& e) { e.removal_reason = reason; })));

  const auto rejected = [&](auto change) { return !UseDeployedAddress(With(reproduced, change)); };
  assert(rejected([](auto& e) { e.enabled = false; }));
  assert(rejected([](auto& e) { e.player_state_event = false; })); // manual clear/UI path
  assert(rejected([](auto& e) { e.owned_queue = false; }));
  assert(rejected([](auto& e) { e.owned_deployment = false; })); // wrong fleet or unknown ownership
  assert(rejected([](auto& e) { e.planning_recall = true; }));
  assert(rejected([](auto& e) { e.reported.valid = false; }));
  assert(rejected([](auto& e) { e.deployed.valid = false; }));
  assert(rejected([](auto& e) { e.target.valid = false; }));
  assert(rejected([](auto& e) { e.target.system = 123; })); // real foreign target
  assert(rejected([](auto& e) { e.target.instance = 711; }));
  assert(rejected([](auto& e) { e.target.planet = 123; }));
  assert(rejected([](auto& e) { e.reported.planet = 0; })); // scope is the observed base substitution
  assert(rejected([&](auto& e) { e.reported.system = system.system; }));
  assert(rejected([](auto& e) { e.reported.instance = 711; }));
  assert(rejected([](auto& e) { e.reported.galaxy = 1; }));
  assert(rejected([&](auto& e) { e.deployed = base; }));   // own deployment really moved to base
  assert(rejected([&](auto& e) { e.reported = system; })); // no wrong address to correct
}
} // namespace

int main()
{
  TestRecovery();
  TestThinProtection();
  TestAddressGuard();
  std::cout << "Action queue tests passed\n";
}

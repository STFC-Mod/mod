// The Kir'Shara queue fixes' decisions and request store, against fake weak handles. They do not model the game's
// ABI or network timing.

#include "features/action_queue.h"

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
    if (id_)
      assert(live.erase(std::exchange(id_, 0)) == 1);
  }
  unsigned id_ = 0;
};

using Requests = action_queue::Requests<FakeHandle>;
const auto now = Clock::time_point{} + 100s;
RetryState dispatch{true, 10, 100, 2.5f, 2, false, false};
RetryState remaining{true, 10, 200, 2.5f, 1, false, false};

QueueSnapshot Queue(int count, std::int64_t head)
{
  auto queue = QueueSnapshot{
      .present         = true,
      .player_fleet_id = 42,
      .count           = count,
      .head_target_id  = head,
  };
  if (count > 0) {
    queue.target_ids[0]         = head;
    queue.captured_target_count = count;
  }
  return queue;
}

QueueSnapshot Queue(std::initializer_list<std::int64_t> targets)
{
  QueueSnapshot queue{
      .present         = true,
      .player_fleet_id = 42,
      .count           = static_cast<int>(targets.size()),
  };
  int index = 0;
  for (const auto target : targets) {
    queue.target_ids[index++] = target;
  }
  queue.captured_target_count = queue.count;
  queue.head_target_id        = queue.count > 0 ? queue.target_ids[0] : 0;
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
  auto reordered            = remaining;
  reordered.contains_target = true;
  assert(!CanAdvance(reordered, 10, 100, true, true, true));
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
  auto invalid  = remaining;
  invalid.valid = false;
  assert(!CanAdvance(invalid, 10, 100, true, true, true));
  auto empty  = remaining;
  empty.count = 0;
  assert(!CanAdvance(empty, 10, 100, true, true, true));
  auto active     = remaining;
  active.engaging = true;
  assert(!CanAdvance(active, 10, 100, true, true, true));

  // Latest target/attempt supersedes old failures; pointer reuse after collection cannot resurrect identity.
  requests.Remember(1, dispatch, now);
  assert(!requests.Consume(2, remaining, 100, now));
  auto newer    = dispatch;
  newer.front   = 300;
  newer.attempt = 3;
  requests.Remember(1, newer, now + 1s);
  assert(!requests.Consume(1, remaining, 100, now + 2s));
  requests.Remember(1, dispatch, now);
  auto differentAttempt    = remaining;
  differentAttempt.attempt = 9;
  assert(!requests.Consume(1, differentAttempt, 100, now));
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
  for (int i = 1; i <= 9; ++i) {
    auto s  = dispatch;
    s.fleet = i;
    requests.Remember(i, s, now + std::chrono::seconds(i));
    assert(FakeHandle::live.size() <= 8);
  }
  auto first  = remaining;
  first.fleet = 1;
  assert(!requests.Consume(1, first, 100, now + 10s));
  auto last  = remaining;
  last.fleet = 9;
  assert(requests.Consume(9, last, 100, now + 10s));
  requests.Clear();
  requests.Clear();
  assert(FakeHandle::live.empty());
  assert(!requests.Consume(2, remaining, 100, now));
}

void TestThinProtection()
{
  // native engage results have stable diagnostic names
  {
    assert(EngageResultName(0).compare("success") == 0);
    assert(EngageResultName(1).compare("skip-target") == 0);
    assert(EngageResultName(2).compare("stop") == 0);
    assert(EngageResultName(-1).compare("not-attempted") == 0);
    assert(EngageResultName(99).compare("unknown") == 0);
  }

  // destroyed head processing is exact and feature gated
  {
    const auto queue = Queue(2, 101);

    assert(ShouldProcessDestroyedHead(true, true, 101, queue));
    assert(!ShouldProcessDestroyedHead(false, true, 101, queue));
    assert(!ShouldProcessDestroyedHead(true, false, 101, queue));
    assert(!ShouldProcessDestroyedHead(true, true, 202, queue));
    assert(!ShouldProcessDestroyedHead(true, true, 0, queue));
  }

  // destroyed head processing refuses active queue latches
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

  // local kill and repeated disposal are no-ops once the exact target is absent
  {
    const auto next_head = Queue(1, 202);
    const auto empty     = Queue(0, 0);

    assert(!ShouldProcessDestroyedHead(true, true, 101, next_head));
    assert(!ShouldProcessDestroyedHead(true, true, 101, empty));
  }

  // native prune identifies a stranded new-head resume candidate
  {
    const auto before = Queue({101, 202, 303, 404});
    const auto after  = Queue({303, 404});

    assert(IsNativePruneResumeCandidate(true, true, before, after));
  }

  // temporary unavailability without native removal is not a candidate
  {
    const auto before = Queue({101, 202});
    const auto after  = Queue({101, 202});

    assert(!IsNativePruneResumeCandidate(true, true, before, after));
  }

  // resume requires idle state and rejects latches on the surviving suffix
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

  // resume accepts no-target sentinels and stale latches from the removed prefix
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

  // resume rejects unrelated positive native latches
  {
    const auto before = Queue({101, 202, 303});

    auto after              = Queue({303});
    after.pending_target_id = 909;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));

    after.pending_target_id      = 0;
    after.last_engaged_target_id = 909;
    assert(!IsNativePruneResumeCandidate(true, true, before, after));
  }

  // manual clear and repeated watchdog callbacks are no-ops
  {
    const auto before = Queue({101, 202});
    const auto empty  = Queue(0, 0);
    const auto stable = Queue({202});

    assert(!IsNativePruneResumeCandidate(true, true, before, empty));
    assert(!IsNativePruneResumeCandidate(true, true, stable, stable));
  }

  // resume fails closed on reorder truncation and fleet replacement
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

  // resume postcondition requires the exact same idle surviving suffix
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
  const Address         base{true, 0, 71088, 71237, 710};
  const Address         system{true, 0, 925162490, 0, 710};
  const AddressEvidence reproduced{true, true, true, true, false, 512, 1, 0, base, system, system};
  assert(UseDeployedAddress(reproduced));
  for (int state : {1, 64, 512}) {
    auto e         = reproduced;
    e.player_state = state;
    assert(UseDeployedAddress(e));
  }
  // Destruction, dock/repair, and an intentional warp must retain native clearing.
  for (int state : {-1, 0, 2, 4, 8, 16, 32, 128, 256}) {
    auto e         = reproduced;
    e.player_state = state;
    assert(!UseDeployedAddress(e));
  }
  for (int state : {-1, 2, 4, 5, 99}) {
    auto e           = reproduced;
    e.deployed_state = state;
    assert(!UseDeployedAddress(e));
  }
  for (int reason : {-1, 1, 2, 3, 4, 5}) {
    auto e           = reproduced;
    e.removal_reason = reason;
    assert(!UseDeployedAddress(e));
  }
  auto reject = [](AddressEvidence e) { assert(!UseDeployedAddress(e)); };
  auto e      = reproduced;
  e.enabled   = false;
  reject(e);
  e                    = reproduced;
  e.player_state_event = false;
  reject(e); // manual clear/UI path
  e             = reproduced;
  e.owned_queue = false;
  reject(e);
  e                  = reproduced;
  e.owned_deployment = false;
  reject(e); // wrong fleet or unknown ownership
  e                 = reproduced;
  e.planning_recall = true;
  reject(e);
  e                = reproduced;
  e.reported.valid = false;
  reject(e);
  e                = reproduced;
  e.deployed.valid = false;
  reject(e);
  e              = reproduced;
  e.target.valid = false;
  reject(e);
  e               = reproduced;
  e.target.system = 123;
  reject(e); // real foreign target
  e                 = reproduced;
  e.target.instance = 711;
  reject(e);
  e               = reproduced;
  e.target.planet = 123;
  reject(e);
  e                 = reproduced;
  e.reported.planet = 0;
  reject(e); // scope is the observed base substitution
  e                 = reproduced;
  e.reported.system = system.system;
  reject(e);
  e                   = reproduced;
  e.reported.instance = 711;
  reject(e);
  e                 = reproduced;
  e.reported.galaxy = 1;
  reject(e);
  e          = reproduced;
  e.deployed = base;
  reject(e); // own deployment really moved to base
  e          = reproduced;
  e.reported = system;
  reject(e); // no wrong address to correct
}
} // namespace

int main()
{
  TestRecovery();
  TestThinProtection();
  TestAddressGuard();
  std::cout << "Action queue tests passed\n";
}

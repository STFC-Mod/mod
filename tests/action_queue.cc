// The Kir'Shara queue fixes' decisions and attempt store. They do not model the game's ABI or network timing.

#include "features/action_queue.h"

#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

using namespace action_queue;

namespace
{
// Fleet 10's queue when it dispatches its attempt on target 100, and after target 100 has gone.
const RetryState dispatch{.valid = true, .fleet = 10, .front = 100, .attempt = 2.5f, .count = 2};
const RetryState remaining{.valid = true, .fleet = 10, .front = 200, .attempt = 2.5f, .count = 1};

// A copy of `value` with one change, for the cases that differ from a baseline in a single field.
template <typename T, typename Change> T With(T value, Change change)
{
  change(value);
  return value;
}

QueueSnapshot Queue(std::vector<std::int64_t> targets)
{ return {.present = true, .player_fleet_id = 42, .targets = std::move(targets)}; }

void TestRecovery()
{
  Attempts attempts;
  // Removed target: exactly one immediate handoff. Pending-success state is intentionally not an input.
  attempts.Remember(3, dispatch);
  assert(CanAdvance(remaining, 100));
  assert(attempts.Consume(remaining, 100));
  assert(!attempts.Consume(remaining, 100));

  // Native retry remains responsible while the target is still queued, even after reordering.
  assert(!CanAdvance(With(remaining, [](auto& s) { s.contains_target = true; }), 100));
  assert(!CanAdvance(dispatch, 100));
  // Missing or empty queue, unknown target and ongoing engagement fail closed.
  assert(!CanAdvance(remaining, 0));
  assert(!CanAdvance(With(remaining, [](auto& s) { s.valid = false; }), 100));
  assert(!CanAdvance(With(remaining, [](auto& s) { s.count = 0; }), 100));
  assert(!CanAdvance(With(remaining, [](auto& s) { s.engaging = true; }), 100));

  // A failure only answers the latest attempt of the same fleet, on the same target at the same attempt time.
  attempts.Remember(3, dispatch);
  assert(!attempts.Consume(With(remaining, [](auto& s) { s.fleet = 11; }), 100));
  attempts.Remember(3, With(dispatch, [](auto& s) {
                      s.front   = 300;
                      s.attempt = 3;
                    }));
  assert(!attempts.Consume(remaining, 100));
  attempts.Remember(3, dispatch);
  assert(!attempts.Consume(With(remaining, [](auto& s) { s.attempt = 9; }), 100));
  // An attempt without a readable head clears the slot rather than leave an older attempt to answer for it.
  attempts.Remember(3, dispatch);
  attempts.Remember(3, With(dispatch, [](auto& s) { s.valid = false; }));
  assert(!attempts.Consume(remaining, 100));

  // An attempt that did not dispatch is forgotten.
  attempts.Remember(3, dispatch);
  attempts.Forget(3);
  assert(!attempts.Consume(remaining, 100));

  // Fleets keep their own slots; slots past the dock count are ignored.
  for (int slot = 0; slot < Attempts::kSlots; ++slot)
    attempts.Remember(slot, With(dispatch, [&](auto& s) { s.fleet = 1000 + slot; }));
  attempts.Remember(Attempts::kSlots, dispatch);
  attempts.Remember(-1, dispatch);
  assert(!attempts.Consume(remaining, 100));
  for (int slot = 0; slot < Attempts::kSlots; ++slot)
    assert(attempts.Consume(With(remaining, [&](auto& s) { s.fleet = 1000 + slot; }), 100));
}

void TestThinProtection()
{
  // Destroyed-head processing is exact.
  {
    const auto queue = Queue({101, 202});

    assert(ShouldProcessDestroyedHead(101, queue));
    assert(!ShouldProcessDestroyedHead(202, queue));
    assert(!ShouldProcessDestroyedHead(0, queue));
  }

  // Destroyed-head processing refuses active queue latches.
  {
    auto queue = Queue({101, 202});

    queue.is_engaging = true;
    assert(!ShouldProcessDestroyedHead(101, queue));

    queue.is_engaging            = false;
    queue.last_engaged_target_id = 101;
    assert(!ShouldProcessDestroyedHead(101, queue));

    queue.last_engaged_target_id = 0;
    queue.pending_target_id      = 101;
    assert(!ShouldProcessDestroyedHead(101, queue));
  }

  // A local kill and repeated disposal are no-ops once the exact target is absent.
  {
    assert(!ShouldProcessDestroyedHead(101, Queue({202})));
    assert(!ShouldProcessDestroyedHead(101, Queue({})));
  }

  // A native prune leaves a stranded new head to resume.
  {
    const auto before = Queue({101, 202, 303, 404});
    const auto after  = Queue({303, 404});

    assert(IsNativePruneResumeCandidate(true, before, after));
  }

  // Temporary unavailability without a native removal is not a candidate.
  {
    const auto before = Queue({101, 202});
    const auto after  = Queue({101, 202});

    assert(!IsNativePruneResumeCandidate(true, before, after));
  }

  // Resuming requires an idle fleet and rejects latches on the surviving suffix.
  {
    const auto before = Queue({101, 202, 303});

    auto after = Queue({202, 303});
    assert(!IsNativePruneResumeCandidate(false, before, after));

    after.is_engaging = true;
    assert(!IsNativePruneResumeCandidate(true, before, after));

    after.is_engaging            = false;
    after.last_engaged_target_id = 202;
    assert(!IsNativePruneResumeCandidate(true, before, after));

    after.last_engaged_target_id = 0;
    after.pending_target_id      = 202;
    assert(!IsNativePruneResumeCandidate(true, before, after));
  }

  // Resuming accepts no-target sentinels and stale latches from the removed prefix.
  {
    const auto before = Queue({101, 202, 303, 404});

    auto after                   = Queue({303, 404});
    after.last_engaged_target_id = 101;
    after.pending_target_id      = 202;
    assert(IsNativePruneResumeCandidate(true, before, after));

    after.last_engaged_target_id = -1;
    after.pending_target_id      = -1;
    assert(IsNativePruneResumeCandidate(true, before, after));
  }

  // Resuming rejects unrelated positive native latches.
  {
    const auto before = Queue({101, 202, 303});

    auto after              = Queue({303});
    after.pending_target_id = 909;
    assert(!IsNativePruneResumeCandidate(true, before, after));

    after.pending_target_id      = 0;
    after.last_engaged_target_id = 909;
    assert(!IsNativePruneResumeCandidate(true, before, after));
  }

  // A manual clear and repeated watchdog callbacks are no-ops.
  {
    const auto before = Queue({101, 202});
    const auto stable = Queue({202});

    assert(!IsNativePruneResumeCandidate(true, before, Queue({})));
    assert(!IsNativePruneResumeCandidate(true, stable, stable));
  }

  // Resuming fails closed on reordering and fleet replacement, and reads queues of any length.
  {
    const auto before = Queue({101, 202, 303, 404});

    auto after = Queue({404, 303});
    assert(!IsNativePruneResumeCandidate(true, before, after));

    after                 = Queue({303, 404});
    after.player_fleet_id = 84;
    assert(!IsNativePruneResumeCandidate(true, before, after));

    const auto long_before = Queue({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
    assert(IsNativePruneResumeCandidate(true, long_before, Queue({3, 4, 5, 6, 7, 8, 9, 10})));
  }
}

void TestAddressGuard()
{
  const Address base{.valid = true, .galaxy = 0, .system = 71088, .planet = 71237, .instance = 710};
  const Address system{.valid = true, .galaxy = 0, .system = 925162490, .planet = 0, .instance = 710};
  // The reproduced case: an impulsing fleet reported at its starbase planet, in another system, while its deployment
  // and its target are both still in space.
  const AddressEvidence reproduced{.planning_recall = false,
                                   .player_state    = kFleetImpulsing,
                                   .deployed_state  = kDeployedMoving,
                                   .removal_reason  = 0,
                                   .reported        = base,
                                   .deployed        = system,
                                   .target          = system};
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

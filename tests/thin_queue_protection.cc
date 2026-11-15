#include "patches/action_queue_guard_policy.h"

#include <cassert>
#include <initializer_list>
#include <iostream>

namespace
{
using action_queue_guard::QueueState;

QueueState Queue(int count, std::int64_t head)
{
  auto queue = QueueState{
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

QueueState Queue(std::initializer_list<std::int64_t> targets)
{
  QueueState queue{
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
} // namespace

int main()
{
  // native engage results have stable diagnostic names
  {
    assert(action_queue_guard::EngageResultName(0).compare("success") == 0);
    assert(action_queue_guard::EngageResultName(1).compare("skip-target") == 0);
    assert(action_queue_guard::EngageResultName(2).compare("stop") == 0);
    assert(action_queue_guard::EngageResultName(-1).compare("not-attempted") == 0);
    assert(action_queue_guard::EngageResultName(99).compare("unknown") == 0);
  }

  // destroyed head processing is exact and feature gated
  {
    const auto queue = Queue(2, 101);

    assert(action_queue_guard::ShouldProcessDestroyedHead(true, true, 101, queue));
    assert(!(action_queue_guard::ShouldProcessDestroyedHead(false, true, 101, queue)));
    assert(!(action_queue_guard::ShouldProcessDestroyedHead(true, false, 101, queue)));
    assert(!(action_queue_guard::ShouldProcessDestroyedHead(true, true, 202, queue)));
    assert(!(action_queue_guard::ShouldProcessDestroyedHead(true, true, 0, queue)));
  }

  // destroyed head processing refuses active queue latches
  {
    auto queue = Queue(2, 101);

    queue.is_engaging = true;
    assert(!(action_queue_guard::ShouldProcessDestroyedHead(true, true, 101, queue)));

    queue.is_engaging            = false;
    queue.last_engaged_target_id = 101;
    assert(!(action_queue_guard::ShouldProcessDestroyedHead(true, true, 101, queue)));

    queue.last_engaged_target_id = 0;
    queue.pending_target_id      = 101;
    assert(!(action_queue_guard::ShouldProcessDestroyedHead(true, true, 101, queue)));
  }

  // local kill and repeated disposal are no-ops once the exact target is absent
  {
    const auto next_head = Queue(1, 202);
    const auto empty     = Queue(0, 0);

    assert(!(action_queue_guard::ShouldProcessDestroyedHead(true, true, 101, next_head)));
    assert(!(action_queue_guard::ShouldProcessDestroyedHead(true, true, 101, empty)));
  }

  // native prune identifies a stranded new-head resume candidate
  {
    const auto before = Queue({101, 202, 303, 404});
    const auto after  = Queue({303, 404});

    assert(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after));
  }

  // temporary unavailability without native removal is not a candidate
  {
    const auto before = Queue({101, 202});
    const auto after  = Queue({101, 202});

    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));
  }

  // resume requires idle state and rejects latches on the surviving suffix
  {
    const auto before = Queue({101, 202, 303});

    auto after = Queue({202, 303});
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(false, true, before, after)));
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, false, before, after)));

    after.is_engaging = true;
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));

    after.is_engaging            = false;
    after.last_engaged_target_id = 202;
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));

    after.last_engaged_target_id = 0;
    after.pending_target_id      = 202;
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));
  }

  // resume accepts no-target sentinels and stale latches from the removed prefix
  {
    const auto before = Queue({101, 202, 303, 404});

    auto after                   = Queue({303, 404});
    after.last_engaged_target_id = 101;
    after.pending_target_id      = 202;
    assert(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after));

    after.last_engaged_target_id = -1;
    after.pending_target_id      = -1;
    assert(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after));
  }

  // resume rejects unrelated positive native latches
  {
    const auto before = Queue({101, 202, 303});

    auto after              = Queue({303});
    after.pending_target_id = 909;
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));

    after.pending_target_id      = 0;
    after.last_engaged_target_id = 909;
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));
  }

  // manual clear and repeated watchdog callbacks are no-ops
  {
    const auto before = Queue({101, 202});
    const auto empty  = Queue(0, 0);
    const auto stable = Queue({202});

    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, empty)));
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, stable, stable)));
  }

  // resume fails closed on reorder truncation and fleet replacement
  {
    const auto before = Queue({101, 202, 303, 404});

    auto after = Queue({404, 303});
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));

    after                   = Queue({303, 404});
    after.targets_truncated = true;
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));

    after                 = Queue({303, 404});
    after.player_fleet_id = 84;
    assert(!(action_queue_guard::IsNativePruneResumeCandidate(true, true, before, after)));
  }

  // resume postcondition requires the exact same idle surviving suffix
  {
    const auto expected = Queue({303, 404});

    assert(action_queue_guard::IsStableResumePostcondition(expected, expected));

    auto confirmed          = expected;
    confirmed.target_ids[1] = 505;
    assert(!(action_queue_guard::IsStableResumePostcondition(expected, confirmed)));

    confirmed             = expected;
    confirmed.is_engaging = true;
    assert(!(action_queue_guard::IsStableResumePostcondition(expected, confirmed)));

    confirmed                   = expected;
    confirmed.pending_target_id = 303;
    assert(!(action_queue_guard::IsStableResumePostcondition(expected, confirmed)));

    auto stale_latch                   = expected;
    stale_latch.last_engaged_target_id = 101;
    stale_latch.pending_target_id      = 202;
    assert(action_queue_guard::IsStableResumePostcondition(stale_latch, stale_latch));

    confirmed                   = stale_latch;
    confirmed.pending_target_id = 101;
    assert(!(action_queue_guard::IsStableResumePostcondition(stale_latch, confirmed)));

    stale_latch.pending_target_id = 303;
    assert(!(action_queue_guard::IsStableResumePostcondition(stale_latch, stale_latch)));

    confirmed                 = expected;
    confirmed.player_fleet_id = 84;
    assert(!(action_queue_guard::IsStableResumePostcondition(expected, confirmed)));

    confirmed                       = expected;
    confirmed.count                 = 1;
    confirmed.head_target_id        = 404;
    confirmed.target_ids[0]         = 404;
    confirmed.target_ids[1]         = 0;
    confirmed.captured_target_count = 1;
    assert(!(action_queue_guard::IsStableResumePostcondition(expected, confirmed)));
  }
  std::cout << "Thin Queue Protection release-policy regressions passed\n";
}

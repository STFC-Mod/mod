#include "patches/parts/action_queue.h"
#include <cassert>
#include <iostream>
#include <map>

namespace
{
struct Handles {
  using Object = int;
  using Handle = unsigned;
  static inline unsigned                 serial{};
  static inline std::map<Handle, Object> live;
  static Handle                          New(Object object)
  {
    live[++serial] = object;
    return serial;
  }
  static Object Get(Handle handle)
  {
    auto it = live.find(handle);
    return it == live.end() ? 0 : it->second;
  }
  static void Free(Handle handle)
  { assert(live.erase(handle) == 1); }
  static void Collect(Object object)
  {
    for (auto& [_, value] : live)
      if (value == object)
        value = 0;
  }
};
using Requests = action_queue::Requests<Handles>;
using action_queue::CanAdvance;
using action_queue::QueueState;
using namespace std::chrono_literals;
const auto now = action_queue::Clock::time_point{} + 100s;
QueueState dispatch{true, 10, 100, 2.5f, 2, false, false};
QueueState remaining{true, 10, 200, 2.5f, 1, false, false};
} // namespace
int main()
{
  Requests requests;
  // Removed target: exactly one immediate handoff. Pending-success state is intentionally not an input.
  requests.Remember(1, dispatch, now);
  assert(CanAdvance(remaining, 10, 100, true, true, true));
  assert(requests.Consume(1, remaining, 100, now + 1s));
  assert(!requests.Consume(1, remaining, 100, now + 2s));
  assert(Handles::live.empty());

  // Native retry remains responsible while the target is still queued, even after reordering.
  auto reordered           = remaining;
  reordered.containsTarget = true;
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
  Handles::Collect(1);
  assert(!requests.Consume(1, remaining, 100, now));
  assert(Handles::live.empty());

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
  assert(Handles::live.empty());
  // Bounded capacity, independent fleets and session cleanup release all handles.
  for (int i = 1; i <= 9; ++i) {
    auto s  = dispatch;
    s.fleet = i;
    requests.Remember(i, s, now + std::chrono::seconds(i));
    assert(Handles::live.size() <= 8);
  }
  auto first  = remaining;
  first.fleet = 1;
  assert(!requests.Consume(1, first, 100, now + 10s));
  auto last  = remaining;
  last.fleet = 9;
  assert(requests.Consume(9, last, 100, now + 10s));
  requests.Clear();
  requests.Clear();
  assert(Handles::live.empty());
  assert(!requests.Consume(2, remaining, 100, now));
  std::cout << "Faster Queue Recovery policy and request-lifecycle tests passed\n";
}

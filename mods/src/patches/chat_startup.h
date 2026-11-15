#pragma once

namespace ChatStartup {
// Pure readiness gate: a false sample restarts the grace period. Completion is
// latched so closing chat manually never causes another automatic open.
struct Gate {
  bool completed = false;
  double ready_since = -1;

  bool Tick(bool ready, bool already_open, double now)
  {
    if (completed)
      return false;
    if (already_open) {
      completed = true;
      return false;
    }
    if (!ready) {
      ready_since = -1;
      return false;
    }
    if (ready_since < 0)
      ready_since = now;
    if (now - ready_since < 3.0)
      return false;
    completed = true;
    return true;
  }
};
} // namespace ChatStartup

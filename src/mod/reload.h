#pragma once

// Game-free readiness gate for in-process reloads. Every interruption restarts the settling period.
namespace reload
{
struct Gate {
  bool   suspended   = false;
  double ready_since = -1;

  void Begin()
  {
    suspended   = true;
    ready_since = -1;
  }

  // True only on the frame that resumes mod work.
  bool Tick(bool ready, double now)
  {
    if (!suspended)
      return false;
    if (!ready) {
      ready_since = -1;
      return false;
    }
    if (ready_since < 0)
      ready_since = now;
    if (now - ready_since < 3.0)
      return false;
    suspended = false;
    return true;
  }
};
} // namespace reload

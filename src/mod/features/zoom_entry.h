#pragma once

namespace ZoomEntry
{
// Per-controller entry detection, including a controller first observed inside a system.
struct Gate {
  bool in_system = false;
  bool pending   = false;

  void Observe(bool system)
  {
    if (system != in_system) {
      in_system = system;
      pending   = system;
    }
  }

  bool Consume(bool ready)
  {
    if (!in_system || !pending || !ready)
      return false;
    pending = false;
    return true;
  }
};
} // namespace ZoomEntry

#pragma once

namespace mod_settings
{
// A blur or destroyed editor does not release keys still held by its user.
// Require a focused, input-free frame before returning input to gameplay.
class InputRelease
{
public:
  void Hold()
  {
    active_     = true;
    idle_frame_ = false;
  }
  bool active() const
  { return active_; }
  bool Tick(bool focused, bool held)
  {
    if (!active_)
      return true;
    if (!focused || held)
      idle_frame_ = false;
    else if (idle_frame_)
      active_ = false;
    else
      idle_frame_ = true;
    return !active_;
  }

private:
  bool active_ = false, idle_frame_ = false;
};
} // namespace mod_settings

#pragma once
#include <array>
#include <optional>
#include <prime/KeyCode.h>
#include <string>

namespace mod_settings
{
// Preserve the modifier families understood by MapKey. Command and Windows
// have distinct Unity keycodes; if a Command press is also reported as Windows,
// prefer Command so the binding does not require the duplicate event later.
template <typename Held> std::string CaptureModifierPrefix(Held held)
{
  std::string token;
  if (held(KeyCode::LeftControl) || held(KeyCode::RightControl))
    token += "CTRL-";
  if (held(KeyCode::LeftAlt) || held(KeyCode::RightAlt))
    token += "ALT-";
  if (held(KeyCode::LeftShift) || held(KeyCode::RightShift))
    token += "SHIFT-";
  if (held(KeyCode::LeftCommand) || held(KeyCode::RightCommand))
    token += "CMD-";
  else if (held(KeyCode::LeftWindows) || held(KeyCode::RightWindows))
    token += "WIN-";
  return token;
}

// Pure input ownership state. The runtime samples supported physical keys only
// while active. A click or hold that opened capture cannot become its result.
class ShortcutCapture
{
public:
  using Keys = std::array<bool, static_cast<int>(KeyCode::Max)>;
  enum class Phase { Idle, ReleaseOpeningKeys, Listening, ReleaseCapturedKeys };
  void Begin()
  { phase_ = Phase::ReleaseOpeningKeys; }
  void Cancel()
  {
    if (phase_ != Phase::Idle)
      phase_ = Phase::ReleaseCapturedKeys;
  }
  bool active() const
  { return phase_ != Phase::Idle; }
  bool listening() const
  { return phase_ == Phase::Listening; }
  Phase phase() const
  { return phase_; }
  // Escape/focus loss cancel a pending recording and still drain the held keys.
  template <typename Modifier>
  std::optional<KeyCode> Tick(const Keys& held, const Keys& down, bool focused, Modifier modifier)
  {
    if (!active())
      return {};
    if (!focused || down[static_cast<int>(KeyCode::Escape)])
      Cancel();
    bool any = false;
    for (bool value : held)
      any |= value;
    if (phase_ == Phase::ReleaseOpeningKeys || phase_ == Phase::ReleaseCapturedKeys) {
      // Unity may report no held keys while unfocused. Require a focused sample
      // before releasing ownership, including when Alt-Tab cancelled recording.
      if (focused && !any)
        phase_ = phase_ == Phase::ReleaseOpeningKeys ? Phase::Listening : Phase::Idle;
      return {};
    }
    std::optional<KeyCode> candidate;
    for (std::size_t i = 1; i < down.size(); ++i) {
      const auto key = static_cast<KeyCode>(i);
      if (!down[i] || modifier(key))
        continue;
      if (candidate) {
        Cancel();
        return {};
      } // Ambiguous simultaneous primary keys.
      candidate = key;
    }
    if (candidate)
      phase_ = Phase::ReleaseCapturedKeys;
    return candidate;
  }

private:
  Phase phase_ = Phase::Idle;
};
} // namespace mod_settings

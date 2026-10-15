// Owns the per-frame keyboard snapshot and text-field focus.

#include <unity/input.h>
#include <unity/ui.h>

#include <algorithm>
#include <array>

namespace unity
{
namespace
{
  // On an idle frame anyKey is false and no key is asked about at all; otherwise each key is one internal call.
  struct Frame {
    int    number   = -1;
    bool   any_held = false;
    bool   any_down = false;
    int8_t focused  = 0; // 0 = not read yet, 1 = true, -1 = false
    int8_t modified = 0;
    std::array<std::array<int8_t, static_cast<size_t>(KeyCode::Max)>, 2> keys{};

    bool Read(KeyCode key, bool down)
    {
      if (!(down ? any_down : any_held))
        return false;
      auto& value = keys[down][static_cast<size_t>(key)];
      if (value == 0)
        value = (down ? Input::GetKeyDown(key) : Input::GetKey(key)) ? 1 : -1;
      return value == 1;
    }
  } frame;

  bool InputFocused(EventSystem* event_system)
  {
    auto* selected = event_system ? event_system->currentSelectedGameObject() : nullptr;
    auto* field    = selected ? selected->GetComponent<TMP_InputField>() : nullptr;
    return field && field->isFocused();
  }
} // namespace

namespace Key
{
  void Poll()
  {
    // Without the frame count nothing is ever read: no key counts as pressed.
    const int number = Time::frameCount();
    if (number < 0 || number == frame.number)
      return;
    frame = {.number = number, .any_held = Input::anyKey(), .any_down = Input::anyKeyDown()};
  }

  bool Pressed(KeyCode key)
  { return frame.Read(key, false); }

  bool Down(KeyCode key)
  { return frame.Read(key, true); }

  bool IsModified()
  {
    if (frame.modified == 0) {
      constexpr std::array kModifiers{KeyCode::LeftAlt,      KeyCode::LeftControl,  KeyCode::LeftShift,
                                      KeyCode::RightAlt,     KeyCode::RightControl, KeyCode::RightShift,
                                      KeyCode::AltGr,        KeyCode::LeftCommand,  KeyCode::LeftWindows,
                                      KeyCode::RightCommand, KeyCode::RightWindows};
      frame.modified = frame.any_held && std::ranges::any_of(kModifiers, Pressed) ? 1 : -1;
    }
    return frame.modified == 1;
  }

  bool IsInputFocused()
  {
    if (frame.focused == 0)
      frame.focused = InputFocused(EventSystem::current()) ? 1 : -1;
    return frame.focused == 1;
  }

  void ClearInputFocus()
  {
    auto* event_system = EventSystem::current();
    if (InputFocused(event_system))
      event_system->SetSelectedGameObject(nullptr);
    // Selection callbacks can focus another field. Re-read the actual focus on the next query this frame.
    frame.focused = 0;
  }
} // namespace Key
} // namespace unity

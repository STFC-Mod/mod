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
  } frame;

  TMP_InputField* FocusedInputField()
  {
    auto* event_system = EventSystem::current();
    auto* selected     = event_system ? event_system->currentSelectedGameObject() : nullptr;
    auto* field        = selected ? selected->GetComponent<TMP_InputField>() : nullptr;
    return field && field->isFocused() ? field : nullptr;
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
  { return frame.any_held && Input::GetKey(key); }

  bool Down(KeyCode key)
  { return frame.any_down && Input::GetKeyDown(key); }

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
      frame.focused = FocusedInputField() ? 1 : -1;
    return frame.focused == 1;
  }

  void ClearInputFocus()
  {
    if (FocusedInputField())
      EventSystem::current()->SetSelectedGameObject(nullptr);
  }
} // namespace Key
} // namespace unity

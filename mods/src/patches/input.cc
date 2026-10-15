#include "patches/input.h"

#include <prime/unity.h>

#include <il2cpp/il2cpp.h>

#include <algorithm>
#include <array>

namespace
{
// Unity's internal calls. A missing one (renamed by a Unity upgrade) reads as nothing held.
struct Input {
  il2cpp::ICall<int()>         frame_count{"UnityEngine.Time::get_frameCount()"};
  il2cpp::ICall<bool()>        any_key{"UnityEngine.Input::get_anyKey()"};
  il2cpp::ICall<bool()>        any_key_down{"UnityEngine.Input::get_anyKeyDown()"};
  il2cpp::ICall<bool(KeyCode)> key{"UnityEngine.Input::GetKeyInt(UnityEngine.KeyCode)"};
  il2cpp::ICall<bool(KeyCode)> key_down{"UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)"};
};

const Input& Unity()
{
  static const Input input;
  return input;
}

// This frame's snapshot. On an idle frame anyKey is false and no key is asked about at all; otherwise each key is
// one internal call, which Unity answers from its own per-frame state.
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
  const auto& unity = Unity();
  if (!unity.frame_count)
    return;
  const int number = unity.frame_count();
  if (number == frame.number)
    return;
  // Without the any-key calls every key is asked about, as if something were always held.
  frame = {.number   = number,
           .any_held = !unity.any_key || unity.any_key(),
           .any_down = !unity.any_key_down || unity.any_key_down()};
}

bool Pressed(KeyCode key)
{ return frame.any_held && Unity().key(key); }

bool Down(KeyCode key)
{ return frame.any_down && Unity().key_down(key); }

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

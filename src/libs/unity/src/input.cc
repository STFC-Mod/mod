// Owns the per-frame keyboard snapshot and text-field focus.

#include <unity/input.h>
#include <unity/ui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace unity
{
namespace
{
  // On an idle frame anyKey is false and no key is asked about at all; otherwise each key is one internal call.
  // The optional members cache a read for the rest of the frame.
  struct Frame {
    int32_t             number   = -1;
    bool                any_held = false;
    bool                any_down = false;
    std::optional<bool> focused;
    std::optional<bool> modified;
    // Indexed [down][key].
    std::array<std::array<std::optional<bool>, std::to_underlying(KeyCode::Max)>, 2> keys{};

    bool Read(KeyCode key, bool down)
    {
      if (!(down ? any_down : any_held))
        return false;
      auto& value = keys[down][std::to_underlying(key)];
      if (!value)
        value = down ? Input::GetKeyDown(key) : Input::GetKey(key);
      return *value;
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
    const int32_t number = Time::frameCount();
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
    constexpr std::array kModifiers{KeyCode::LeftAlt,      KeyCode::LeftControl,  KeyCode::LeftShift,
                                    KeyCode::RightAlt,     KeyCode::RightControl, KeyCode::RightShift,
                                    KeyCode::AltGr,        KeyCode::LeftCommand,  KeyCode::LeftWindows,
                                    KeyCode::RightCommand, KeyCode::RightWindows};
    if (!frame.modified)
      frame.modified = frame.any_held && std::ranges::any_of(kModifiers, Pressed);
    return *frame.modified;
  }

  bool IsInputFocused()
  {
    if (!frame.focused)
      frame.focused = InputFocused(EventSystem::current());
    return *frame.focused;
  }

  void ClearInputFocus()
  {
    auto* event_system = EventSystem::current();
    if (InputFocused(event_system))
      event_system->SetSelectedGameObject(nullptr);
    // Selection callbacks can focus another field. Re-read the actual focus on the next query this frame.
    frame.focused.reset();
  }
} // namespace Key
} // namespace unity

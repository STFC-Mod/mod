#include "patches/input.h"

#include <prime/unity.h>

#include <il2cpp/il2cpp.h>

#include <algorithm>
#include <array>

namespace
{
constexpr auto kKeyCount = static_cast<size_t>(KeyCode::Max);

constexpr std::array kDirectionalKeys{KeyCode::LeftArrow, KeyCode::RightArrow, KeyCode::UpArrow, KeyCode::DownArrow};

// 0 = not read yet this frame, 1 = true, -1 = false.
int                           cache_frame          = -1;
int                           cached_input_focused = 0;
int                           cached_modified      = 0;
std::array<int8_t, kKeyCount> cached_pressed{};
std::array<int8_t, kKeyCount> cached_down{};
std::array<bool, kKeyCount>   claimed_directions{};

int CurrentFrame()
{
  static auto get_frameCount = il2cpp::ResolveICall<int()>("UnityEngine.Time::get_frameCount()");
  return get_frameCount();
}

TMP_InputField* FocusedInputField()
{
  auto* event_system = EventSystem::current();
  auto* selected     = event_system ? event_system->currentSelectedGameObject() : nullptr;
  auto* field        = selected ? selected->GetComponent<TMP_InputField>() : nullptr;
  return field && field->isFocused() ? field : nullptr;
}

void EnsureCurrentFrame()
{
  const auto frame = CurrentFrame();
  if (frame == cache_frame)
    return;

  cache_frame          = frame;
  cached_input_focused = 0;
  cached_modified      = 0;
  cached_pressed.fill(0);
  cached_down.fill(0);

  // Prune released claims here as well as in IsDirectionalInputClaimed, so a claim cannot outlive a scene
  // without NavigationPan.
  for (const auto key : kDirectionalKeys) {
    auto& claimed = claimed_directions[static_cast<size_t>(key)];
    if (claimed && !Key::Pressed(key))
      claimed = false;
  }
}
} // namespace

namespace Key
{
bool Pressed(KeyCode key)
{
  EnsureCurrentFrame();
  static auto GetKeyInt = il2cpp::ResolveICall<bool(KeyCode)>("UnityEngine.Input::GetKeyInt(UnityEngine.KeyCode)");

  auto& cached = cached_pressed[static_cast<size_t>(key)];
  if (cached == 0)
    cached = GetKeyInt(key) ? 1 : -1;
  return cached == 1;
}

bool Down(KeyCode key)
{
  EnsureCurrentFrame();
  static auto GetKeyDownInt =
      il2cpp::ResolveICall<bool(KeyCode)>("UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)");

  auto& cached = cached_down[static_cast<size_t>(key)];
  if (cached == 0)
    cached = GetKeyDownInt(key) ? 1 : -1;
  return cached == 1;
}

bool IsModified()
{
  EnsureCurrentFrame();
  if (cached_modified == 0) {
    constexpr std::array kModifiers{KeyCode::LeftAlt,      KeyCode::LeftControl,  KeyCode::LeftShift,
                                    KeyCode::RightAlt,     KeyCode::RightControl, KeyCode::RightShift,
                                    KeyCode::AltGr,        KeyCode::LeftCommand,  KeyCode::LeftWindows,
                                    KeyCode::RightCommand, KeyCode::RightWindows};
    cached_modified = std::ranges::any_of(kModifiers, Pressed) ? 1 : -1;
  }
  return cached_modified == 1;
}

bool HasShift()
{ return Pressed(KeyCode::LeftShift) || Pressed(KeyCode::RightShift); }

bool HasAlt()
{ return Pressed(KeyCode::LeftAlt) || Pressed(KeyCode::RightAlt); }

bool HasCtrl()
{ return Pressed(KeyCode::LeftControl) || Pressed(KeyCode::RightControl); }

bool IsInputFocused()
{
  EnsureCurrentFrame();
  if (cached_input_focused == 0)
    cached_input_focused = FocusedInputField() ? 1 : -1;
  return cached_input_focused == 1;
}

void ClearInputFocus()
{
  if (FocusedInputField())
    EventSystem::current()->SetSelectedGameObject(nullptr);
}

void ClaimDirectionalInput(KeyCode key)
{
  if (std::ranges::find(kDirectionalKeys, key) != kDirectionalKeys.end())
    claimed_directions[static_cast<size_t>(key)] = true;
}

bool IsDirectionalInputClaimed()
{
  EnsureCurrentFrame();
  bool claimed = false;
  for (const auto key : kDirectionalKeys) {
    auto& direction = claimed_directions[static_cast<size_t>(key)];
    if (direction && Pressed(key))
      claimed = true;
    else
      direction = false;
  }
  return claimed;
}
} // namespace Key

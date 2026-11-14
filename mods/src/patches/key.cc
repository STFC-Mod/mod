#include "key.h"
#include "prime/EventSystem.h"
#include "str_utils.h"
#include <prime/TMP_InputField.h>

#include <string>
#include <string_view>
#include <unordered_map>

int Key::cacheInputFocused  = 0;
int Key::cacheInputModified = 0;
int Key::cacheFrame         = -1;

std::array<int, (int)KeyCode::Max>  Key::cacheKeyPressed         = {};
std::array<int, (int)KeyCode::Max>  Key::cacheKeyDown            = {};
std::array<bool, (int)KeyCode::Max> Key::claimedDirectionalInput = {};

namespace
{
constexpr std::array directionalKeys = {
    KeyCode::LeftArrow,
    KeyCode::RightArrow,
    KeyCode::UpArrow,
    KeyCode::DownArrow,
};

int CurrentInputFrame()
{
  static auto GetFrameCount = il2cpp::ResolveICall<int()>("UnityEngine.Time::get_frameCount()");
  return GetFrameCount();
}
}

const std::unordered_map<std::string, KeyCode> Key::mappedKeys = {
    {"LALT", KeyCode::LeftAlt},
    {"LAPPLE", KeyCode::LeftApple},
    {"LCOM", KeyCode::LeftCommand},
    {"LCTRL", KeyCode::LeftControl},
    {"ALTGR", KeyCode::AltGr},
    {"END", KeyCode::End},
    {"HOME", KeyCode::Home},
    {"PGDOWN", KeyCode::PageDown},
    {"PGUP", KeyCode::PageUp},
    {"DOWN", KeyCode::DownArrow},
    {"LEFT", KeyCode::LeftArrow},
    {"RIGHT", KeyCode::RightArrow},
    {"UP", KeyCode::UpArrow},
    {"BACKSPACE", KeyCode::Backspace},
    {"BREAK", KeyCode::Break},
    {"CAPS", KeyCode::CapsLock},
    {"CLEAR", KeyCode::Clear},
    {"DELETE", KeyCode::Delete},
    {"ESCAPE", KeyCode::Escape},
    {"HELP", KeyCode::Help},
    {"INSERT", KeyCode::Insert},
    {"LSHIFT", KeyCode::LeftShift},
    {"LWIN", KeyCode::LeftWindows},
    {"MENU", KeyCode::Menu},
    {"PAUSE", KeyCode::Pause},
    {"PRINT", KeyCode::Print},
    {"RALT", KeyCode::RightAlt},
    {"RAPPLE", KeyCode::RightApple},
    {"RCOM", KeyCode::RightCommand},
    {"RCTRL", KeyCode::RightControl},
    {"RETURN", KeyCode::Return},
    {"RSHIFT", KeyCode::RightShift},
    {"RWIN", KeyCode::RightWindows},
    {"SCROLL", KeyCode::ScrollLock},
    {"SYSREQ", KeyCode::SysReq},
    {"TAB", KeyCode::Tab},
    {"MOUSE0", KeyCode::Mouse0},
    {"MOUSE1", KeyCode::Mouse1},
    {"MOUSE2", KeyCode::Mouse2},
    {"MOUSE3", KeyCode::Mouse3},
    {"MOUSE4", KeyCode::Mouse4},
    {"MOUSE5", KeyCode::Mouse5},
    {"MOUSE6", KeyCode::Mouse6},
    {"SPACE", KeyCode::Space},
    {"MINUS", KeyCode::Minus},
    {"_", KeyCode::Underscore},
    {",", KeyCode::Comma},
    {";", KeyCode::Semicolon},
    {":", KeyCode::Colon},
    {"!", KeyCode::Exclaim},
    {"?", KeyCode::Question},
    {".", KeyCode::Period},
    {"'", KeyCode::Quote},
    {"(", KeyCode::LeftParen},
    {")", KeyCode::RightParen},
    {"[", KeyCode::LeftBracket},
    {"]", KeyCode::RightBracket},
    {"{", KeyCode::LeftCurlyBracket},
    {"}", KeyCode::RightCurlyBracket},
    {"@", KeyCode::At},
    {"*", KeyCode::Asterisk},
    {"/", KeyCode::Slash},
    {"\"", KeyCode::DoubleQuote},
    {"\\", KeyCode::Backslash},
    {"&", KeyCode::Ampersand},
    {"#", KeyCode::Hash},
    {"%", KeyCode::Percent},
    {"`", KeyCode::BackQuote},
    {"^", KeyCode::Caret},
    {"+", KeyCode::Plus},
    {"<", KeyCode::Less},
    {"=", KeyCode::Equals},
    {">", KeyCode::Greater},
    {"|", KeyCode::Pipe},
    {"~", KeyCode::Tilde},
    {"$", KeyCode::Dollar},
    {"0", KeyCode::Alpha0},
    {"1", KeyCode::Alpha1},
    {"2", KeyCode::Alpha2},
    {"3", KeyCode::Alpha3},
    {"4", KeyCode::Alpha4},
    {"5", KeyCode::Alpha5},
    {"6", KeyCode::Alpha6},
    {"7", KeyCode::Alpha7},
    {"8", KeyCode::Alpha8},
    {"9", KeyCode::Alpha9},
    {"A", KeyCode::A},
    {"B", KeyCode::B},
    {"C", KeyCode::C},
    {"D", KeyCode::D},
    {"E", KeyCode::E},
    {"F", KeyCode::F},
    {"F1", KeyCode::F1},
    {"F10", KeyCode::F10},
    {"F11", KeyCode::F11},
    {"F12", KeyCode::F12},
    {"F13", KeyCode::F13},
    {"F14", KeyCode::F14},
    {"F15", KeyCode::F15},
    {"F2", KeyCode::F2},
    {"F3", KeyCode::F3},
    {"F4", KeyCode::F4},
    {"F5", KeyCode::F5},
    {"F6", KeyCode::F6},
    {"F7", KeyCode::F7},
    {"F8", KeyCode::F8},
    {"F9", KeyCode::F9},
    {"G", KeyCode::G},
    {"H", KeyCode::H},
    {"I", KeyCode::I},
    {"J", KeyCode::J},
    {"K", KeyCode::K},
    {"L", KeyCode::L},
    {"M", KeyCode::M},
    {"N", KeyCode::N},
    {"O", KeyCode::O},
    {"P", KeyCode::P},
    {"Q", KeyCode::Q},
    {"R", KeyCode::R},
    {"S", KeyCode::S},
    {"T", KeyCode::T},
    {"U", KeyCode::U},
    {"V", KeyCode::V},
    {"W", KeyCode::W},
    {"X", KeyCode::X},
    {"Y", KeyCode::Y},
    {"Z", KeyCode::Z},
    {"NUMLOCK", KeyCode::Numlock},
    {"KEY0", KeyCode::Keypad0},
    {"KEY1", KeyCode::Keypad1},
    {"KEY2", KeyCode::Keypad2},
    {"KEY3", KeyCode::Keypad3},
    {"KEY4", KeyCode::Keypad4},
    {"KEY5", KeyCode::Keypad5},
    {"KEY6", KeyCode::Keypad6},
    {"KEY7", KeyCode::Keypad7},
    {"KEY8", KeyCode::Keypad8},
    {"KEY9", KeyCode::Keypad9},
    {"KEYDIVIDE", KeyCode::KeypadDivide},
    {"KEYENTER", KeyCode::KeypadEnter},
    {"KEYEQUAL", KeyCode::KeypadEquals},
    {"KEYMINUS", KeyCode::KeypadMinus},
    {"KEYMULTI", KeyCode::KeypadMultiply},
    {"KEYPERIOD", KeyCode::KeypadPeriod},
    {"KEYPLUS", KeyCode::KeypadPlus},
};

KeyCode Key::Parse(std::string_view key)
{
  auto wantedKey = AsciiStrToUpper(key);
  for (const auto& [value, keycode] : mappedKeys) {
    if (wantedKey == value) {
      return (KeyCode)keycode;
    }
  }

  return KeyCode::None;
}

bool Key::IsModifier(KeyCode key)
{
  switch (key) {
    case KeyCode::LeftAlt:
    case KeyCode::LeftCommand:
    case KeyCode::LeftControl:
    case KeyCode::AltGr:
    case KeyCode::LeftShift:
    case KeyCode::LeftWindows:
    case KeyCode::RightAlt:
    case KeyCode::RightCommand:
    case KeyCode::RightControl:
    case KeyCode::RightShift:
    case KeyCode::RightWindows:
      return true;
    default:
      return false;
  }
}

void Key::ClaimDirectionalInput(KeyCode key)
{
  switch (key) {
    case KeyCode::LeftArrow:
    case KeyCode::RightArrow:
    case KeyCode::UpArrow:
    case KeyCode::DownArrow:
      claimedDirectionalInput[(int)key] = true;
      break;
    default:
      break;
  }
}

bool Key::IsDirectionalInputClaimed()
{
  EnsureCurrentFrame();

  auto claimed = false;
  for (const auto key : directionalKeys) {
    auto& directionClaimed = claimedDirectionalInput[(int)key];
    if (!directionClaimed) {
      continue;
    }

    if (Pressed(key)) {
      claimed = true;
    } else {
      directionClaimed = false;
    }
  }

  return claimed;
}

bool Key::IsModified()
{
  EnsureCurrentFrame();

  if (cacheInputModified == 0) {
    cacheInputModified = -1;
    if (Key::Pressed(KeyCode::LeftAlt) || Key::Pressed(KeyCode::LeftControl) || Key::Pressed(KeyCode::LeftShift)
        || Key::Pressed(KeyCode::RightAlt) || Key::Pressed(KeyCode::RightControl) || Key::Pressed(KeyCode::RightShift)
        || Key::Pressed(KeyCode::AltGr) || Key::Pressed(KeyCode::LeftCommand) || Key::Pressed(KeyCode::LeftWindows)
        || Key::Pressed(KeyCode::RightCommand) || Key::Pressed(KeyCode::RightWindows)) {
      cacheInputModified = 1;
    }
  }

  return cacheInputModified == 1;
}

void Key::ResetCache()
{
  Key::cacheFrame          = CurrentInputFrame();
  Key::cacheInputFocused  = 0;
  Key::cacheInputModified = 0;
  for (int i = 0; i < (int)KeyCode::Max; i++) {
    Key::cacheKeyDown[i]    = 0;
    Key::cacheKeyPressed[i] = 0;
  }

  // Direction claims span frames while their key is held so one-shot chords cannot leak into native movement.
  // Prune them here as well as at the consumer so claims cannot survive a scene without NavigationPan.
  for (const auto key : directionalKeys) {
    auto& directionClaimed = claimedDirectionalInput[(int)key];
    if (directionClaimed && !Pressed(key)) {
      directionClaimed = false;
    }
  }
}

void Key::EnsureCurrentFrame()
{
  if (cacheFrame != CurrentInputFrame()) {
    ResetCache();
  }
}

bool Key::Down(KeyCode key)
{
  EnsureCurrentFrame();

  static auto GetKeyDownInt =
      il2cpp::ResolveICall<bool(KeyCode)>("UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)");

  if (cacheKeyDown[(int)key] == 0) {
    cacheKeyDown[(int)key] = GetKeyDownInt(key) ? 1 : -1;
  }

  return cacheKeyDown[(int)key] == 1;
}

bool Key::Pressed(KeyCode key)
{
  EnsureCurrentFrame();

  static auto GetKeyInt = il2cpp::ResolveICall<bool(KeyCode)>("UnityEngine.Input::GetKeyInt(UnityEngine.KeyCode)");

  if (cacheKeyPressed[(int)key] == 0) {
    cacheKeyPressed[(int)key] = GetKeyInt(key) ? 1 : -1;
  }

  return cacheKeyPressed[(int)key] == 1;
}

bool Key::IsInputFocused()
{
  EnsureCurrentFrame();

  if (cacheInputFocused == 0) {
    cacheInputFocused = -1;

    auto eventSystem = EventSystem::current();
    if (eventSystem) {
      auto n = eventSystem->currentSelectedGameObject();
      if (n) {
        auto n2 = n->GetComponent<TMP_InputField>();
        if (n2 && n2->isFocused()) {
          cacheInputFocused = 1;
        }
      }
    }
  }

  return cacheInputFocused == 1;
}

bool Key::HasShift()
{
  return Key::Pressed(KeyCode::LeftShift) || Key::Pressed(KeyCode::RightShift);
}

bool Key::HasAlt()
{
  return Key::Pressed(KeyCode::LeftAlt) || Key::Pressed(KeyCode::RightAlt);
}

bool Key::HasCtrl()
{
  return Key::Pressed(KeyCode::LeftControl) || Key::Pressed(KeyCode::RightControl);
}

void Key::ClearInputFocus()
{
  if (auto eventSystem = EventSystem::current(); eventSystem) {
    if (auto n = eventSystem->currentSelectedGameObject(); n) {
      auto n2 = n->GetComponent<TMP_InputField>();
      if (n2 && n2->isFocused()) {
        eventSystem->SetSelectedGameObject(nullptr);
      }
    }
  }
}

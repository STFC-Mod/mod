#pragma once

#include <prime/KeyCode.h>

#include <array>
#include <string>
#include <unordered_map>

class Key
{
private:
  static const std::unordered_map<std::string, KeyCode> mappedKeys;

  static int cacheInputFocused;
  static int cacheInputModified;
  static int cacheFrame;

  static std::array<int, (int)KeyCode::Max>  cacheKeyPressed;
  static std::array<int, (int)KeyCode::Max>  cacheKeyDown;
  static std::array<bool, (int)KeyCode::Max> claimedDirectionalInput;

  static void EnsureCurrentFrame();

public:
  static void    ClearInputFocus();
  static void    ResetCache();
  static KeyCode Parse(std::string_view key);

  static bool Pressed(KeyCode key);
  static bool Down(KeyCode key);
  // Editors alone sample raw input; every ordinary consumer respects ownership.
  static bool        RawPressed(KeyCode key);
  static bool        AnyRawPressed();
  static bool        RawDown(KeyCode key);
  static std::string Token(KeyCode key);
  inline static bool shortcutCaptureActive = false, shortcutPopupActive = false;
  inline static bool settingsSearchActive = false, textEntryActive = false;
  static bool        SettingsOwnInput()
  { return shortcutCaptureActive || shortcutPopupActive || settingsSearchActive || textEntryActive; }

  static void ClaimDirectionalInput(KeyCode key);
  static bool IsDirectionalInputClaimed();

  static bool IsModifier(KeyCode key);

  static bool IsModified();
  static bool IsInputFocused();
  static bool HasShift();
  static bool HasAlt();
  static bool HasCtrl();
};

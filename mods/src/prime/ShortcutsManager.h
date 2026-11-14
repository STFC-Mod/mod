#pragma once

#include "MonoSingleton.h"

struct ShortcutsManager : MonoSingleton<ShortcutsManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GameInput", "ShortcutsManager">;

  bool ShowKeybindings()
  { return Get<"ShowKeybindings", bool>(); }
  void SetShowKeybindings(bool value)
  { Set<"ShowKeybindings">(value); }
  // Private static gate the native shortcut actions check (tutorial, modals, text input).
  static bool CanUseShortcuts()
  { return Class::Get<"CanUseShortcuts", bool>(); }
};

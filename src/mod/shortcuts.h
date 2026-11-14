#pragma once

// Configurable shortcuts: the actions they trigger, parsing [shortcuts] from the settings file, and
// matching the parsed key chords against this frame's input.

#include <toml++/impl/forward_declarations.hpp>
#include <unity/input.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What a shortcut does. Each [shortcuts] setting binds key chords to one action.
enum class Action : uint8_t {
#define SHORTCUT(action, ...) action,
#include "shortcuts.def"
#undef SHORTCUT
  kCount
};

// One key with its required modifiers, such as CTRL-SHIFT-F1.
struct KeyChord {
  unity::KeyCode key = unity::KeyCode::None;
  struct Modifier {
    std::string_view name;
    // SHIFT accepts either side; LSHIFT stores LeftShift twice.
    std::array<unity::KeyCode, 2> keys;
    std::string_view              hint;
  };
  // Points into the immutable modifier catalog; spelling, matching and badge text share one definition.
  std::vector<const Modifier*> modifiers;

  // Parses one chord. Unknown tokens are ignored; a chord without a non-modifier key has key == None. With two
  // keys, the last one counts.
  static KeyChord Parse(std::string_view text);

  // "CTRL-SHIFT-F1"
  std::string Text() const;
  // Compact form for the game's keybind badges: "^+F1".
  std::string Hint() const;

private:
  std::string key_name_;
};

namespace Shortcut
{
using Bindings = std::array<std::vector<KeyChord>, static_cast<size_t>(Action::kCount)>;

// Parses [shortcuts] without changing live input. Records effective bindings and their sources in `parsed`.
Bindings Parse(const toml::table& config, toml::table& parsed, bool extended, bool experimental);
// Activates a complete set of bindings and releases keys owned by the previous set. Game thread only.
void Set(Bindings bindings) noexcept;

bool HasBinding(Action action);
// Any binding of `action` is held with its modifiers. A binding without modifiers needs none held; one with
// modifiers still fires when extra modifiers are held (CTRL-F1 fires under CTRL+SHIFT).
bool IsPressed(Action action);
// Any binding of `action` went down this frame with its modifiers held.
bool IsDown(Action action);
// A binding with modifiers matched on `key`, which is still held. Plain bindings on it wait for the release.
bool Owns(unity::KeyCode key);

// "CTRL-G | F8"
std::string Describe(Action action);
// The badge text of the first binding, or empty when unbound.
std::string Hint(Action action);
} // namespace Shortcut

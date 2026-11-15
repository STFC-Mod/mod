#include "mapkey.h"
#include "gamefunctions.h"
#include "key.h"
#include "modifierkey.h"
#include "str_utils.h"
#include <prime/KeyCode.h>

#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
struct CompactShortcutTokenMapping {
  std::string_view token;
  std::string_view compact;
  bool             primary_key_only = false;
};

// Use AutoHotkey's established ASCII modifier notation so the native game's limited font can render every badge.
constexpr auto kCompactShortcutTokenMappings = std::to_array<CompactShortcutTokenMapping>({
    {"SHIFT", "+"},
    {"CTRL", "^"},
    {"ALT", "!"},
    {"ALTGR", "!"},
    {"APPLE", "#"},
    {"CMD", "#"},
    {"WIN", "#"},
    {"LSHIFT", "<+"},
    {"RSHIFT", ">+"},
    {"LCTRL", "<^"},
    {"RCTRL", ">^"},
    {"LALT", "<!"},
    {"RALT", ">!"},
    {"LAPPLE", "<#"},
    {"LCOM", "<#"},
    {"LWIN", "<#"},
    {"RAPPLE", ">#"},
    {"RCOM", ">#"},
    {"RWIN", ">#"},
    {"+", "PLS", true},
    {"^", "CAR", true},
    {"!", "EXC", true},
    {"#", "HSH", true},
    {"SPACE", "SPC"},
    {"MOUSE0", "M0"},
    {"MOUSE1", "M1"},
    {"MOUSE2", "M2"},
    {"MOUSE3", "M3"},
    {"MOUSE4", "M4"},
    {"MOUSE5", "M5"},
    {"MOUSE6", "M6"},
    {"ENTER", "ENT"},
    {"RETURN", "ENT"},
    {"ESCAPE", "ESC"},
    {"TAB", "TAB"},
    {"BACKSPACE", "BS"},
    {"DELETE", "DEL"},
    {"MINUS", "-"},
    {"EQUAL", "="},
    {"LEFT", "LT"},
    {"RIGHT", "RT"},
    {"UP", "UP"},
    {"DOWN", "DN"},
    {"PGUP", "PU"},
    {"PGDOWN", "PD"},
    {"HOME", "HM"},
    {"END", "END"},
});

consteval bool CompactShortcutTokensAreUnique()
{
  for (size_t index = 0; index < kCompactShortcutTokenMappings.size(); ++index) {
    for (size_t other = index + 1; other < kCompactShortcutTokenMappings.size(); ++other) {
      if (kCompactShortcutTokenMappings[index].token == kCompactShortcutTokenMappings[other].token) {
        return false;
      }
    }
  }
  return true;
}

static_assert(CompactShortcutTokensAreUnique());

constexpr std::string_view CompactShortcutToken(std::string_view token, bool is_primary_key)
{
  for (const auto& mapping : kCompactShortcutTokenMappings) {
    if (mapping.token == token && (!mapping.primary_key_only || is_primary_key)) {
      return mapping.compact;
    }
  }
  return token;
}

static_assert(CompactShortcutToken("CTRL", false) == "^");
static_assert(CompactShortcutToken("ALTGR", false) == "!");
static_assert(CompactShortcutToken("+", true) == "PLS");
static_assert(CompactShortcutToken("+", false) == "+");
static_assert(CompactShortcutToken("F7", true) == "F7");

std::string CompactShortcutForHint(const std::vector<std::string>& shortcuts)
{
  std::string compact;
  for (size_t index = 0; index < shortcuts.size(); ++index) {
    compact.append(CompactShortcutToken(shortcuts[index], index + 1 == shortcuts.size()));
  }
  return compact;
}
} // namespace

MapKey::MapKey()
{
  this->Key          = KeyCode::None;
  this->hasModifiers = false;
}

MapKey MapKey::Parse(std::string_view key)
{
  auto strippedKey = StripTrailingAsciiWhitespace(key);
  auto lowerKey    = AsciiStrToUpper(strippedKey);
  auto wantedKeys  = StrSplit(lowerKey, '-');

  MapKey mapKey;
  for (std::string_view wantedKey : wantedKeys) {
    auto modifier = ModifierKey::Parse(wantedKey);
    if (modifier.HasModifiers()) {
      mapKey.hasModifiers = true;
      mapKey.Modifiers.emplace_back(std::move(modifier));
      mapKey.Shortcuts.emplace_back(wantedKey);
    } else {
      auto parsedKey = Key::Parse(wantedKey);

      if (Key::IsModifier(parsedKey)) {
        continue;
      }

      if (parsedKey != KeyCode::None) {
        mapKey.Key = parsedKey;
        mapKey.Shortcuts.emplace_back(wantedKey);
      }
    }

#ifndef NDEBUG
    if (mapKey.Key == KeyCode::X) {
      std::cout << "\n\n----------\nX key:\n" << mapKey.GetParsedValues() << "\n----------\n\n";
    }
#endif
  }

  return mapKey;
}

std::string MapKey::GetShortcuts(GameFunction gameFunction)
{
  const auto &mapKeys = MapKey::mappedKeys[gameFunction];

  bool appendPipe = false;

  std::string shortcuts = "";
  for (const MapKey &mapKey : mapKeys) {
    if (appendPipe) {
      shortcuts.append(" | ");
    }
    shortcuts.append(mapKey.GetParsedValues());
    appendPipe = true;
  }

  return shortcuts;
}

std::string MapKey::GetShortcutHint(GameFunction gameFunction)
{
  const auto& mapKeys = MapKey::mappedKeys[gameFunction];
  return mapKeys.empty() ? "" : mapKeys.front().shortcutHint;
}

void MapKey::CacheShortcutHints()
{
  // Native badges display only the first binding for each action.
  for (auto& mapKeys : mappedKeys) {
    if (!mapKeys.empty()) {
      mapKeys.front().shortcutHint = CompactShortcutForHint(mapKeys.front().Shortcuts);
    }
  }
}

bool MapKey::HasBinding(GameFunction gameFunction)
{
  for (const auto& mapKey : mappedKeys[gameFunction]) {
    if (mapKey.Key != KeyCode::None) {
      return true;
    }
  }
  return false;
}

void MapKey::AddMappedKey(GameFunction gameFunction, MapKey mappedKey)
{
  MapKey::mappedKeys[gameFunction].emplace_back(std::move(mappedKey));
}

// Movement reads the direction itself; claiming it would suppress its own pan
// on the next frame. Other modified actions retain ownership until key release.
static bool IsMovementAction(GameFunction action)
{
  return action == GameFunction::MoveLeft || action == GameFunction::MoveRight
         || action == GameFunction::MoveUp || action == GameFunction::MoveDown;
}

bool MapKey::IsPressed(GameFunction gameFunction)
{
  const auto &mapKeys = MapKey::mappedKeys[(int)gameFunction];
  for (const MapKey &mapKey : mapKeys) {
    if (mapKey.Key != KeyCode::None) {
      if (Key::Pressed(mapKey.Key)) {
        if (MapKey::HasCorrectModifiers(mapKey)) {
          if (mapKey.hasModifiers && !IsMovementAction(gameFunction)) {
            Key::ClaimDirectionalInput(mapKey.Key);
          }
          return true;
        }
      }
    }
  }

  return false;
}

bool MapKey::IsDown(GameFunction gameFunction)
{
  const auto &mapKeys = MapKey::mappedKeys[(int)gameFunction];
  for (const MapKey &mapKey : mapKeys) {
    if (mapKey.Key != KeyCode::None) {
      if (Key::Down(mapKey.Key)) {
        if (MapKey::HasCorrectModifiers(mapKey)) {
          if (mapKey.hasModifiers && !IsMovementAction(gameFunction)) {
            Key::ClaimDirectionalInput(mapKey.Key);
          }
          return true;
        }
      }
    }
  }

  return false;
}

bool MapKey::HasCorrectModifiers(const MapKey& mapKey)
{
  auto        result  = false;
  std::string section = "non set";
  if (!mapKey.hasModifiers) {
    section = "no modifiers";
    result  = !Key::IsModified();
  } else {
    result = true;
    for (const ModifierKey& modifier : mapKey.Modifiers) {
      if (!modifier.IsPressed()) {
        section = modifier.GetParsedValues();
        result  = false;
        break;
      }
    }
  }

#ifndef NDEBUG
  if (mapKey.Key == KeyCode::Backslash && Key::Pressed(KeyCode::Backslash)) {
    std::cout << "HasCorrectModifiers(" << mapKey.GetParsedValues() << "): [" << section << "] " << result << "\n";
  }
#endif

  return result;
}

std::string MapKey::GetParsedValues() const
{
  std::string output = "";
  for (const std::string_view key : this->Shortcuts) {
    if (output.length()) {
      output.append("-");
    }
    output.append(key);
  }

  return output;
}

std::array<std::vector<MapKey>, (int)GameFunction::Max> MapKey::mappedKeys = {};

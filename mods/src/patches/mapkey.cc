#include "mapkey.h"
#include "gamefunctions.h"
#include "key.h"
#include "modifierkey.h"
#include "str_utils.h"
#include <prime/KeyCode.h>

#include <algorithm>
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
    {"PIPE", "|"},
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
  if (!Definition(gameFunction).enabled)
    return {};
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
  if (!Definition(gameFunction).enabled)
    return false;
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

void MapKey::RegisterAction(GameFunction gameFunction, std::string_view key, std::string_view defaultBinding)
{ definitions.at(static_cast<std::size_t>(gameFunction)) = {std::string(key), std::string(defaultBinding)}; }

const MapKey::ActionDefinition& MapKey::Definition(GameFunction gameFunction)
{ return definitions.at(static_cast<std::size_t>(gameFunction)); }

void MapKey::SetActionEnabled(GameFunction action, bool enabled)
{ definitions.at(static_cast<std::size_t>(action)).enabled = enabled; }

const std::vector<MapKey>& MapKey::Bindings(GameFunction gameFunction)
{ return mappedKeys.at(static_cast<std::size_t>(gameFunction)); }

bool MapKey::ReplaceBindings(GameFunction gameFunction, std::vector<MapKey> bindings)
{
  if (gameFunction < 0 || gameFunction >= GameFunction::Max || !Definition(gameFunction).enabled)
    return false;
  for (const auto& binding : bindings)
    if (binding.Key == KeyCode::None)
      return false;
  if (!bindings.empty())
    bindings.front().shortcutHint = CompactShortcutForHint(bindings.front().Shortcuts);
  mappedKeys[gameFunction].swap(bindings);
  return true;
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
  if (!Definition(gameFunction).enabled)
    return false;
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
  if (!Definition(gameFunction).enabled)
    return false;
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

bool MapKey::SameBinding(const MapKey& first, const MapKey& second)
{
  const auto containsAll = [](const auto& first, const auto& second) {
    return std::all_of(first.begin(), first.end(), [&](const auto& modifier) {
      return std::find(second.begin(), second.end(), modifier) != second.end();
    });
  };
  return first.Key != KeyCode::None && first.Key == second.Key && containsAll(first.Modifiers, second.Modifiers)
         && containsAll(second.Modifiers, first.Modifiers);
}

bool MapKey::MayOverlap(const MapKey& first, const MapKey& second)
{
  if (first.Key == KeyCode::None || first.Key != second.Key)
    return false;
  // Explicit modifiers are minimum requirements. Their union can satisfy both
  // bindings, whereas an unmodified binding rejects every modifier.
  return first.hasModifiers == second.hasModifiers;
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

std::array<MapKey::ActionDefinition, (int)GameFunction::Max> MapKey::definitions = {};

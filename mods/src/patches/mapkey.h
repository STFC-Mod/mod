#pragma once

#include "modifierkey.h"
#include <patches/gamefunctions.h>
#include <prime/KeyCode.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

class MapKey
{
public:
  struct ActionDefinition {
    std::string key, defaultBinding;
    bool        enabled = true;
  };
  MapKey();

  static MapKey Parse(std::string_view key);
  static void   AddMappedKey(GameFunction gameFunction, MapKey mappedKey);
  static void   RegisterAction(GameFunction action, std::string_view key, std::string_view defaultBinding);
  static const ActionDefinition&    Definition(GameFunction action);
  static void                       SetActionEnabled(GameFunction action, bool enabled);
  static const std::vector<MapKey>& Bindings(GameFunction action);
  static bool                       ReplaceBindings(GameFunction action, std::vector<MapKey> bindings);
  static bool                       SameBinding(const MapKey& first, const MapKey& second);
  // Advisory overlap under existing minimum-modifier dispatch rules.
  static bool   MayOverlap(const MapKey& first, const MapKey& second);
  static bool   HasBinding(GameFunction gameFunction);
  static bool   IsPressed(GameFunction gameFunction);
  static bool   IsDown(GameFunction gameFunction);
  static bool   HasCorrectModifiers(const MapKey& mapKey);

  static std::string GetShortcuts(GameFunction gameFunction);
  static std::string GetShortcutHint(GameFunction gameFunction);
  // Call once after config parsing, only when shortcut hints are enabled.
  static void CacheShortcutHints();

  std::string GetParsedValues() const;

  std::vector<ModifierKey> Modifiers;
  std::vector<std::string> Shortcuts;

  KeyCode Key;

private:
  static std::array<std::vector<MapKey>, (int)GameFunction::Max> mappedKeys;

  static std::array<ActionDefinition, (int)GameFunction::Max> definitions;

  bool        hasModifiers;
  std::string shortcutHint;
};

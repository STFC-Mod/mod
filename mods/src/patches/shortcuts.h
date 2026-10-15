#pragma once

// Configurable shortcuts: the actions they trigger, parsing [shortcuts] from the settings file, and
// matching the parsed key chords against this frame's input.

#include <toml++/toml.h>
#include <unity/input.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What a shortcut does. Each [shortcuts] setting binds key chords to one action.
enum class Action : uint8_t {
  kMoveLeft,
  kMoveRight,
  kMoveUp,
  kMoveDown,
  kSelectChatAlliance,
  kSelectChatGlobal,
  kSelectChatPrivate,
  kSelectShip1,
  kSelectShip2,
  kSelectShip3,
  kSelectShip4,
  kSelectShip5,
  kSelectShip6,
  kSelectShip7,
  kSelectShip8,
  kSelectCurrent,
  kShowAlliance,
  kShowAllianceArmada,
  kShowAllianceHelp,
  kShowArtifacts,
  kShowOfficers,
  kShowOfficerPresets,
  kShowCommander,
  kShowRefinery,
  kShowQTrials,
  kShowBookmarks,
  kShowLookup,
  kShowExoComp,
  kShowFactions,
  kShowGifts,
  kShowDaily,
  kShowAwayTeam,
  kShowMissions,
  kShowResearch,
  kShowScrapYard,
  kShowShips,
  kShowInventory,
  kShowStationInterior,
  kShowHaven,
  kShowStationExterior,
  kShowGalaxy,
  kNativeShortcutGalaxy,
  kShowSystem,
  kShowChat,
  kShowChatSide1,
  kShowChatSide2,
  kShowEvents,
  kNativeShortcutEvents,
  kShowSettings,
  kToggleShortcutHints,
  kZoomPreset1,
  kZoomPreset2,
  kZoomPreset3,
  kZoomPreset4,
  kZoomPreset5,
  kZoomIn,
  kZoomOut,
  kZoomMin,
  kZoomMax,
  kZoomReset,
  kUiScaleUp,
  kUiScaleDown,
  kUiShipScaleUp,
  kUiShipScaleDown,
  kUiViewerScaleUp,
  kUiViewerScaleDown,
  kActionPrimary,
  kActionSecondary,
  kActionQueue,
  kActionQueueClear,
  kActionView,
  kActionRecall,
  kActionRecallCancel,
  kActionRepair,
  kSetZoomPreset1,
  kSetZoomPreset2,
  kSetZoomPreset3,
  kSetZoomPreset4,
  kSetZoomPreset5,
  kSetZoomDefault,
  kDisableHotKeys,
  kEnableHotKeys,
  kToggleQueue,
  kToggleAutoConfirmInstantWarp,
  kTogglePreviewLocate,
  kTogglePreviewRecall,
  kToggleCargoDefault,
  kToggleCargoPlayer,
  kToggleCargoStation,
  kToggleCargoHostile,
  kToggleCargoArmada,
  kLogLevelDebug,
  kLogLevelInfo,
  kLogLevelTrace,
  kLogLevelError,
  kLogLevelWarn,
  kLogLevelOff,
  kRestart,
  kReloadSettings,
  kQuit,
  kFocusSearch,
  kShowShipConstruction,
  kShowOfficerSwap,
  kShowShipSwap,
  kShowShields,
  kShowBattlelogs,

  kCount
};

// One key with its required modifiers, such as CTRL-SHIFT-F1.
struct KeyChord {
  unity::KeyCode key = unity::KeyCode::None;
  // Each group is satisfied by either of its keys: SHIFT is {LeftShift, RightShift}, LSHIFT is LeftShift twice.
  std::vector<std::array<unity::KeyCode, 2>> modifiers;
  // The upper-cased tokens for display, modifiers first and the key last: {"CTRL", "SHIFT", "F1"}.
  std::vector<std::string> tokens;

  // Parses one chord. Unknown tokens are ignored; a chord without a non-modifier key has key == None. With two
  // keys, the last one counts.
  static KeyChord Parse(std::string_view text);

  // "CTRL-SHIFT-F1"
  std::string Text() const;
  // Compact form for the game's keybind badges: "^+F1".
  std::string Hint() const;
};

namespace Shortcut
{
// Replaces every binding with the [shortcuts] entries from `config`, and records the effective bindings and where
// each came from in `parsed` ([shortcuts] and [shortcuts_source]). `extended` and `experimental` gate the optional
// actions.
void Load(const toml::table& config, toml::table& parsed, bool extended, bool experimental);

// Adds a binding directly. Load uses this; tests use it to set up bindings.
void Bind(Action action, KeyChord chord);
void Clear();

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

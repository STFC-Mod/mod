#pragma once

// Configurable shortcuts: the actions they trigger, parsing [shortcuts] from the settings file, and
// matching the parsed key chords against this frame's input.

#include <prime/KeyCode.h>

#include <toml++/toml.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

enum GameFunction {
  MoveLeft,
  MoveRight,
  MoveUp,
  MoveDown,
  SelectChatAlliance,
  SelectChatGlobal,
  SelectChatPrivate,
  SelectShip1,
  SelectShip2,
  SelectShip3,
  SelectShip4,
  SelectShip5,
  SelectShip6,
  SelectShip7,
  SelectShip8,
  SelectCurrent,
  ShowAlliance,
  ShowAllianceArmada,
  ShowAllianceHelp,
  ShowArtifacts,
  ShowOfficers,
  ShowOfficerPresets,
  ShowCommander,
  ShowRefinery,
  ShowQTrials,
  ShowBookmarks,
  ShowLookup,
  ShowExoComp,
  ShowFactions,
  ShowGifts,
  ShowDaily,
  ShowAwayTeam,
  ShowMissions,
  ShowResearch,
  ShowScrapYard,
  ShowShips,
  ShowInventory,
  ShowStationInterior,
  ShowHaven,
  ShowStationExterior,
  ShowGalaxy,
  NativeShortcutGalaxy,
  ShowSystem,
  ShowChat,
  ShowChatSide1,
  ShowChatSide2,
  ShowEvents,
  NativeShortcutEvents,
  ShowSettings,
  ToggleShortcutHints,
  ZoomPreset1,
  ZoomPreset2,
  ZoomPreset3,
  ZoomPreset4,
  ZoomPreset5,
  ZoomIn,
  ZoomOut,
  ZoomMin,
  ZoomMax,
  ZoomReset,
  UiScaleUp,
  UiScaleDown,
  UiShipScaleUp,
  UiShipScaleDown,
  UiViewerScaleUp,
  UiViewerScaleDown,
  ActionPrimary,
  ActionSecondary,
  ActionQueue,
  ActionQueueClear,
  ActionView,
  ActionRecall,
  ActionRecallCancel,
  ActionRepair,
  SetZoomPreset1,
  SetZoomPreset2,
  SetZoomPreset3,
  SetZoomPreset4,
  SetZoomPreset5,
  SetZoomDefault,
  DisableHotKeys,
  EnableHotKeys,
  ToggleQueue,
  ToggleAutoConfirmInstantWarp,
  TogglePreviewLocate,
  TogglePreviewRecall,
  ToggleCargoDefault,
  ToggleCargoPlayer,
  ToggleCargoStation,
  ToggleCargoHostile,
  ToggleCargoArmada,
  LogLevelDebug,
  LogLevelInfo,
  LogLevelTrace,
  LogLevelError,
  LogLevelWarn,
  LogLevelOff,
  Restart,
  ReloadSettings,
  Quit,
  FocusSearch,
  ShowShipConstruction,
  ShowOfficerSwap,
  ShowShipSwap,
  ShowShields,
  ShowBattlelogs,

  Max
};

// One key with its required modifiers, such as CTRL-SHIFT-F1.
struct KeyChord {
  KeyCode key = KeyCode::None;
  // Each group is satisfied by either of its keys: SHIFT is {LeftShift, RightShift}, LSHIFT is LeftShift twice.
  std::vector<std::array<KeyCode, 2>> modifiers;
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
void Bind(GameFunction action, KeyChord chord);
void Clear();

bool HasBinding(GameFunction action);
// Any binding of `action` is held with its modifiers. A binding without modifiers needs none held; one with
// modifiers still fires when extra modifiers are held (CTRL-F1 fires under CTRL+SHIFT).
bool IsPressed(GameFunction action);
// Any binding of `action` went down this frame with its modifiers held.
bool IsDown(GameFunction action);
// A binding with modifiers matched on `key`, which is still held. Plain bindings on it wait for the release.
bool Owns(KeyCode key);

// "CTRL-G | F8"
std::string Describe(GameFunction action);
// The badge text of the first binding, or empty when unbound.
std::string Hint(GameFunction action);
} // namespace Shortcut

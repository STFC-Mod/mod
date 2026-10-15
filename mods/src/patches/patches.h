#pragma once

// The mod's features: the descriptors mod.cc installs, and the calls the shortcut dispatch in hotkeys.cc makes into
// the features that own the work. Grouped by the file that defines them.

#include "config.h"

#include <cstdint>

struct NavigationZoom;
enum class NodeDepth : int32_t;

// What mod.cc installs. Only the [patches] hook groups decide what is installed, so users can avoid clashing with
// other mods; feature settings are read inside the hooks, which keeps every feature switchable at runtime.
struct Feature {
  const char* name;
  // The [patches] group that installs it; null for the few that always install.
  bool Config::Patches::* group;
  // Null for frame work that needs no hooks.
  void (*install)();
  // Runs every frame from the frame tick once installed, in every hotkey mode, before the shortcuts. Returns at once
  // when its feature is off.
  void (*frame)() = nullptr;
};

// alliance_store.cc
extern const Feature kAllianceGamesStore;

// anomaly_timer.cc
extern const Feature kAnomalyTimer;

// audio.cc
extern const Feature kAudio;

// buffs.cc
extern const Feature kOutOfDockPower;
extern const Feature kBuffCrashGuard;

// camera.cc
extern const Feature kZoom;
extern const Feature kPan;

// cargo.cc
extern const Feature kCargoFormat;
extern const Feature kInstantCargo;

// chat.cc
extern const Feature kChat;
// Opens chat, or focuses side chat's input. Never swallows the key.
void OpenChatShortcut();
// In chat, the channel shortcuts. True when the key was used.
bool ChatChannelShortcuts();

// dock.cc
extern const Feature kPinnedShips;
extern const Feature kDoubleClickAssign;
extern const Feature kAssignOnEnter;

// fleet_actions.cc
extern const Feature kQueueSwitch;
// The pre-scan cargo hooks; installed with the hotkeys.
void InstallFleetActions();
// A target or object viewer card is open in space.
bool TargetViewerShown();
// The number keys. True when the key was used.
bool SelectShipShortcuts();
// Select current and the queue switch. True when the key was used.
bool FleetShortcuts();
// The actions on the selected fleet and the open viewer. True when the key must not reach the game.
bool FleetActionShortcuts();

// fleet_labels.cc, fed by camera.cc's zoom hooks.
extern const Feature kFleetLabels;
void                 FleetLabelsFollowDepth(NavigationZoom* zoom, NodeDepth depth);
void                 FleetLabelsAfterZoom(NavigationZoom* zoom);

// forbidden_tech.cc
extern const Feature kForbiddenTechConfirm;
extern const Feature kForbiddenTechShipName;

// game_config.cc
extern const Feature kGameConfigOverrides;

// haven.cc
extern const Feature kHavenZoom;
extern const Feature kHavenHistory;

// hotkeys.cc
extern const Feature kHotkeys;
// The shortcut dispatch for one frame, run by the frame tick. True when the game's own ScreenManager.Update must
// not run, so it does not handle the key as well. False until kHotkeys is installed.
bool HandleShortcuts();

// instant_warp.cc
extern const Feature kInstantWarp;
void                 CycleInstantWarpConfirmation();

// keybind_badges.cc, installed with the hotkeys.
void InstallKeybindBadges();
// Toggles the game's keybind badges. False when the badges are not the mod's, so the key is not used.
bool ToggleKeybindBadges();
void HideKeybindBadges();

// loading_screens.cc
extern const Feature kLoginScreen;
extern const Feature kTransitionScreen;
extern const Feature kLoadingTips;

// missions.cc
extern const Feature kMissionHud;
extern const Feature kDailyClaimFilter;

// officers.cc
extern const Feature kOfficerSort;

// screens.cc
// Presses a button a screen shortcut opened a screen for, once the screen is up. True when one was pressed.
bool PressAwaitedButton();
// The shortcuts that open, step through, or search game screens. True when the key must not reach the game.
bool ScreenShortcuts();

// shop.cc
extern const Feature kGiftsAutoOpen;
extern const Feature kShopLimits;
extern const Feature kSkipRevealSequence;
extern const Feature kArtifactExchange;

// toasts.cc
extern const Feature kToasts;

// tracked_objects.cc
extern const Feature kObjectTracker;

// ui_scale.cc
extern const Feature kUiScale;
void                 AdjustUiScale(bool up);
void                 AdjustUiShipScale(bool up);
void                 AdjustUiViewerScale(bool up);

// window.cc
#if _WIN32
extern const Feature kFreeResize;
extern const Feature kCursor;
#endif
// The monitor's physical-to-logical pixel ratio on Windows; 1 elsewhere. Cached until `refresh`.
float WindowDpiScale(bool refresh = false);

// sync/sync.cc
extern const Feature kSync;




extern const Feature kInteriorZoom;
extern const Feature kChatTitles;
extern const Feature kRevealConfirm;
extern const Feature kActionQueueRecovery;
extern const Feature kQueueAddressGuard;
extern const Feature kThinQueueProtection;
extern const Feature kPinnedShipClick;
extern const Feature kDockSelection;
extern const Feature kAbilityButtons;

#pragma once

// The mod's features. ApplyPatches is the entry point the platform loaders call; the rest is the table of
// features patches.cc installs, plus the few calls features make into each other.

// Loads GameAssembly and hooks il2cpp_init, which installs everything else once the runtime is up.
void ApplyPatches();

void InstallAudioEventHooks();
void InstallBuffFixHooks();
void InstallCargoFormatHooks();
void InstallChatPatches();
void InstallDailyFactionBulkClaimHooks();
void InstallDoubleClickAssignShipHooks();
void InstallForbiddenTechConfirmationHooks();
void InstallForbiddenTechShipNameHooks();
void InstallFreeResizeHooks();
void InstallGalacticAnomalyTimer();
void InstallGiftsBulkClaimHooks();
void InstallHotkeyHooks();
void InstallInstantCargoCounterHooks();
void InstallInstantWarpConfirmationHooks();
void InstallLoadingScreenHooks();
void InstallLoadingTipHooks();
void InstallMiscPatches();
void InstallMissionHudTweaksHooks();
void InstallArtifactExchangeHooks();
void InstallObjectTrackers();
void InstallOfficerSortHooks();
void InstallPanHooks();
void InstallPinnedShipSortHooks();
void InstallSyncPatches();
void InstallTempCrashFixes();
void InstallTestPatches();
void InstallToastBannerHooks();
void InstallTransitionScreenHooks();
void InstallUiScaleHooks();
void InstallZoomHooks();

// ScreenManager.Update is the mod's per-frame tick. Hotkeys own the detour; features that poll every frame ask
// for it with this, and hotkeys.cc calls their Update functions in order. Safe to call more than once.
bool InstallScreenManagerUpdateHook();
// Reloads the settings when the settings file changes.
void InstallSettingsReload();
void AllianceGamesStoreUpdate();
void AssignShipEnterKeyUpdate();
void ChatOptionsUpdate();
void DailyFactionBulkClaimUpdate();
void FleetSyncUpdate();
void GalacticAnomalyTimerUpdate();
void WarpActionLabelUpdate();

// Hotkeys and their helpers in shortcut_hints.cc and space_actions.cc.
void InstallShortcutHintHooks();
// Toggles the game's keybind badges. False when the badges are not the mod's, so the key is not used.
bool ToggleShortcutBadges();
void HideShortcutBadges();
void InstallSpaceActionHooks();
// A target or object viewer card is open in space.
bool TargetViewerShown();
// The fleet and viewer half of the shortcut dispatch. True when the key must not reach the game.
bool SpaceActionsUpdate();

// UI scale hotkeys, implemented in ui_scale.cc.
void AdjustUiScale(bool up);
void AdjustUiShipScale(bool up);
void AdjustUiViewerScale(bool up);

// The monitor's physical-to-logical pixel ratio on Windows; 1 elsewhere. Cached until `refresh`.
float WindowDpiScale(bool refresh = false);

void InstallHavenZoomHooks();
void InstallHavenHistoryHooks();
void InstallAllianceGamesStoreHooks();

void InstallInteriorZoomHooks();
void InstallChatTitleDisplayHooks();
void InstallPinnedShipClickHooks();
void InstallRevealConfirmationHooks();
void InstallActionQueueRecovery();
void InstallQueueAddressGuard();
void InstallThinQueueProtection();

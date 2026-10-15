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
void AllianceGamesStoreUpdate();
void AssignShipEnterKeyUpdate();
void ChatOptionsUpdate();
void DailyFactionBulkClaimUpdate();
void FleetSyncUpdate();
void GalacticAnomalyTimerUpdate();
void WarpActionLabelUpdate();

// UI scale hotkeys, implemented in ui_scale.cc.
void AdjustUiScale(bool up);
void AdjustUiShipScale(bool up);
void AdjustUiViewerScale(bool up);

// The monitor's physical-to-logical pixel ratio on Windows; 1 elsewhere. Cached until `refresh`.
float WindowDpiScale(bool refresh = false);

void InstallHavenZoomHooks();
void InstallHavenHistoryHooks();

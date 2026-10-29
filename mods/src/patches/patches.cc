#include "patches.h"
#include "file.h"
#include "log_history.h"
#include "version.h"

#include <il2cpp/il2cpp-functions.h>

#include <spud/detour.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#if _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#endif

void InstallUiScaleHooks();
void InstallZoomHooks();
void InstallHavenZoomHooks();
void InstallInteriorZoomHooks();
void InstallBuffFixHooks();
#if _WIN32
void InstallFreeResizeHooks();
#endif
void InstallToastBannerHooks();
void InstallPanHooks();
void InstallHotkeyHooks();
void InstallGiftsBulkClaimHooks();
void InstallDailyFactionBulkClaimHooks();

void InstallTestPatches();
void InstallMiscPatches();
void InstallMissionHudTweaksHooks();
void InstallArtifactExchangeHooks();
void InstallChatPatches();
void InstallChatTitleDisplayHooks();
void InstallTempCrashFixes();
void InstallSyncPatches();
void InstallObjectTrackers();
void InstallLoadingScreenHooks();
void InstallTransitionScreenHooks();
void InstallGalacticAnomalyTimer();
void InstallLoadingTipHooks();
void InstallCargoFormatHooks();
void InstallInstantCargoCounterHooks();
void InstallOfficerSortHooks();
void InstallPinnedShipSortHooks();
void InstallPinnedShipClickHooks();
void InstallDoubleClickAssignShipHooks();
void InstallInstantWarpConfirmationHooks();
void InstallForbiddenTechConfirmationHooks();
void InstallForbiddenTechShipNameHooks();
void InstallRevealConfirmationHooks();
void InstallAudioEventHooks();
void InstallActionQueueRecovery();
void InstallThinQueueProtection();
void InstallQueueAddressGuard();
void InstallHavenHistoryHooks();
void InstallAllianceGamesStoreHooks();

__int64 il2cpp_init_hook(auto original, const char* domain_name)
{
  struct PatchEntry {
    const char*                  name;
    std::pair<void (*)(), bool*> fnAndEnabled;
  };

#if _WIN32
#ifndef NDEBUG
  AllocConsole();
  FILE* fp;
  freopen_s(&fp, "CONOUT$", "w", stdout);
#endif
#endif

  File::Init();

  std::string rotation_error;
  try {
    RotateLogHistory(File::Log());
  } catch (const std::filesystem::filesystem_error& error) {
    rotation_error = error.what();
  }

  // Append if rotation failed so an existing log is never truncated.
  auto file_logger = spdlog::basic_logger_mt("default", File::Log(), false);
  auto sink        = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  file_logger->sinks().push_back(sink);
  spdlog::set_default_logger(file_logger);

  const auto log_level =
      File::hasTrace() ? spdlog::level::trace : (File::hasDebug() ? spdlog::level::debug : spdlog::level::info);

  spdlog::set_level(log_level);
  spdlog::flush_on(log_level);
  if (!rotation_error.empty()) {
    spdlog::warn("Failed to rotate log history; preserving existing log: {}", rotation_error);
  }

#if VERSION_PATCH
  if constexpr (sizeof(VERSION_COMMIT_HASH) > 1) {
    spdlog::info("Initializing STFC Community Mod ({} [{}])", VER_PRODUCT_VERSION_STR, VERSION_COMMIT_HASH);
  } else {
    spdlog::info("Initializing STFC Community Mod ({})", VER_PRODUCT_VERSION_STR);
  }
#else
  spdlog::info("Initializing STFC Community Mod ({})", VER_PRODUCT_VERSION_STR);
#endif
  spdlog::info("");
  if (File::hasCustomNames()) {
    spdlog::info("Using custom names");
  } else {
    spdlog::info("Using standard names");
  }

  spdlog::info("  Log: {}", File::Log());
  spdlog::info("  Cfg: {}", File::Config());
  spdlog::info("  Var: {}", File::Vars());
  spdlog::info("   BL: {}", File::Battles());
  spdlog::info("");

#if VERSION_PATCH
  spdlog::warn("*** NOTE: Beta versions may have unexpected bugs and issues");
  spdlog::info("");
#endif

  spdlog::info("Please see https://github.com/STFC-Mod/mod for latest configuration help,");
  spdlog::info("examples and future releases, or visit the STFC Community Mod discord server");
  spdlog::info("at https://discord.gg/PrpHgs7Vjs");
  spdlog::info("");
  spdlog::info("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
  spdlog::info("");
  spdlog::info("Loading Configuration...");
  spdlog::info("");

  static auto& cfg = Config::Get();

  spdlog::info("");
  spdlog::info("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
  spdlog::info("");

  spdlog::info("Initializing code hooks:");
  const PatchEntry patches[] = {
      {"UiScaleHooks", {InstallUiScaleHooks, &cfg.installUiScaleHooks}},
      {"ZoomHooks", {InstallZoomHooks, &cfg.installZoomHooks}},
      {"HavenZoomHooks", {InstallHavenZoomHooks, &cfg.installHavenZoomHooks}},
      {"InteriorZoomHooks", {InstallInteriorZoomHooks, &cfg.installInteriorZoomHooks}},
      {"BuffFixHooks", {InstallBuffFixHooks, &cfg.installBuffFixHooks}},
      {"ToastBannerHooks", {InstallToastBannerHooks, &cfg.installToastBannerHooks}},
      {"PanHooks", {InstallPanHooks, &cfg.installPanHooks}},
      {"HotkeyHooks", {InstallHotkeyHooks, &cfg.installHotkeyHooks}},
      {"GiftsBulkClaimHooks", {InstallGiftsBulkClaimHooks, &cfg.installGiftsBulkClaimHooks}},
      {"DailyFactionBulkClaimHooks", {InstallDailyFactionBulkClaimHooks, &cfg.installDailyFactionBulkClaimHooks}},
#if _WIN32
      {"FreeResizeHooks", {InstallFreeResizeHooks, &cfg.installFreeResizeHooks}},
#endif
      {"TempCrashFixes", {InstallTempCrashFixes, &cfg.installTempCrashFixes}},
      {"TestPatches", {InstallTestPatches, &cfg.installTestPatches}},
      {"MiscPatches", {InstallMiscPatches, &cfg.installMiscPatches}},
      {"MissionHudTweaksHooks", {InstallMissionHudTweaksHooks, &cfg.installMissionHudTweaksHooks}},
      {"ArtifactExchangeHooks", {InstallArtifactExchangeHooks, &cfg.installArtifactExchangeHooks}},
      {"ChatPatches", {InstallChatPatches, &cfg.installChatPatches}},
      {"ChatTitleDisplay", {InstallChatTitleDisplayHooks, &cfg.installPlayerTitleDisplayHooks}},
      {"SyncPatches", {InstallSyncPatches, &cfg.installSyncPatches}},
      {"ObjectTracker", {InstallObjectTrackers, &cfg.installObjectTracker}},
      {"LoadingScreen", {InstallLoadingScreenHooks, &cfg.installLoadingScreenHooks}},
      {"TransitionScreen", {InstallTransitionScreenHooks, &cfg.installTransitionScreenHooks}},
      {"GalacticAnomalyTimer", {InstallGalacticAnomalyTimer, &cfg.installGalacticAnomalyTimerHooks}},
      {"LoadingTip", {InstallLoadingTipHooks, &cfg.installLoadingTipHooks}},
      {"InstantCargoCounter", {InstallInstantCargoCounterHooks, &cfg.installInstantCargoCounterHooks}},
      {"CargoFormat", {InstallCargoFormatHooks, &cfg.installCargoFormatHooks}},
      {"OfficerSortHooks", {InstallOfficerSortHooks, &cfg.installOfficerSortHooks}},
      {"PinnedShipSort", {InstallPinnedShipSortHooks, &cfg.installPinnedShipSortHooks}},
      {"PinnedShipClick", {InstallPinnedShipClickHooks, &cfg.installPinnedShipClickToggle}},
      {"DoubleClickAssignShip", {InstallDoubleClickAssignShipHooks, &cfg.installDoubleClickAssignShipHooks}},
      {"InstantWarpConfirm", {InstallInstantWarpConfirmationHooks, &cfg.installInstantWarpConfirmationHooks}},
      {"MessageBoxConfirmations", {InstallForbiddenTechConfirmationHooks, &cfg.installForbiddenTechConfirmationHooks}},
      {"ForbiddenTechShipName", {InstallForbiddenTechShipNameHooks, &cfg.installForbiddenTechShipNameHooks}},
      {"RevealConfirm", {InstallRevealConfirmationHooks, &cfg.installRevealConfirmationHooks}},
      {"AudioEvents", {InstallAudioEventHooks, &cfg.installAudioEventHooks}},
      {"ActionQueueRecovery", {InstallActionQueueRecovery, &cfg.installActionQueueRecoveryHooks}},
      {"QueueAddressGuard", {InstallQueueAddressGuard, &cfg.installQueueAddressGuardHooks}},
      {"ThinQueueProtection", {InstallThinQueueProtection, &cfg.installThinQueueProtectionHooks}},
      {"HavenHistory", {InstallHavenHistoryHooks, &cfg.installHavenHistoryHooks}},
      {"AllianceGamesStore", {InstallAllianceGamesStoreHooks, &cfg.installAllianceGamesStoreHooks}},
  };
  printf("il2cpp_init_hook(%s)\n", domain_name);

  auto r = original(domain_name);

  auto patch_count = 0;
  auto patch_total = sizeof(patches) / sizeof(patches[0]);

  for (const auto& patch : patches) {
    patch_count++;
    const auto [patch_func, patch_enabled] = patch.fnAndEnabled;
    const auto patch_install               = (patch_enabled && *patch_enabled);
    const auto patch_mode                  = patch_install ? "+ Patch" : "x Skipp";
    spdlog::info(" {}ing {:>2} of {} ({})", patch_mode, patch_count, patch_total, patch.name);

    if (patch_install) {
      patch_func();
    }
  }

  spdlog::info("");

#if VERSION_PATCH
  spdlog::info("Installed beta version {}.{}.{} (Patch {})", VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION,
               VERSION_PATCH);
#else
  spdlog::info("Installed release version {}.{}.{}", VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION);
#endif

  spdlog::info("");
  spdlog::info("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
  spdlog::info("");

  return r;
}

void ApplyPatches()
{
#if _WIN32
  auto assembly = LoadLibraryA("GameAssembly.dll");
#else
  char     buf[PATH_MAX];
  uint32_t bufsize = PATH_MAX;
  _NSGetExecutablePath(buf, &bufsize);

  char assembly_path[PATH_MAX];
  snprintf(assembly_path, sizeof(assembly_path), "%s/%s", dirname(buf), "../Frameworks/GameAssembly.dylib");
  printf("Loading %s\n", assembly_path);
  auto assembly = dlopen(assembly_path, RTLD_LAZY | RTLD_GLOBAL);

  init_il2cpp_pointers();
#endif

  if (assembly == nullptr) {
    spdlog::error("Failed to load GameAssembly");
    return;
  } else {
    try {
#if _WIN32
      auto n = GetProcAddress(assembly, "il2cpp_init");
#else
      auto n = dlsym(assembly, "il2cpp_init");
#endif
      printf("Got il2cpp_init %p\n", n);

      SPUD_STATIC_DETOUR(n, il2cpp_init_hook);
    } catch (...) {
      // Failed to Apply at least some patches
    }
  }
}

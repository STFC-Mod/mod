#include "patches/patches.h"

#include "config.h"
#include "file.h"
#include "version.h"

#include <il2cpp/il2cpp.h>
#include <spud/detour.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <iterator>

#if _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#endif

namespace
{
struct Patch {
  const char* name;
  void (*install)();
  bool (*enabled)(const Config&);
};

// Installed in this order; the log numbers them. Most are switched by their [patches] setting; the rest only
// install when the feature they serve is turned on.
constexpr Patch kPatches[] = {
    {"interiorzoomhooks", InstallInteriorZoomHooks, [](const Config& c) { return c.patches.interiorzoomhooks; }},
    {"chattitledisplayhooks", InstallChatTitleDisplayHooks, [](const Config& c) { return c.patches.chattitledisplayhooks; }},
    {"revealconfirmhooks", InstallRevealConfirmationHooks, [](const Config& c) { return c.patches.revealconfirmhooks; }},
    {"actionqueuerecoveryhooks", InstallActionQueueRecovery, [](const Config& c) { return c.patches.actionqueuerecoveryhooks; }},
    {"queueaddressguardhooks", InstallQueueAddressGuard, [](const Config& c) { return c.patches.queueaddressguardhooks; }},
    {"thinqueueprotectionhooks", InstallThinQueueProtection, [](const Config& c) { return c.patches.thinqueueprotectionhooks; }},
    {"pinnedshipclicktoggle", InstallPinnedShipClickHooks, [](const Config& c) { return c.patches.pinnedshipclicktoggle; }},

    {"HavenZoomHooks", InstallHavenZoomHooks, [](const Config& c) { return c.patches.havenzoomhooks; }},
    {"HavenHistory", InstallHavenHistoryHooks, [](const Config& c) { return c.patches.havenhistoryhooks; }},
    {"AllianceGamesStore", InstallAllianceGamesStoreHooks,
     [](const Config& c) { return c.patches.alliancegamesstorehooks; }},
    {"UiScaleHooks", InstallUiScaleHooks, [](const Config& c) { return c.patches.uiscalehooks; }},
    {"ZoomHooks", InstallZoomHooks, [](const Config& c) { return c.patches.zoomhooks; }},
    {"BuffFixHooks", InstallBuffFixHooks, [](const Config& c) { return c.patches.bufffixhooks; }},
    {"ToastBannerHooks", InstallToastBannerHooks, [](const Config& c) { return c.patches.toastbannerhooks; }},
    {"PanHooks", InstallPanHooks, [](const Config& c) { return c.patches.panhooks; }},
    {"HotkeyHooks", InstallHotkeyHooks, [](const Config& c) { return c.patches.hotkeyhooks; }},
    {"GiftsBulkClaimHooks", InstallGiftsBulkClaimHooks, [](const Config& c) { return c.patches.giftsbulkclaimhooks; }},
    {"DailyFactionBulkClaimHooks", InstallDailyFactionBulkClaimHooks,
     [](const Config& c) { return c.patches.dailyfactionbulkclaimhooks; }},
#if _WIN32
    {"FreeResizeHooks", InstallFreeResizeHooks, [](const Config& c) { return c.patches.freeresizehooks; }},
#endif
    {"TempCrashFixes", InstallTempCrashFixes, [](const Config& c) { return c.patches.tempcrashfixes; }},
    {"TestPatches", InstallTestPatches, [](const Config& c) { return c.patches.testpatches; }},
    {"MiscPatches", InstallMiscPatches, [](const Config& c) { return c.patches.miscpatches; }},
    {"MissionHudTweaksHooks", InstallMissionHudTweaksHooks,
     [](const Config& c) { return c.patches.missionhudtweakshooks; }},
    {"ArtifactExchangeHooks", InstallArtifactExchangeHooks,
     [](const Config& c) { return c.patches.artifactexchangehooks; }},
    {"ChatPatches", InstallChatPatches, [](const Config& c) { return c.patches.chatpatches; }},
    {"SyncPatches", InstallSyncPatches, [](const Config& c) { return c.patches.syncpatches; }},
    {"ObjectTracker", InstallObjectTrackers, [](const Config& c) { return c.patches.objecttracker; }},
    {"LoadingScreen", InstallLoadingScreenHooks, [](const Config& c) { return c.patches.loadingscreenhooks; }},
    {"TransitionScreen", InstallTransitionScreenHooks, [](const Config& c) { return c.patches.transitionscreenhooks; }},
    {"GalacticAnomalyTimer", InstallGalacticAnomalyTimer,
     [](const Config& c) { return c.patches.galacticanomalytimerhooks; }},
    {"LoadingTip", InstallLoadingTipHooks, [](const Config& c) { return c.patches.loadingtiphooks; }},
    {"InstantCargoCounter", InstallInstantCargoCounterHooks,
     [](const Config& c) { return c.patches.instantcargocounterhooks; }},
    {"CargoFormat", InstallCargoFormatHooks, [](const Config& c) { return c.patches.cargoformathooks; }},
    {"OfficerSortHooks", InstallOfficerSortHooks, [](const Config& c) { return c.patches.officersorthooks; }},
    {"PinnedShipSort", InstallPinnedShipSortHooks, [](const Config& c) { return c.patches.pinnedshiphooks; }},
    {"DoubleClickAssignShip", InstallDoubleClickAssignShipHooks,
     [](const Config& c) { return c.patches.doubleclickassignshiphooks; }},
    {"InstantWarpConfirm", InstallInstantWarpConfirmationHooks, [](const Config&) { return true; }},
    {"ForbiddenTechConfirm", InstallForbiddenTechConfirmationHooks,
     [](const Config& c) { return c.patches.forbiddentechconfirmhooks; }},
    {"ForbiddenTechShipName", InstallForbiddenTechShipNameHooks,
     [](const Config& c) { return c.patches.forbiddentechshipnamehooks; }},
    {"AudioEvents", InstallAudioEventHooks, [](const Config& c) { return c.patches.audioeventhooks; }},
    {"SettingsReload", InstallSettingsReload, [](const Config&) { return true; }},
};

void StartLogging()
{
  std::string rotation_error;
  try {
    const auto backup = [](int index) { return std::filesystem::path(File::Log() + "." + std::to_string(index)); };
    std::filesystem::remove(backup(5));
    for (int index = 4; index >= 1; --index)
      if (std::filesystem::exists(backup(index)))
        std::filesystem::rename(backup(index), backup(index + 1));
    if (std::filesystem::exists(File::Log()))
      std::filesystem::rename(File::Log(), backup(1));
  } catch (const std::filesystem::filesystem_error& error) {
    rotation_error = error.what();
  }
  // Append if rotation failed so an existing session log is not truncated.
  auto logger = spdlog::basic_logger_mt("default", File::Log(), false);
  logger->sinks().push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  spdlog::set_default_logger(logger);

  const auto level = File::Trace() ? spdlog::level::trace : File::Debug() ? spdlog::level::debug : spdlog::level::info;
  spdlog::set_level(level);
  spdlog::flush_on(level);
  if (!rotation_error.empty())
    spdlog::warn("Failed to rotate log history; preserving existing log: {}", rotation_error);
  il2cpp::SetErrorHandler([](std::string_view message) { spdlog::error("{}", message); });
}

void LogBanner()
{
  if constexpr (VERSION_PATCH && sizeof(VERSION_COMMIT_HASH) > 1)
    spdlog::info("Initializing STFC Community Mod ({} [{}])", VER_PRODUCT_VERSION_STR, VERSION_COMMIT_HASH);
  else
    spdlog::info("Initializing STFC Community Mod ({})", VER_PRODUCT_VERSION_STR);
  spdlog::info("");
  spdlog::info("{}", File::Profile().empty() ? "Using standard names" : "Using custom names");
  spdlog::info("  Log: {}", File::Log());
  spdlog::info("  Cfg: {}", File::Config());
  spdlog::info("  Var: {}", File::Vars());
  spdlog::info("   BL: {}", File::Battles());
  spdlog::info("");
  if constexpr (VERSION_PATCH) {
    spdlog::warn("*** NOTE: Beta versions may have unexpected bugs and issues");
    spdlog::info("");
  }
  spdlog::info("Please see https://github.com/STFC-Mod/mod for latest configuration help,");
  spdlog::info("examples and future releases, or visit the STFC Community Mod discord server");
  spdlog::info("at https://discord.gg/PrpHgs7Vjs");
  spdlog::info("");
  spdlog::info("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
  spdlog::info("");
}

int64_t il2cpp_init_hook(auto original, const char* domain_name)
{
#if _WIN32 && !defined(NDEBUG)
  AllocConsole();
  FILE* console;
  freopen_s(&console, "CONOUT$", "w", stdout);
#endif

  File::Init();
  StartLogging();
  LogBanner();

  spdlog::info("Loading Configuration...");
  spdlog::info("");
  auto& config = Config::Get();
  spdlog::info("");
  spdlog::info("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
  spdlog::info("");

  // Bindings resolve against the runtime's metadata, so nothing is installed before the domain exists.
  const auto result = original(domain_name);

  spdlog::info("Initializing code hooks:");
  for (size_t i = 0; i < std::size(kPatches); ++i) {
    const auto& patch   = kPatches[i];
    const bool  enabled = patch.enabled(config);
    spdlog::info(" {} {:>2} of {} ({})", enabled ? "+ Patching" : "x Skipping", i + 1, std::size(kPatches), patch.name);
    if (enabled)
      patch.install();
  }

  spdlog::info("");
  if constexpr (VERSION_PATCH)
    spdlog::info("Installed beta version {}.{}.{} (Patch {})", VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION,
                 VERSION_PATCH);
  else
    spdlog::info("Installed release version {}.{}.{}", VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION);
  spdlog::info("");
  spdlog::info("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
  spdlog::info("");

  return result;
}
} // namespace

void ApplyPatches()
{
#if _WIN32
  auto assembly = LoadLibraryA("GameAssembly.dll");
#else
  char     executable[PATH_MAX];
  uint32_t size = sizeof(executable);
  if (_NSGetExecutablePath(executable, &size) != 0) {
    fprintf(stderr, "stfc-community-mod: executable path does not fit\n");
    return;
  }
  char assembly_path[PATH_MAX];
  snprintf(assembly_path, sizeof(assembly_path), "%s/../Frameworks/GameAssembly.dylib", dirname(executable));
  auto assembly = dlopen(assembly_path, RTLD_LAZY | RTLD_GLOBAL);
#endif

  // The logger does not exist yet; it is set up inside il2cpp_init once the settings path is known.
  if (!assembly) {
    fprintf(stderr, "stfc-community-mod: failed to load GameAssembly\n");
    return;
  }

  try {
    il2cpp::Initialize(assembly);
#if _WIN32
    auto il2cpp_init = reinterpret_cast<void*>(GetProcAddress(assembly, "il2cpp_init"));
#else
    auto il2cpp_init = dlsym(assembly, "il2cpp_init");
#endif
    SPUD_STATIC_DETOUR(il2cpp_init, il2cpp_init_hook);
  } catch (const std::exception& error) {
    fprintf(stderr, "stfc-community-mod: failed to hook il2cpp_init: %s\n", error.what());
  }
}

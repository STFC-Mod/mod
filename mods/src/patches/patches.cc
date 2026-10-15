#include "mod.h"

#include "config.h"
#include "patches/patches.h"
#include "file.h"
#include "version.h"

#include "prime/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <unity/input.h>

#include <filesystem>
#include <iterator>
#include <vector>

#if _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#endif

// The mod's startup: StartMod hooks il2cpp_init, which sets up logging and settings, installs the features, and
// installs the frame tick that runs them.

namespace
{
// Installed in this order; the log numbers them.
constexpr const Feature* kFeatures[] = {
    &kInteriorZoom,
    &kChatTitles,
    &kRevealConfirm,
    &kActionQueueRecovery,
    &kQueueAddressGuard,
    &kThinQueueProtection,
    &kPinnedShipClick,
    &kDockSelection,
    &kAbilityButtons,
    &kUiScale,
    &kZoom,
    &kHavenZoom,
    &kHavenHistory,
    &kAllianceGamesStore,
    &kFleetLabels,
    &kPan,
    &kOutOfDockPower,
    &kBuffCrashGuard,
    &kToasts,
    &kHotkeys,
    &kGiftsAutoOpen,
    &kDailyClaimFilter,
#if _WIN32
    &kFreeResize,
    &kCursor,
#endif
    &kSkipRevealSequence,
    &kGameConfigOverrides,
    &kQueueSwitch,
    &kShopLimits,
    &kMissionHud,
    &kArtifactExchange,
    &kChat,
    &kSync,
    &kObjectTracker,
    &kLoginScreen,
    &kTransitionScreen,
    &kAnomalyTimer,
    &kLoadingTips,
    &kInstantCargo,
    &kCargoFormat,
    &kOfficerSort,
    &kPinnedShips,
    &kDoubleClickAssign,
    &kAssignOnEnter,
    &kInstantWarp,
    &kForbiddenTechConfirm,
    &kForbiddenTechShipName,
    &kAudio,
};

// The per-frame work of the installed features, in install order.
std::vector<void (*)()> frame_work;

// Runs one piece of frame work. An exception must not unwind into the game's native frame.
void RunGuarded(void (*work)())
{
  try {
    work();
  } catch (const std::exception& error) {
    spdlog::warn("[Frame] update failed: {}", error.what());
  } catch (...) {
    spdlog::warn("[Frame] update failed with an unknown exception");
  }
}

// ScreenManager.Update is the mod's frame tick. It runs whatever hotkey mode is set: the settings file watch and
// each feature's frame work first, then the shortcut dispatch. Returning without calling the game's Update swallows
// the key, so the game's own handling of it does not run as well.
void ScreenManager_Update_Hook(auto original, ScreenManager* _this)
{
  unity::Key::Poll();
  RunGuarded(Config::ReloadIfChanged);
  for (auto work : frame_work)
    RunGuarded(work);
  if (HandleShortcuts())
    return;
  original(_this);
}

void InstallFeatures(const Config& config)
{
  spdlog::info("Initializing code hooks:");
  for (size_t i = 0; i < std::size(kFeatures); ++i) {
    const auto& feature = *kFeatures[i];
    const bool  enabled = !feature.group || config.patches.*feature.group;
    spdlog::info(" {} {:>2} of {} ({})", enabled ? "+ Patching" : "x Skipping", i + 1, std::size(kFeatures),
                 feature.name);
    if (!enabled)
      continue;
    try {
      if (feature.install)
        feature.install();
      if (feature.frame)
        frame_work.push_back(feature.frame);
    } catch (const std::exception& error) {
      spdlog::error("{} failed to install: {}", feature.name, error.what());
    }
  }
  // Last, so a feature never sees a frame before it is installed. Without the tick nothing polls, and the settings
  // file is only read at startup.
  if (!IL2CPP_HOOK(ScreenManager::Class::Method("Update"), ScreenManager_Update_Hook))
    spdlog::error("The frame tick is missing: shortcuts, settings reload and per-frame features will not run");
}

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

int64_t il2cpp_init_Hook(auto original, const char* domain_name)
{
#if _WIN32 && !defined(NDEBUG)
  AllocConsole();
  FILE* console;
  freopen_s(&console, "CONOUT$", "w", stdout);
#endif

  // Nothing here may throw into the game: a mod that cannot set itself up leaves the game running without it.
  const Config* config = nullptr;
  try {
    File::Init();
    StartLogging();
    LogBanner();

    spdlog::info("Loading Configuration...");
    spdlog::info("");
    config = &Config::Get();
    spdlog::info("");
    spdlog::info("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
    spdlog::info("");
  } catch (const std::exception& error) {
    // The file logger may not exist; spdlog falls back to its console logger.
    spdlog::error("Community mod setup failed, not installing any patches: {}", error.what());
  }

  // Bindings resolve against the runtime's metadata, so nothing is installed before the domain exists.
  const auto result = original(domain_name);
  if (!config)
    return result;

  InstallFeatures(*config);

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

void StartMod()
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
    if (!IL2CPP_DETOUR(il2cpp_init, il2cpp_init_Hook, "il2cpp_init"))
      fprintf(stderr, "stfc-community-mod: failed to hook il2cpp_init\n");
  } catch (const std::exception& error) {
    fprintf(stderr, "stfc-community-mod: failed to hook il2cpp_init: %s\n", error.what());
  }
}

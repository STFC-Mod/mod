// The mod's startup: StartMod hooks il2cpp_init, which sets up logging and settings, installs the features, and
// installs the frame tick that runs them.

#include "mod.h"

#include "config.h"
#include "features/features.h"
#include "file.h"
#include "version.h"

#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <unity/input.h>

#include <filesystem>
#include <format>
#include <iterator>
#include <string>
#include <vector>

#if _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <mach-o/dyld.h>
#endif

namespace
{
// --- Logging ---

// Opens the session log, keeping the previous five as <log>.1 (newest) to <log>.5.
void StartLogging()
{
  // File::Log() is UTF-8, and Windows reads a narrow file name in the ANSI code page, so every name here goes
  // through File::FromUtf8.
  const auto  log = File::FromUtf8(File::Log());
  std::string rotation_error;
  try {
    const auto backup = [](int index) { return File::FromUtf8(std::format("{}.{}", File::Log(), index)); };
    std::filesystem::remove(backup(5));
    for (int index = 4; index >= 1; --index)
      if (std::filesystem::exists(backup(index)))
        std::filesystem::rename(backup(index), backup(index + 1));
    if (std::filesystem::exists(log))
      std::filesystem::rename(log, backup(1));
  } catch (const std::filesystem::filesystem_error& error) {
    rotation_error = error.what();
  }
  // Append if rotation failed so an existing session log is not truncated. spdlog takes the path's native name,
  // which is wide on Windows (the build sets SPDLOG_WCHAR_FILENAMES there).
  auto logger = spdlog::basic_logger_mt("default", log.native(), false);
  logger->sinks().push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  spdlog::set_default_logger(logger);

  const auto level = File::Trace() ? spdlog::level::trace : File::Debug() ? spdlog::level::debug : spdlog::level::info;
  spdlog::set_level(level);
  spdlog::flush_on(level);
  if (!rotation_error.empty())
    spdlog::warn("could not rotate the log history; appending to the existing log: {}", rotation_error);
  il2cpp::SetErrorHandler([](std::string_view message) { spdlog::error("{}", message); });
}

// Between the banner, the settings and the install log.
void LogSeparator()
{
  spdlog::info("");
  spdlog::info("=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
  spdlog::info("");
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
  LogSeparator();
}

// --- Features and the frame tick ---

// Installed in this order, which the log numbers, and their frame work runs in this order too. Grouped by file as
// in features.h. No feature depends on another being installed first; the object tracker leads only so that every
// tracked class is hooked before anything else is.
constexpr const Feature* kFeatures[] = {
    // tracked_objects.cc
    &kObjectTracker,
    // action_queue.cc
    &kActionQueueRecovery,
    &kThinQueueProtection,
    &kQueueAddressGuard,
    // alliance_store.cc
    &kAllianceGamesStore,
    // anomaly_timer.cc
    &kAnomalyTimer,
    // audio.cc
    &kAudio,
    // buffs.cc
    &kOutOfDockPower,
    &kBuffCrashGuard,
    // camera.cc
    &kZoom,
    &kPan,
    &kInteriorZoom,
    // cargo.cc
    &kCargoFormat,
#if _WIN32
    &kInstantCargo,
#endif
    // chat.cc
    &kChat,
    &kChatTitles,
    // confirmations.cc
    &kForbiddenTechConfirm,
    &kRevealConfirm,
    // dock.cc
    &kPinnedShips,
    &kPinnedShipClick,
    &kDoubleClickAssign,
    &kAssignOnEnter,
    &kDockSelection,
    // fleet_actions.cc
    &kQueueSwitch,
    &kAbilityButtons,
    // fleet_labels.cc
    &kFleetLabels,
    // forbidden_tech_ship_name.cc
    &kForbiddenTechShipName,
    // game_config.cc
    &kGameConfigOverrides,
    // haven.cc
    &kHavenZoom,
    &kHavenHistory,
    // hotkeys.cc
    &kHotkeys,
    // instant_warp.cc
    &kInstantWarp,
    // loading_screens.cc
    &kLoginScreen,
    &kTransitionScreen,
    &kLoadingTips,
    // missions.cc
    &kMissionHud,
    &kDailyClaimFilter,
    // officers.cc
    &kOfficerSort,
    // shop.cc
    &kGiftsAutoOpen,
    &kShopLimits,
    &kSkipRevealSequence,
    &kArtifactExchange,
    // toasts.cc
    &kToasts,
    // ui_scale.cc
    &kUiScale,
// window.cc
#if _WIN32
    &kFreeResize,
    &kCursor,
#endif
    // sync/sync.cc
    &kSync,
};

// The installed features with frame work, in install order.
std::vector<const Feature*> frame_features;

// Runs one piece of frame work for `name`. An exception must not unwind into the game's native frame.
template <typename Work> void RunGuarded(const char* name, Work&& work)
{
  try {
    work();
  } catch (const std::exception& error) {
    spdlog::warn("[{}] frame update failed: {}", name, error.what());
  } catch (...) {
    spdlog::warn("[{}] frame update failed with an unknown exception", name);
  }
}

// ScreenManager.Update is the mod's frame tick and runs in every hotkey mode: input, the settings file watch and
// each installed feature's frame work, then the shortcut dispatch. Skipping the game's Update swallows a key a
// shortcut used, so the game does not handle it as well.
void ScreenManager_Update_Hook(auto original, ScreenManager* _this)
{
  unity::Key::Poll();
  RunGuarded("Settings", Config::ReloadIfChanged);
  for (const auto* feature : frame_features)
    RunGuarded(feature->name, feature->frame);
  bool handled = false;
  RunGuarded("Shortcuts", [&] { handled = HandleShortcuts(); });
  if (!handled)
    original(_this);
}

// Installs every feature whose [patches] group is on, then the frame tick that runs them.
void InstallFeatures(const Config& config)
{
  constexpr auto total = std::size(kFeatures);
  spdlog::info("Initializing code hooks:");
  for (size_t i = 0; i < total; ++i) {
    const auto& feature = *kFeatures[i];
    if (feature.group && !(config.patches.*feature.group)) {
      spdlog::info(" x Skipping {:>2} of {} ({})", i + 1, total, feature.name);
      continue;
    }
    bool installed = false;
    try {
      installed = !feature.install || feature.install();
    } catch (const std::exception& error) {
      spdlog::error("[{}] failed to install: {}", feature.name, error.what());
    }
    // Logged after installing, so the line says what happened; the reasons for a failure come just before it.
    if (!installed) {
      spdlog::warn(" ! Not installed {:>2} of {} ({})", i + 1, total, feature.name);
      continue;
    }
    spdlog::info(" + Patching {:>2} of {} ({})", i + 1, total, feature.name);
    if (feature.frame)
      frame_features.push_back(&feature);
  }
  // Last, so a feature never sees a frame before it is installed. Without the tick nothing polls, and the settings
  // file is only read at startup.
  il2cpp::HookSet hooks("FrameTick");
  IL2CPP_STAGE(hooks, ScreenManager::Class::Method("Update"), ScreenManager_Update_Hook);
  if (!hooks.Commit())
    spdlog::error("the frame tick is not installed: shortcuts, settings reload and frame updates will not run");
}

// --- Startup ---

int il2cpp_init_Hook(auto original, const char* domain_name)
{
#if _WIN32 && !defined(NDEBUG)
  AllocConsole();
  FILE* console;
  freopen_s(&console, "CONOUT$", "w", stdout);
#endif

  // Nothing here may throw into the game: a mod that cannot set itself up leaves the game running without it.
  // Keeping the settings pointer is safe here because only the frame tick reloads, and it is not installed yet.
  const Config* config = nullptr;
  try {
    File::Init();
    StartLogging();
    LogBanner();

    spdlog::info("Loading Configuration...");
    spdlog::info("");
    config = &Config::Get();
    LogSeparator();
  } catch (const std::exception& error) {
    // The file logger may not exist; spdlog falls back to its console logger.
    spdlog::error("setup failed, so no features are installed: {}", error.what());
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
  LogSeparator();

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
    ReportStartupFailure("the executable path does not fit");
    return;
  }
  const auto assembly_path = std::filesystem::path(executable).parent_path() / "../Frameworks/GameAssembly.dylib";
  auto       assembly      = dlopen(assembly_path.c_str(), RTLD_LAZY | RTLD_GLOBAL);
#endif

  if (!assembly) {
    ReportStartupFailure("failed to load GameAssembly");
    return;
  }

  try {
    il2cpp::Initialize(assembly);
#if _WIN32
    auto il2cpp_init = reinterpret_cast<void*>(GetProcAddress(assembly, "il2cpp_init"));
#else
    auto il2cpp_init = dlsym(assembly, "il2cpp_init");
#endif
    il2cpp::HookSet hooks("il2cpp_init");
    IL2CPP_STAGE_AT(hooks, il2cpp_init, il2cpp_init_Hook);
    if (!hooks.Commit())
      ReportStartupFailure("failed to hook il2cpp_init");
  } catch (const std::exception& error) {
    ReportStartupFailure(std::format("failed to hook il2cpp_init: {}", error.what()).c_str());
  }
}

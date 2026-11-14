// The order the mod's keyboard shortcuts are handled in, each frame from the frame tick in mod.cc, and the hotkey
// switches: the mode toggles, restart and quit, Escape, and the shortcuts that change a setting. The other
// shortcuts live with their features: screens.cc, fleet_actions.cc, chat.cc, camera.cc.

#include "config.h"
#include "features/features.h"
#include "shortcuts.h"

#include "game/app.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <spdlog/spdlog.h>
#include <unity/input.h>

#include <chrono>
#include <optional>

#if _WIN32
#include <Windows.h>
#endif

namespace
{
// The frame tick dispatches shortcuts whether or not this feature installed; this says whether it did.
bool hotkeys_installed = false;

// --- Setting shortcuts ---

template <auto Section, auto Setting> void ToggleSetting()
{
  auto& section    = Config::Get().*Section;
  section.*Setting = !(section.*Setting);
}

template <spdlog::level::level_enum Level> void SetLogLevel()
{
  spdlog::set_level(Level);
  spdlog::flush_on(Level);
}

struct SettingShortcut {
  Action shortcut;
  void (*run)();
  // Held shortcuts repeat every frame while the keys are down; the rest fire once per press.
  bool held = false;
};

// Shortcuts that change a setting. The game sees their keys too. The first pressed one runs.
constexpr SettingShortcut kSettingShortcuts[] = {
    {Action::kUiScaleUp, [] { AdjustUiScale(true); }, true},
    {Action::kUiScaleDown, [] { AdjustUiScale(false); }, true},
    {Action::kUiShipScaleUp, [] { AdjustUiShipScale(true); }, true},
    {Action::kUiShipScaleDown, [] { AdjustUiShipScale(false); }, true},
    {Action::kUiViewerScaleUp, [] { AdjustUiViewerScale(true); }, true},
    {Action::kUiViewerScaleDown, [] { AdjustUiViewerScale(false); }, true},
    {Action::kToggleAutoConfirmInstantWarp, CycleInstantWarpConfirmation},
    {Action::kTogglePreviewLocate, ToggleSetting<&Config::ui, &Config::Ui::disable_preview_locate>},
    {Action::kTogglePreviewRecall, ToggleSetting<&Config::ui, &Config::Ui::disable_preview_recall>},
    {Action::kToggleCargoDefault, ToggleSetting<&Config::ui, &Config::Ui::show_cargo_default>},
    {Action::kToggleCargoPlayer, ToggleSetting<&Config::ui, &Config::Ui::show_player_cargo>},
    {Action::kToggleCargoStation, ToggleSetting<&Config::ui, &Config::Ui::show_station_cargo>},
    {Action::kToggleCargoHostile, ToggleSetting<&Config::ui, &Config::Ui::show_hostile_cargo>},
    {Action::kToggleCargoArmada, ToggleSetting<&Config::ui, &Config::Ui::show_armada_cargo>},
    {Action::kLogLevelOff, SetLogLevel<spdlog::level::off>},
    {Action::kLogLevelError, SetLogLevel<spdlog::level::err>},
    {Action::kLogLevelWarn, SetLogLevel<spdlog::level::warn>},
    {Action::kLogLevelInfo, SetLogLevel<spdlog::level::info>},
    {Action::kLogLevelDebug, SetLogLevel<spdlog::level::debug>},
    {Action::kLogLevelTrace, SetLogLevel<spdlog::level::trace>},
    {Action::kReloadSettings, [] { Config::Reload(); }},
};

void SettingShortcuts()
{
  for (const auto& setting : kSettingShortcuts) {
    if (setting.held ? Shortcut::IsPressed(setting.shortcut) : Shortcut::IsDown(setting.shortcut)) {
      setting.run();
      return;
    }
  }
}

// --- Escape exit ---

// With disable_escape_exit, Escape no longer reaches the game's exit prompt. disable_escape_exit_timer > 0 lets
// a double tap within that many milliseconds through. Returns true when Escape is swallowed.
bool SwallowEscape()
{
  const auto& config = Config::Get();
  if (!config.ui.disable_escape_exit || !unity::Key::Pressed(unity::KeyCode::Escape))
    return false;
  // A held key stays swallowed; only distinct presses count towards a double tap.
  if (config.ui.disable_escape_exit_timer <= 0 || !unity::Key::Down(unity::KeyCode::Escape))
    return true;

  static std::optional<std::chrono::steady_clock::time_point> first_press;
  const auto                                                  now = std::chrono::steady_clock::now();
  if (!first_press || now - *first_press > std::chrono::milliseconds(config.ui.disable_escape_exit_timer)) {
    first_press = now;
    return true;
  }
  // Consume the pair so a quick third press starts a new one.
  first_press.reset();
  return false;
}

// --- Scopely hotkeys ---

// With Scopely hotkeys off, the game never registers its own input actions, so its shortcuts cannot fire.
void ShortcutsManager_InitializeActions_Hook(auto original, ShortcutsManager* _this)
{
  if (Config::Get().control.use_scopely_hotkeys)
    original(_this);
}

bool InstallHotkeys()
{
  il2cpp::HookSet hooks("Hotkeys");
  IL2CPP_STAGE(hooks, ShortcutsManager::Class::Method("InitializeActions"), ShortcutsManager_InitializeActions_Hook);
  // The badges only make sense when the game's own input actions are under the mod's control.
  if (hooks.Commit())
    InstallKeybindBadges();
  InstallFleetActions();
  // The dispatch itself needs no hook: the frame tick runs it. The hint badges and pre-scan cargo are extras whose
  // own sets report themselves.
  hotkeys_installed = true;
  return true;
}
} // namespace

bool HandleShortcuts()
{
  if (!hotkeys_installed)
    return false;
  auto& config = Config::Get();

  if (Shortcut::IsDown(Action::kDisableHotKeys)) {
    HideKeybindBadges();
    config.control.hotkeys_enabled = false;
    spdlog::warn("[Hotkeys] mod hotkeys off");
    return true;
  }
  if (Shortcut::IsDown(Action::kEnableHotKeys)) {
    config.control.hotkeys_enabled = true;
    spdlog::warn("[Hotkeys] mod hotkeys on");
    return true;
  }

  if (PressAwaitedButton())
    return true;

  // Switched off, or with Scopely's (the game's own) hotkeys, the mod leaves the keys to the game. Returning true
  // here would skip the game's ScreenManager.Update, which also runs the back button, orientation and the rest of
  // the UI's per-frame work.
  if (!config.control.hotkeys_enabled || config.control.use_scopely_hotkeys)
    return false;

  // Keys typed into a text field (chat, search) are text, not shortcuts.
  const bool typing = unity::Key::IsInputFocused();
  if (!typing && Shortcut::IsDown(Action::kToggleShortcutHints) && ToggleKeybindBadges())
    return true;

  if (!typing && Shortcut::IsDown(Action::kRestart)) {
    spdlog::info("[Hotkeys] clearing the localisation cache and restarting");
    LanguageManager::ClearCache();
    if (auto* app = Hub::App())
      app->Reload();
    return true;
  }
  if (!typing && Shortcut::IsDown(Action::kQuit)) {
    // On Windows the process ends here: TerminateProcess does not return.
#if _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#else
    if (auto* app = Hub::App())
      app->Quit();
#endif
    return true;
  }

  if (SelectShipShortcuts())
    return true;

  // The game only clears a focused text field from its back button; Escape should too.
  const bool in_chat = Hub::IsInChat();
  if (unity::Key::Pressed(unity::KeyCode::Escape) && (typing || in_chat)) {
    unity::Key::ClearInputFocus();
    return true;
  }

  if (in_chat) {
    if (ChatChannelShortcuts())
      return true;
  } else if (!typing) {
    if (FleetShortcuts())
      return true;
    OpenChatShortcut();
    if (ScreenShortcuts())
      return true;
    SettingShortcuts();
  }

  return FleetActionShortcuts() || SwallowEscape();
}

const Feature kHotkeys{"Hotkeys", &Config::Patches::hotkeyhooks, InstallHotkeys};

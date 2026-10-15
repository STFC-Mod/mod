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

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;

bool hotkeys_installed = false;

template <auto Section, auto Setting> void Toggle()
{
  auto& section    = Config::Get().*Section;
  section.*Setting = !(section.*Setting);
}

template <spdlog::level::level_enum Level> void SetLogLevel()
{
  spdlog::set_level(Level);
  spdlog::flush_on(Level);
}

template <void (*Adjust)(bool), bool Up> void Scale()
{ Adjust(Up); }

void ReloadSettings()
{ Config::Reload(); }

struct SettingShortcut {
  Action shortcut;
  void (*run)();
  // Held shortcuts repeat every frame while the keys are down; the rest fire once per press.
  bool held = false;
};

// Shortcuts that change a setting. The game sees their keys too. The first pressed one runs.
constexpr SettingShortcut kSettings[] = {
    {Action::kUiScaleUp, Scale<AdjustUiScale, true>, true},
    {Action::kUiScaleDown, Scale<AdjustUiScale, false>, true},
    {Action::kUiShipScaleUp, Scale<AdjustUiShipScale, true>, true},
    {Action::kUiShipScaleDown, Scale<AdjustUiShipScale, false>, true},
    {Action::kUiViewerScaleUp, Scale<AdjustUiViewerScale, true>, true},
    {Action::kUiViewerScaleDown, Scale<AdjustUiViewerScale, false>, true},
    {Action::kToggleAutoConfirmInstantWarp, CycleInstantWarpConfirmation},
    {Action::kTogglePreviewLocate, Toggle<&Config::ui, &Config::Ui::disable_preview_locate>},
    {Action::kTogglePreviewRecall, Toggle<&Config::ui, &Config::Ui::disable_preview_recall>},
    {Action::kToggleCargoDefault, Toggle<&Config::ui, &Config::Ui::show_cargo_default>},
    {Action::kToggleCargoPlayer, Toggle<&Config::ui, &Config::Ui::show_player_cargo>},
    {Action::kToggleCargoStation, Toggle<&Config::ui, &Config::Ui::show_station_cargo>},
    {Action::kToggleCargoHostile, Toggle<&Config::ui, &Config::Ui::show_hostile_cargo>},
    {Action::kToggleCargoArmada, Toggle<&Config::ui, &Config::Ui::show_armada_cargo>},
    {Action::kLogLevelOff, SetLogLevel<spdlog::level::off>},
    {Action::kLogLevelError, SetLogLevel<spdlog::level::err>},
    {Action::kLogLevelWarn, SetLogLevel<spdlog::level::warn>},
    {Action::kLogLevelInfo, SetLogLevel<spdlog::level::info>},
    {Action::kLogLevelDebug, SetLogLevel<spdlog::level::debug>},
    {Action::kLogLevelTrace, SetLogLevel<spdlog::level::trace>},
    {Action::kReloadSettings, ReloadSettings},
};

void SettingShortcuts()
{
  for (const auto& setting : kSettings) {
    if (setting.held ? Shortcut::IsPressed(setting.shortcut) : Shortcut::IsDown(setting.shortcut)) {
      setting.run();
      return;
    }
  }
}

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

  static Clock::time_point first_press{};
  const auto               now = Clock::now();
  if (first_press == Clock::time_point{}
      || now - first_press > std::chrono::milliseconds(config.ui.disable_escape_exit_timer)) {
    first_press = now;
    return true;
  }
  // Consume the pair so a quick third press starts a new one.
  first_press = {};
  return false;
}

// The shortcut dispatch for one frame. Returns true when the game's Update must not run, so it does not also
// handle the key.
bool DispatchShortcuts()
{
  auto& config = Config::Get();

  if (Shortcut::IsDown(Action::kDisableHotKeys)) {
    HideKeybindBadges();
    config.control.hotkeys_enabled = false;
    spdlog::warn("Setting hotkeys to DISABLED");
    return true;
  }
  if (Shortcut::IsDown(Action::kEnableHotKeys)) {
    config.control.hotkeys_enabled = true;
    spdlog::warn("Setting hotkeys to ENABLED");
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
    spdlog::info("Clearing localisation cache and restarting");
    LanguageManager::ClearCache();
    Hub::App()->Reload();
    return true;
  }
  if (!typing && Shortcut::IsDown(Action::kQuit)) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#else
    Hub::App()->Quit();
    return true;
#endif
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

// With Scopely hotkeys off, the game never registers its own input actions, so its shortcuts cannot fire.
void ShortcutsManager_InitializeActions_Hook(auto original, ShortcutsManager* _this)
{
  if (Config::Get().control.use_scopely_hotkeys)
    original(_this);
}

void Install()
{
  // The badges only make sense when the game's own input actions are under the mod's control.
  if (IL2CPP_HOOK(ShortcutsManager::Class::Method("InitializeActions"), ShortcutsManager_InitializeActions_Hook))
    InstallKeybindBadges();
  InstallFleetActions();
  hotkeys_installed = true;
}
} // namespace

bool HandleShortcuts()
{ return hotkeys_installed && DispatchShortcuts(); }

const Feature kHotkeys{"Hotkeys", &Config::Patches::hotkeyhooks, Install};

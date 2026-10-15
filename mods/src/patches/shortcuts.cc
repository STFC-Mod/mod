#include "patches/shortcuts.h"

#include "patches/input.h"
#include "str_utils.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <optional>

namespace
{
// A key name from the settings file, and its keybind badge text when that differs from the name. The badge font
// has few glyphs, so long names are shortened and the symbols the modifier notation uses are spelled out.
struct KeyName {
  std::string_view name;
  KeyCode          key;
  std::string_view hint = {};
};

constexpr KeyName kKeys[] = {
    {"END", KeyCode::End},
    {"HOME", KeyCode::Home, "HM"},
    {"PGDOWN", KeyCode::PageDown, "PD"},
    {"PGUP", KeyCode::PageUp, "PU"},
    {"DOWN", KeyCode::DownArrow, "DN"},
    {"LEFT", KeyCode::LeftArrow, "LT"},
    {"RIGHT", KeyCode::RightArrow, "RT"},
    {"UP", KeyCode::UpArrow},
    {"BACKSPACE", KeyCode::Backspace, "BS"},
    {"BREAK", KeyCode::Break},
    {"CAPS", KeyCode::CapsLock},
    {"CLEAR", KeyCode::Clear},
    {"DELETE", KeyCode::Delete, "DEL"},
    {"ESCAPE", KeyCode::Escape, "ESC"},
    {"HELP", KeyCode::Help},
    {"INSERT", KeyCode::Insert},
    {"MENU", KeyCode::Menu},
    {"PAUSE", KeyCode::Pause},
    {"PRINT", KeyCode::Print},
    {"RETURN", KeyCode::Return, "ENT"},
    {"SCROLL", KeyCode::ScrollLock},
    {"SYSREQ", KeyCode::SysReq},
    {"TAB", KeyCode::Tab},
    {"MOUSE0", KeyCode::Mouse0, "M0"},
    {"MOUSE1", KeyCode::Mouse1, "M1"},
    {"MOUSE2", KeyCode::Mouse2, "M2"},
    {"MOUSE3", KeyCode::Mouse3, "M3"},
    {"MOUSE4", KeyCode::Mouse4, "M4"},
    {"MOUSE5", KeyCode::Mouse5, "M5"},
    {"MOUSE6", KeyCode::Mouse6, "M6"},
    {"SPACE", KeyCode::Space, "SPC"},
    // Chords are split on '-', so the minus key needs a name.
    {"MINUS", KeyCode::Minus, "-"},
    {"_", KeyCode::Underscore},
    {",", KeyCode::Comma},
    {";", KeyCode::Semicolon},
    {":", KeyCode::Colon},
    {"!", KeyCode::Exclaim, "EXC"},
    {"?", KeyCode::Question},
    {".", KeyCode::Period},
    {"'", KeyCode::Quote},
    {"(", KeyCode::LeftParen},
    {")", KeyCode::RightParen},
    {"[", KeyCode::LeftBracket},
    {"]", KeyCode::RightBracket},
    {"{", KeyCode::LeftCurlyBracket},
    {"}", KeyCode::RightCurlyBracket},
    {"@", KeyCode::At},
    {"*", KeyCode::Asterisk},
    {"/", KeyCode::Slash},
    {"\"", KeyCode::DoubleQuote},
    {"\\", KeyCode::Backslash},
    {"&", KeyCode::Ampersand},
    {"#", KeyCode::Hash, "HSH"},
    {"%", KeyCode::Percent},
    {"`", KeyCode::BackQuote},
    {"^", KeyCode::Caret, "CAR"},
    {"+", KeyCode::Plus, "PLS"},
    {"<", KeyCode::Less},
    {"=", KeyCode::Equals},
    {">", KeyCode::Greater},
    {"~", KeyCode::Tilde},
    {"$", KeyCode::Dollar},
    {"0", KeyCode::Alpha0},
    {"1", KeyCode::Alpha1},
    {"2", KeyCode::Alpha2},
    {"3", KeyCode::Alpha3},
    {"4", KeyCode::Alpha4},
    {"5", KeyCode::Alpha5},
    {"6", KeyCode::Alpha6},
    {"7", KeyCode::Alpha7},
    {"8", KeyCode::Alpha8},
    {"9", KeyCode::Alpha9},
    {"A", KeyCode::A},
    {"B", KeyCode::B},
    {"C", KeyCode::C},
    {"D", KeyCode::D},
    {"E", KeyCode::E},
    {"F", KeyCode::F},
    {"G", KeyCode::G},
    {"H", KeyCode::H},
    {"I", KeyCode::I},
    {"J", KeyCode::J},
    {"K", KeyCode::K},
    {"L", KeyCode::L},
    {"M", KeyCode::M},
    {"N", KeyCode::N},
    {"O", KeyCode::O},
    {"P", KeyCode::P},
    {"Q", KeyCode::Q},
    {"R", KeyCode::R},
    {"S", KeyCode::S},
    {"T", KeyCode::T},
    {"U", KeyCode::U},
    {"V", KeyCode::V},
    {"W", KeyCode::W},
    {"X", KeyCode::X},
    {"Y", KeyCode::Y},
    {"Z", KeyCode::Z},
    {"F1", KeyCode::F1},
    {"F2", KeyCode::F2},
    {"F3", KeyCode::F3},
    {"F4", KeyCode::F4},
    {"F5", KeyCode::F5},
    {"F6", KeyCode::F6},
    {"F7", KeyCode::F7},
    {"F8", KeyCode::F8},
    {"F9", KeyCode::F9},
    {"F10", KeyCode::F10},
    {"F11", KeyCode::F11},
    {"F12", KeyCode::F12},
    {"F13", KeyCode::F13},
    {"F14", KeyCode::F14},
    {"F15", KeyCode::F15},
    {"NUMLOCK", KeyCode::Numlock},
    {"KEY0", KeyCode::Keypad0},
    {"KEY1", KeyCode::Keypad1},
    {"KEY2", KeyCode::Keypad2},
    {"KEY3", KeyCode::Keypad3},
    {"KEY4", KeyCode::Keypad4},
    {"KEY5", KeyCode::Keypad5},
    {"KEY6", KeyCode::Keypad6},
    {"KEY7", KeyCode::Keypad7},
    {"KEY8", KeyCode::Keypad8},
    {"KEY9", KeyCode::Keypad9},
    {"KEYDIVIDE", KeyCode::KeypadDivide},
    {"KEYENTER", KeyCode::KeypadEnter},
    {"KEYEQUAL", KeyCode::KeypadEquals},
    {"KEYMINUS", KeyCode::KeypadMinus},
    {"KEYMULTI", KeyCode::KeypadMultiply},
    {"KEYPERIOD", KeyCode::KeypadPeriod},
    {"KEYPLUS", KeyCode::KeypadPlus},
};

// Modifier names. The unsided ones match either side of the keyboard. Badges use AutoHotkey's ASCII notation.
struct ModifierName {
  std::string_view       name;
  std::array<KeyCode, 2> keys;
  std::string_view       hint;
};

constexpr ModifierName kModifiers[] = {
    {"SHIFT", {KeyCode::LeftShift, KeyCode::RightShift}, "+"},
    {"CTRL", {KeyCode::LeftControl, KeyCode::RightControl}, "^"},
    {"ALT", {KeyCode::LeftAlt, KeyCode::RightAlt}, "!"},
    {"APPLE", {KeyCode::LeftApple, KeyCode::RightApple}, "#"},
    {"CMD", {KeyCode::LeftCommand, KeyCode::RightCommand}, "#"},
    {"WIN", {KeyCode::LeftWindows, KeyCode::RightWindows}, "#"},
    {"LSHIFT", {KeyCode::LeftShift, KeyCode::LeftShift}, "<+"},
    {"RSHIFT", {KeyCode::RightShift, KeyCode::RightShift}, ">+"},
    {"LCTRL", {KeyCode::LeftControl, KeyCode::LeftControl}, "<^"},
    {"RCTRL", {KeyCode::RightControl, KeyCode::RightControl}, ">^"},
    {"LALT", {KeyCode::LeftAlt, KeyCode::LeftAlt}, "<!"},
    {"RALT", {KeyCode::RightAlt, KeyCode::RightAlt}, ">!"},
    {"ALTGR", {KeyCode::AltGr, KeyCode::AltGr}, "!"},
    {"LAPPLE", {KeyCode::LeftApple, KeyCode::LeftApple}, "<#"},
    {"LCOM", {KeyCode::LeftCommand, KeyCode::LeftCommand}, "<#"},
    {"LWIN", {KeyCode::LeftWindows, KeyCode::LeftWindows}, "<#"},
    {"RAPPLE", {KeyCode::RightApple, KeyCode::RightApple}, ">#"},
    {"RCOM", {KeyCode::RightCommand, KeyCode::RightCommand}, ">#"},
    {"RWIN", {KeyCode::RightWindows, KeyCode::RightWindows}, ">#"},
};

// Settings are only parsed at load, so the tables are searched in order.
const KeyName* FindKey(std::string_view name)
{
  const auto it = std::ranges::find(kKeys, name, &KeyName::name);
  return it == std::end(kKeys) ? nullptr : it;
}

const ModifierName* FindModifier(std::string_view name)
{
  const auto it = std::ranges::find(kModifiers, name, &ModifierName::name);
  return it == std::end(kModifiers) ? nullptr : it;
}

std::array<std::vector<KeyChord>, GameFunction::Max> bindings;

// Keys that completed a binding with modifiers, until released. A plain binding on the same key waits for the
// release, so letting go of CTRL before PGUP does not turn a held CTRL-PGUP into PGUP.
std::vector<KeyCode> used_keys;

bool IsUsed(KeyCode key)
{
  std::erase_if(used_keys, [](KeyCode used) { return !Key::Pressed(used); });
  return std::ranges::find(used_keys, key) != used_keys.end();
}

bool ModifiersHeld(const KeyChord& chord)
{
  return std::ranges::all_of(chord.modifiers,
                             [](const auto& group) { return std::ranges::any_of(group, Key::Pressed); });
}

template <bool (*KeyState)(KeyCode)> bool Matches(GameFunction action)
{
  for (const auto& chord : bindings[action]) {
    if (chord.key == KeyCode::None || !KeyState(chord.key))
      continue;
    if (chord.modifiers.empty() ? Key::IsModified() || IsUsed(chord.key) : !ModifiersHeld(chord))
      continue;
    if (!chord.modifiers.empty() && std::ranges::find(used_keys, chord.key) == used_keys.end())
      used_keys.push_back(chord.key);
    return true;
  }
  return false;
}

enum class Tier { kAlways, kExtended, kExperimental };

struct Setting {
  std::string_view                key;
  GameFunction                    action;
  std::string_view                fallback;
  Tier                            tier    = Tier::kAlways;
  std::array<std::string_view, 2> aliases = {};
};

// Order is the order of the debug log; the TOML output is sorted by key anyway.
constexpr Setting kSettings[] = {
    {"move_up", GameFunction::MoveUp, "W|UP"},
    {"move_down", GameFunction::MoveDown, "S|DOWN"},
    {"move_left", GameFunction::MoveLeft, "A|LEFT"},
    {"move_right", GameFunction::MoveRight, "D|RIGHT"},
    {"set_hotkeys_disabled",
     GameFunction::DisableHotKeys,
     "CTRL-ALT-MINUS",
     Tier::kAlways,
     {"set_hotkeys_disble", "set_hotkeys_disable"}},
    {"set_hotkeys_enabled", GameFunction::EnableHotKeys, "CTRL-ALT-=", Tier::kAlways, {"set_hotkeys_enable"}},
    {"select_chatalliance", GameFunction::SelectChatAlliance, "CTRL-2"},
    {"select_chatglobal", GameFunction::SelectChatGlobal, "CTRL-1"},
    {"select_chatprivate", GameFunction::SelectChatPrivate, "CTRL-3"},
    {"quit", GameFunction::Quit, "F10"},
    {"select_ship1", GameFunction::SelectShip1, "1"},
    {"select_ship2", GameFunction::SelectShip2, "2"},
    {"select_ship3", GameFunction::SelectShip3, "3"},
    {"select_ship4", GameFunction::SelectShip4, "4"},
    {"select_ship5", GameFunction::SelectShip5, "5"},
    {"select_ship6", GameFunction::SelectShip6, "6"},
    {"select_ship7", GameFunction::SelectShip7, "7"},
    {"select_ship8", GameFunction::SelectShip8, "8"},
    {"select_current", GameFunction::SelectCurrent, "CTRL-SPACE"},
    {"focus_search", GameFunction::FocusSearch, "CTRL-F"},
    {"action_primary", GameFunction::ActionPrimary, "SPACE|MOUSE1"},
    {"action_secondary", GameFunction::ActionSecondary, "TAB|MOUSE4"},
    {"action_queue", GameFunction::ActionQueue, "SPACE|MOUSE1"},
    {"action_queue_clear", GameFunction::ActionQueueClear, "CTRL-C"},
    {"action_view", GameFunction::ActionView, "V|MOUSE2"},
    {"action_recall", GameFunction::ActionRecall, "R|MOUSE3"},
    {"action_recall_cancel", GameFunction::ActionRecallCancel, "SPACE|MOUSE1"},
    {"action_repair", GameFunction::ActionRepair, "R|MOUSE3"},
    {"show_chat", GameFunction::ShowChat, "C"},
    {"show_chatside1", GameFunction::ShowChatSide1, "ALT-C"},
    {"show_chatside2", GameFunction::ShowChatSide2, "`"},
    {"show_galaxy", GameFunction::ShowGalaxy, "G"},
    {"show_galaxy_native", GameFunction::NativeShortcutGalaxy, "CTRL-G", Tier::kAlways, {"native_shortcut_galaxy"}},
    {"show_system", GameFunction::ShowSystem, "H"},
    {"zoom_preset1", GameFunction::ZoomPreset1, "F1"},
    {"zoom_preset2", GameFunction::ZoomPreset2, "F2"},
    {"zoom_preset3", GameFunction::ZoomPreset3, "F3"},
    {"zoom_preset4", GameFunction::ZoomPreset4, "F4"},
    {"zoom_preset5", GameFunction::ZoomPreset5, "F5"},
    {"zoom_in", GameFunction::ZoomIn, "Q"},
    {"zoom_out", GameFunction::ZoomOut, "E"},
    {"zoom_max", GameFunction::ZoomMax, "MINUS"},
    {"zoom_min", GameFunction::ZoomMin, "BACKSPACE"},
    {"zoom_reset", GameFunction::ZoomReset, "="},
    {"ui_scaleup", GameFunction::UiScaleUp, "PGUP"},
    {"ui_scaledown", GameFunction::UiScaleDown, "PGDOWN"},
    {"ui_scaleshipup", GameFunction::UiShipScaleUp, "CTRL-PGUP"},
    {"ui_scaleshipdown", GameFunction::UiShipScaleDown, "CTRL-PGDOWN"},
    {"ui_scaleviewerup", GameFunction::UiViewerScaleUp, "SHIFT-PGUP"},
    {"ui_scaleviewerdown", GameFunction::UiViewerScaleDown, "SHIFT-PGDOWN"},
    {"log_debug", GameFunction::LogLevelDebug, "CTRL-SHIFT-F9"},
    {"log_trace", GameFunction::LogLevelTrace, "CTRL-SHIFT-F7"},
    {"log_info", GameFunction::LogLevelInfo, "CTRL-SHIFT-F8"},
    {"log_warn", GameFunction::LogLevelWarn, "CTRL-SHIFT-F10"},
    {"log_error", GameFunction::LogLevelError, "CTRL-SHIFT-F11"},
    {"log_off", GameFunction::LogLevelOff, "CTRL-SHIFT-F12"},
    {"restart", GameFunction::Restart, "F9"},
    {"reload_settings", GameFunction::ReloadSettings, "CTRL-SHIFT-F6"},
    {"show_awayteam", GameFunction::ShowAwayTeam, "T"},
    {"show_gifts", GameFunction::ShowGifts, "/"},
    {"show_artifacts", GameFunction::ShowArtifacts, "SHIFT-I"},
    {"show_commander", GameFunction::ShowCommander, "O"},
    {"show_daily", GameFunction::ShowDaily, "Z"},
    {"show_events", GameFunction::ShowEvents, "SHIFT-E"},
    {"show_events_native", GameFunction::NativeShortcutEvents, "CTRL-E", Tier::kAlways, {"native_shortcut_events"}},
    {"show_exocomp", GameFunction::ShowExoComp, "X"},
    {"show_factions", GameFunction::ShowFactions, "F"},
    {"show_inventory", GameFunction::ShowInventory, "I"},
    {"show_missions", GameFunction::ShowMissions, "M"},
    {"show_research", GameFunction::ShowResearch, "U"},
    {"show_scrapyard", GameFunction::ShowScrapYard, "Y"},
    {"show_settings", GameFunction::ShowSettings, "SHIFT-S"},
    {"toggle_shortcut_hints", GameFunction::ToggleShortcutHints, "HOME"},
    {"show_officers", GameFunction::ShowOfficers, "SHIFT-O"},
    {"show_officerpresets", GameFunction::ShowOfficerPresets, "CTRL-O"},
    {"show_officerswap", GameFunction::ShowOfficerSwap, "ALT-O"},
    {"show_qtrials", GameFunction::ShowQTrials, "SHIFT-Q"},
    {"show_refinery", GameFunction::ShowRefinery, "SHIFT-F"},
    {"show_ships", GameFunction::ShowShips, "N"},
    {"show_shipconstruction", GameFunction::ShowShipConstruction, "SHIFT-N"},
    {"show_shipswap", GameFunction::ShowShipSwap, "ALT-N"},
    {"show_shields", GameFunction::ShowShields, "CTRL-S"},
    {"show_battlelogs", GameFunction::ShowBattlelogs, "SHIFT-B"},
    {"show_stationexterior", GameFunction::ShowStationExterior, "SHIFT-G"},
    {"show_stationinterior", GameFunction::ShowStationInterior, "SHIFT-H"},
    {"show_haven", GameFunction::ShowHaven, "ALT-H"},
    {"toggle_queue", GameFunction::ToggleQueue, "CTRL-Q"},
    {"toggle_instant_warp", GameFunction::ToggleAutoConfirmInstantWarp, "ALT-I"},
    {"show_alliance", GameFunction::ShowAlliance, "ALT-'", Tier::kExtended},
    {"show_alliance_help", GameFunction::ShowAllianceHelp, "SHIFT-'", Tier::kExperimental},
    {"show_alliance_armada", GameFunction::ShowAllianceArmada, "CTRL-'", Tier::kExperimental},
    {"show_bookmarks", GameFunction::ShowBookmarks, "B", Tier::kExtended},
    {"show_lookup", GameFunction::ShowLookup, "L", Tier::kExperimental},
    {"set_zoom_preset1", GameFunction::SetZoomPreset1, "SHIFT-F1", Tier::kExtended},
    {"set_zoom_preset2", GameFunction::SetZoomPreset2, "SHIFT-F2", Tier::kExtended},
    {"set_zoom_preset3", GameFunction::SetZoomPreset3, "SHIFT-F3", Tier::kExtended},
    {"set_zoom_preset4", GameFunction::SetZoomPreset4, "SHIFT-F4", Tier::kExtended},
    {"set_zoom_preset5", GameFunction::SetZoomPreset5, "SHIFT-F5", Tier::kExtended},
    {"set_zoom_default", GameFunction::SetZoomDefault, "CTRL-=", Tier::kExtended},
    {"toggle_preview_locate", GameFunction::TogglePreviewLocate, "CTRL-R", Tier::kExtended},
    {"toggle_preview_recall", GameFunction::TogglePreviewRecall, "CTRL-T", Tier::kExtended},
    {"toggle_cargo_default", GameFunction::ToggleCargoDefault, "ALT-1", Tier::kExtended},
    {"toggle_cargo_player", GameFunction::ToggleCargoPlayer, "ALT-2", Tier::kExtended},
    {"toggle_cargo_station", GameFunction::ToggleCargoStation, "ALT-3", Tier::kExtended},
    {"toggle_cargo_hostile", GameFunction::ToggleCargoHostile, "ALT-4", Tier::kExtended},
    {"toggle_cargo_armada", GameFunction::ToggleCargoArmada, "ALT-5", Tier::kExtended},
};

// Binds every chord in `value` ("CTRL-G|F8"). Reports whether any was valid.
bool BindChords(const Setting& setting, std::string_view key, std::string_view value)
{
  bool bound = false;
  for (const auto& text : StrSplit(value, '|')) {
    if (auto chord = KeyChord::Parse(text); chord.key != KeyCode::None) {
      Shortcut::Bind(setting.action, std::move(chord));
      bound = true;
    } else {
      spdlog::warn("Invalid shortcut token [shortcuts].{} token='{}' value='{}'; ignoring token.", key, text, value);
    }
  }
  return bound;
}

// Binds one [shortcuts] setting and records the effective binding, and where it came from, in `out`.
void LoadSetting(const toml::table* shortcuts, toml::table& out, toml::table& out_source, const Setting& setting)
{
  // The setting itself, else the first deprecated alias present, else the default.
  std::string_view  key  = setting.key;
  const toml::node* node = shortcuts ? shortcuts->get(key) : nullptr;
  for (const auto alias : setting.aliases) {
    if (alias.empty() || !shortcuts || !shortcuts->contains(alias))
      continue;
    if (node) {
      spdlog::warn("Ignoring deprecated shortcut alias [shortcuts].{} because [shortcuts].{} is set.", alias,
                   setting.key);
      continue;
    }
    spdlog::warn("Deprecated shortcut alias [shortcuts].{} is supported for compatibility; use [shortcuts].{}.", alias,
                 setting.key);
    key  = alias;
    node = shortcuts->get(alias);
    break;
  }
  std::optional<std::string> configured = node ? node->value<std::string>() : std::nullopt;
  if (node && !configured)
    spdlog::error("Invalid shortcut value [shortcuts].{} must be a string; using default for [shortcuts].{}.", key,
                  setting.key);
  std::string label = !configured ? "default" : key == setting.key ? "config" : "alias:" + std::string(key);

  const auto record = [&](std::string value, std::string source) {
    spdlog::debug("shortcut value shortcuts.{} value: {}", setting.key, value);
    out.insert_or_assign(setting.key, std::move(value));
    out_source.insert_or_assign(setting.key, std::move(source));
  };

  // An explicit empty string disables the binding, just like NONE.
  const auto value = AsciiStrToUpper(StripAsciiWhitespace(configured.value_or(std::string(setting.fallback))));
  if (value.empty() || value == "NONE")
    return record("NONE", label);

  if (!BindChords(setting, key, value)) {
    if (configured)
      spdlog::error("No valid shortcut tokens for [shortcuts].{} value='{}'; using default for [shortcuts].{}.", key,
                    *configured, setting.key);
    if (!configured || !BindChords(setting, setting.key, setting.fallback)) {
      spdlog::error("Default shortcut [shortcuts].{} value='{}' has no valid tokens; action disabled.", setting.key,
                    setting.fallback);
      return record("NONE", "invalid");
    }
    label = "default";
  }
  record(Shortcut::Describe(setting.action), label);
}
} // namespace

KeyChord KeyChord::Parse(std::string_view text)
{
  KeyChord    chord;
  std::string key_token;
  for (auto& token : StrSplit(AsciiStrToUpper(text), '-')) {
    if (const auto* modifier = FindModifier(token)) {
      chord.modifiers.push_back(modifier->keys);
      chord.tokens.push_back(std::move(token));
    } else if (const auto* key = FindKey(token)) {
      // A chord has one key; a later one replaces an earlier one, so the text shows what is bound.
      chord.key = key->key;
      key_token = std::move(token);
    }
  }
  if (chord.key != KeyCode::None)
    chord.tokens.push_back(std::move(key_token));
  return chord;
}

std::string KeyChord::Text() const
{
  std::string text;
  for (const auto& token : tokens) {
    if (!text.empty())
      text += '-';
    text += token;
  }
  return text;
}

std::string KeyChord::Hint() const
{
  std::string hint;
  for (size_t i = 0; i < tokens.size(); ++i) {
    // The key is always last; everything before it is a modifier.
    const auto* key      = i + 1 == tokens.size() && this->key != KeyCode::None ? FindKey(tokens[i]) : nullptr;
    const auto* modifier = key ? nullptr : FindModifier(tokens[i]);
    hint += key && !key->hint.empty() ? key->hint : modifier ? modifier->hint : std::string_view(tokens[i]);
  }
  return hint;
}

namespace Shortcut
{
void Load(const toml::table& config, toml::table& parsed, bool extended, bool experimental)
{
  // A reload replaces every binding.
  Clear();
  const auto* shortcuts = config["shortcuts"].as_table();
  auto&       out       = *parsed.emplace<toml::table>("shortcuts").first->second.as_table();
  auto&       source    = *parsed.emplace<toml::table>("shortcuts_source").first->second.as_table();

  for (const auto& setting : kSettings) {
    if ((setting.tier == Tier::kExtended && !extended)
        || (setting.tier == Tier::kExperimental && !(extended && experimental)))
      continue;
    LoadSetting(shortcuts, out, source, setting);
  }
}

void Bind(GameFunction action, KeyChord chord)
{ bindings[action].push_back(std::move(chord)); }

void Clear()
{
  for (auto& chords : bindings)
    chords.clear();
  used_keys.clear();
}

bool Owns(KeyCode key)
{ return IsUsed(key); }

bool HasBinding(GameFunction action)
{
  return std::ranges::any_of(bindings[action], [](const auto& chord) { return chord.key != KeyCode::None; });
}

bool IsPressed(GameFunction action)
{ return Matches<Key::Pressed>(action); }

bool IsDown(GameFunction action)
{ return Matches<Key::Down>(action); }

std::string Describe(GameFunction action)
{
  std::string text;
  for (const auto& chord : bindings[action]) {
    if (!text.empty())
      text += " | ";
    text += chord.Text();
  }
  return text;
}

std::string Hint(GameFunction action)
{
  // The game's badges have room for one binding.
  return bindings[action].empty() ? std::string{} : bindings[action].front().Hint();
}
} // namespace Shortcut

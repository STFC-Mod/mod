#include "patches/shortcuts.h"

#include "str_utils.h"

#include <spdlog/spdlog.h>
#include <unity/input.h>

#include <algorithm>
#include <array>
#include <optional>

namespace
{
// A key name from the settings file, and its keybind badge text when that differs from the name. The badge font
// has few glyphs, so long names are shortened and the symbols the modifier notation uses are spelled out.
struct KeyName {
  std::string_view name;
  unity::KeyCode   key;
  std::string_view hint = {};
};

constexpr KeyName kKeys[] = {
    {"END", unity::KeyCode::End},
    {"HOME", unity::KeyCode::Home, "HM"},
    {"PGDOWN", unity::KeyCode::PageDown, "PD"},
    {"PGUP", unity::KeyCode::PageUp, "PU"},
    {"DOWN", unity::KeyCode::DownArrow, "DN"},
    {"LEFT", unity::KeyCode::LeftArrow, "LT"},
    {"RIGHT", unity::KeyCode::RightArrow, "RT"},
    {"UP", unity::KeyCode::UpArrow},
    {"BACKSPACE", unity::KeyCode::Backspace, "BS"},
    {"BREAK", unity::KeyCode::Break},
    {"CAPS", unity::KeyCode::CapsLock},
    {"CLEAR", unity::KeyCode::Clear},
    {"DELETE", unity::KeyCode::Delete, "DEL"},
    {"ESCAPE", unity::KeyCode::Escape, "ESC"},
    {"HELP", unity::KeyCode::Help},
    {"INSERT", unity::KeyCode::Insert},
    {"MENU", unity::KeyCode::Menu},
    {"PAUSE", unity::KeyCode::Pause},
    {"PRINT", unity::KeyCode::Print},
    {"RETURN", unity::KeyCode::Return, "ENT"},
    {"SCROLL", unity::KeyCode::ScrollLock},
    {"SYSREQ", unity::KeyCode::SysReq},
    {"TAB", unity::KeyCode::Tab},
    {"MOUSE0", unity::KeyCode::Mouse0, "M0"},
    {"MOUSE1", unity::KeyCode::Mouse1, "M1"},
    {"MOUSE2", unity::KeyCode::Mouse2, "M2"},
    {"MOUSE3", unity::KeyCode::Mouse3, "M3"},
    {"MOUSE4", unity::KeyCode::Mouse4, "M4"},
    {"MOUSE5", unity::KeyCode::Mouse5, "M5"},
    {"MOUSE6", unity::KeyCode::Mouse6, "M6"},
    {"SPACE", unity::KeyCode::Space, "SPC"},
    // Chords are split on '-', so the minus key needs a name.
    {"MINUS", unity::KeyCode::Minus, "-"},
    {"_", unity::KeyCode::Underscore},
    {",", unity::KeyCode::Comma},
    {";", unity::KeyCode::Semicolon},
    {":", unity::KeyCode::Colon},
    {"!", unity::KeyCode::Exclaim, "EXC"},
    {"?", unity::KeyCode::Question},
    {".", unity::KeyCode::Period},
    {"'", unity::KeyCode::Quote},
    {"(", unity::KeyCode::LeftParen},
    {")", unity::KeyCode::RightParen},
    {"[", unity::KeyCode::LeftBracket},
    {"]", unity::KeyCode::RightBracket},
    {"{", unity::KeyCode::LeftCurlyBracket},
    {"}", unity::KeyCode::RightCurlyBracket},
    {"@", unity::KeyCode::At},
    {"*", unity::KeyCode::Asterisk},
    {"/", unity::KeyCode::Slash},
    {"\"", unity::KeyCode::DoubleQuote},
    {"\\", unity::KeyCode::Backslash},
    {"&", unity::KeyCode::Ampersand},
    {"#", unity::KeyCode::Hash, "HSH"},
    {"%", unity::KeyCode::Percent},
    {"`", unity::KeyCode::BackQuote},
    {"^", unity::KeyCode::Caret, "CAR"},
    {"+", unity::KeyCode::Plus, "PLS"},
    {"<", unity::KeyCode::Less},
    {"=", unity::KeyCode::Equals},
    {">", unity::KeyCode::Greater},
    {"~", unity::KeyCode::Tilde},
    {"$", unity::KeyCode::Dollar},
    {"0", unity::KeyCode::Alpha0},
    {"1", unity::KeyCode::Alpha1},
    {"2", unity::KeyCode::Alpha2},
    {"3", unity::KeyCode::Alpha3},
    {"4", unity::KeyCode::Alpha4},
    {"5", unity::KeyCode::Alpha5},
    {"6", unity::KeyCode::Alpha6},
    {"7", unity::KeyCode::Alpha7},
    {"8", unity::KeyCode::Alpha8},
    {"9", unity::KeyCode::Alpha9},
    {"A", unity::KeyCode::A},
    {"B", unity::KeyCode::B},
    {"C", unity::KeyCode::C},
    {"D", unity::KeyCode::D},
    {"E", unity::KeyCode::E},
    {"F", unity::KeyCode::F},
    {"G", unity::KeyCode::G},
    {"H", unity::KeyCode::H},
    {"I", unity::KeyCode::I},
    {"J", unity::KeyCode::J},
    {"K", unity::KeyCode::K},
    {"L", unity::KeyCode::L},
    {"M", unity::KeyCode::M},
    {"N", unity::KeyCode::N},
    {"O", unity::KeyCode::O},
    {"P", unity::KeyCode::P},
    {"Q", unity::KeyCode::Q},
    {"R", unity::KeyCode::R},
    {"S", unity::KeyCode::S},
    {"T", unity::KeyCode::T},
    {"U", unity::KeyCode::U},
    {"V", unity::KeyCode::V},
    {"W", unity::KeyCode::W},
    {"X", unity::KeyCode::X},
    {"Y", unity::KeyCode::Y},
    {"Z", unity::KeyCode::Z},
    {"F1", unity::KeyCode::F1},
    {"F2", unity::KeyCode::F2},
    {"F3", unity::KeyCode::F3},
    {"F4", unity::KeyCode::F4},
    {"F5", unity::KeyCode::F5},
    {"F6", unity::KeyCode::F6},
    {"F7", unity::KeyCode::F7},
    {"F8", unity::KeyCode::F8},
    {"F9", unity::KeyCode::F9},
    {"F10", unity::KeyCode::F10},
    {"F11", unity::KeyCode::F11},
    {"F12", unity::KeyCode::F12},
    {"F13", unity::KeyCode::F13},
    {"F14", unity::KeyCode::F14},
    {"F15", unity::KeyCode::F15},
    {"NUMLOCK", unity::KeyCode::Numlock},
    {"KEY0", unity::KeyCode::Keypad0},
    {"KEY1", unity::KeyCode::Keypad1},
    {"KEY2", unity::KeyCode::Keypad2},
    {"KEY3", unity::KeyCode::Keypad3},
    {"KEY4", unity::KeyCode::Keypad4},
    {"KEY5", unity::KeyCode::Keypad5},
    {"KEY6", unity::KeyCode::Keypad6},
    {"KEY7", unity::KeyCode::Keypad7},
    {"KEY8", unity::KeyCode::Keypad8},
    {"KEY9", unity::KeyCode::Keypad9},
    {"KEYDIVIDE", unity::KeyCode::KeypadDivide},
    {"KEYENTER", unity::KeyCode::KeypadEnter},
    {"KEYEQUAL", unity::KeyCode::KeypadEquals},
    {"KEYMINUS", unity::KeyCode::KeypadMinus},
    {"KEYMULTI", unity::KeyCode::KeypadMultiply},
    {"KEYPERIOD", unity::KeyCode::KeypadPeriod},
    {"KEYPLUS", unity::KeyCode::KeypadPlus},
};

// Modifier names. The unsided ones match either side of the keyboard. Badges use AutoHotkey's ASCII notation.
struct ModifierName {
  std::string_view              name;
  std::array<unity::KeyCode, 2> keys;
  std::string_view              hint;
};

constexpr ModifierName kModifiers[] = {
    {"SHIFT", {unity::KeyCode::LeftShift, unity::KeyCode::RightShift}, "+"},
    {"CTRL", {unity::KeyCode::LeftControl, unity::KeyCode::RightControl}, "^"},
    {"ALT", {unity::KeyCode::LeftAlt, unity::KeyCode::RightAlt}, "!"},
    {"APPLE", {unity::KeyCode::LeftApple, unity::KeyCode::RightApple}, "#"},
    {"CMD", {unity::KeyCode::LeftCommand, unity::KeyCode::RightCommand}, "#"},
    {"WIN", {unity::KeyCode::LeftWindows, unity::KeyCode::RightWindows}, "#"},
    {"LSHIFT", {unity::KeyCode::LeftShift, unity::KeyCode::LeftShift}, "<+"},
    {"RSHIFT", {unity::KeyCode::RightShift, unity::KeyCode::RightShift}, ">+"},
    {"LCTRL", {unity::KeyCode::LeftControl, unity::KeyCode::LeftControl}, "<^"},
    {"RCTRL", {unity::KeyCode::RightControl, unity::KeyCode::RightControl}, ">^"},
    {"LALT", {unity::KeyCode::LeftAlt, unity::KeyCode::LeftAlt}, "<!"},
    {"RALT", {unity::KeyCode::RightAlt, unity::KeyCode::RightAlt}, ">!"},
    {"ALTGR", {unity::KeyCode::AltGr, unity::KeyCode::AltGr}, "!"},
    {"LAPPLE", {unity::KeyCode::LeftApple, unity::KeyCode::LeftApple}, "<#"},
    {"LCOM", {unity::KeyCode::LeftCommand, unity::KeyCode::LeftCommand}, "<#"},
    {"LWIN", {unity::KeyCode::LeftWindows, unity::KeyCode::LeftWindows}, "<#"},
    {"RAPPLE", {unity::KeyCode::RightApple, unity::KeyCode::RightApple}, ">#"},
    {"RCOM", {unity::KeyCode::RightCommand, unity::KeyCode::RightCommand}, ">#"},
    {"RWIN", {unity::KeyCode::RightWindows, unity::KeyCode::RightWindows}, ">#"},
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

std::array<std::vector<KeyChord>, static_cast<size_t>(Action::kCount)> bindings;

std::vector<KeyChord>& BindingsOf(Action action)
{ return bindings[static_cast<size_t>(action)]; }

// Keys that completed a binding with modifiers, until released. A plain binding on the same key waits for the
// release, so letting go of CTRL before PGUP does not turn a held CTRL-PGUP into PGUP.
std::vector<unity::KeyCode> used_keys;

bool IsUsed(unity::KeyCode key)
{
  std::erase_if(used_keys, [](unity::KeyCode used) { return !unity::Key::Pressed(used); });
  return std::ranges::find(used_keys, key) != used_keys.end();
}

bool ModifiersHeld(const KeyChord& chord)
{
  return std::ranges::all_of(chord.modifiers,
                             [](const auto& group) { return std::ranges::any_of(group, unity::Key::Pressed); });
}

template <bool (*KeyState)(unity::KeyCode)> bool Matches(Action action)
{
  for (const auto& chord : BindingsOf(action)) {
    if (chord.key == unity::KeyCode::None || !KeyState(chord.key))
      continue;
    if (chord.modifiers.empty() ? unity::Key::IsModified() || IsUsed(chord.key) : !ModifiersHeld(chord))
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
  Action                          action;
  std::string_view                fallback;
  Tier                            tier    = Tier::kAlways;
  std::array<std::string_view, 2> aliases = {};
};

// Order is the order of the debug log; the TOML output is sorted by key anyway.
constexpr Setting kSettings[] = {
    {"move_up", Action::kMoveUp, "W|UP"},
    {"move_down", Action::kMoveDown, "S|DOWN"},
    {"move_left", Action::kMoveLeft, "A|LEFT"},
    {"move_right", Action::kMoveRight, "D|RIGHT"},
    {"set_hotkeys_disabled",
     Action::kDisableHotKeys,
     "CTRL-ALT-MINUS",
     Tier::kAlways,
     {"set_hotkeys_disble", "set_hotkeys_disable"}},
    {"set_hotkeys_enabled", Action::kEnableHotKeys, "CTRL-ALT-=", Tier::kAlways, {"set_hotkeys_enable"}},
    {"select_chatalliance", Action::kSelectChatAlliance, "CTRL-2"},
    {"select_chatglobal", Action::kSelectChatGlobal, "CTRL-1"},
    {"select_chatprivate", Action::kSelectChatPrivate, "CTRL-3"},
    {"quit", Action::kQuit, "F10"},
    {"select_ship1", Action::kSelectShip1, "1"},
    {"select_ship2", Action::kSelectShip2, "2"},
    {"select_ship3", Action::kSelectShip3, "3"},
    {"select_ship4", Action::kSelectShip4, "4"},
    {"select_ship5", Action::kSelectShip5, "5"},
    {"select_ship6", Action::kSelectShip6, "6"},
    {"select_ship7", Action::kSelectShip7, "7"},
    {"select_ship8", Action::kSelectShip8, "8"},
    {"select_current", Action::kSelectCurrent, "CTRL-SPACE"},
    {"focus_search", Action::kFocusSearch, "CTRL-F"},
    {"action_primary", Action::kActionPrimary, "SPACE|MOUSE1"},
    {"action_secondary", Action::kActionSecondary, "TAB|MOUSE4"},
    {"action_queue", Action::kActionQueue, "SPACE|MOUSE1"},
    {"action_queue_clear", Action::kActionQueueClear, "CTRL-C"},
    {"action_view", Action::kActionView, "V|MOUSE2"},
    {"action_recall", Action::kActionRecall, "R|MOUSE3"},
    {"action_recall_cancel", Action::kActionRecallCancel, "SPACE|MOUSE1"},
    {"action_repair", Action::kActionRepair, "R|MOUSE3"},
    {"show_chat", Action::kShowChat, "C"},
    {"show_chatside1", Action::kShowChatSide1, "ALT-C"},
    {"show_chatside2", Action::kShowChatSide2, "`"},
    {"show_galaxy", Action::kShowGalaxy, "G"},
    {"show_galaxy_native", Action::kNativeShortcutGalaxy, "CTRL-G", Tier::kAlways, {"native_shortcut_galaxy"}},
    {"show_system", Action::kShowSystem, "H"},
    {"zoom_preset1", Action::kZoomPreset1, "F1"},
    {"zoom_preset2", Action::kZoomPreset2, "F2"},
    {"zoom_preset3", Action::kZoomPreset3, "F3"},
    {"zoom_preset4", Action::kZoomPreset4, "F4"},
    {"zoom_preset5", Action::kZoomPreset5, "F5"},
    {"zoom_in", Action::kZoomIn, "Q"},
    {"zoom_out", Action::kZoomOut, "E"},
    {"zoom_max", Action::kZoomMax, "MINUS"},
    {"zoom_min", Action::kZoomMin, "BACKSPACE"},
    {"zoom_reset", Action::kZoomReset, "="},
    {"ui_scaleup", Action::kUiScaleUp, "PGUP"},
    {"ui_scaledown", Action::kUiScaleDown, "PGDOWN"},
    {"ui_scaleshipup", Action::kUiShipScaleUp, "CTRL-PGUP"},
    {"ui_scaleshipdown", Action::kUiShipScaleDown, "CTRL-PGDOWN"},
    {"ui_scaleviewerup", Action::kUiViewerScaleUp, "SHIFT-PGUP"},
    {"ui_scaleviewerdown", Action::kUiViewerScaleDown, "SHIFT-PGDOWN"},
    {"log_debug", Action::kLogLevelDebug, "CTRL-SHIFT-F9"},
    {"log_trace", Action::kLogLevelTrace, "CTRL-SHIFT-F7"},
    {"log_info", Action::kLogLevelInfo, "CTRL-SHIFT-F8"},
    {"log_warn", Action::kLogLevelWarn, "CTRL-SHIFT-F10"},
    {"log_error", Action::kLogLevelError, "CTRL-SHIFT-F11"},
    {"log_off", Action::kLogLevelOff, "CTRL-SHIFT-F12"},
    {"restart", Action::kRestart, "F9"},
    {"reload_settings", Action::kReloadSettings, "CTRL-SHIFT-F6"},
    {"show_awayteam", Action::kShowAwayTeam, "T"},
    {"show_gifts", Action::kShowGifts, "/"},
    {"show_artifacts", Action::kShowArtifacts, "SHIFT-I"},
    {"show_commander", Action::kShowCommander, "O"},
    {"show_daily", Action::kShowDaily, "Z"},
    {"show_events", Action::kShowEvents, "SHIFT-E"},
    {"show_events_native", Action::kNativeShortcutEvents, "CTRL-E", Tier::kAlways, {"native_shortcut_events"}},
    {"show_exocomp", Action::kShowExoComp, "X"},
    {"show_factions", Action::kShowFactions, "F"},
    {"show_inventory", Action::kShowInventory, "I"},
    {"show_missions", Action::kShowMissions, "M"},
    {"show_research", Action::kShowResearch, "U"},
    {"show_scrapyard", Action::kShowScrapYard, "Y"},
    {"show_settings", Action::kShowSettings, "SHIFT-S"},
    {"toggle_shortcut_hints", Action::kToggleShortcutHints, "HOME"},
    {"show_officers", Action::kShowOfficers, "SHIFT-O"},
    {"show_officerpresets", Action::kShowOfficerPresets, "CTRL-O"},
    {"show_officerswap", Action::kShowOfficerSwap, "ALT-O"},
    {"show_qtrials", Action::kShowQTrials, "SHIFT-Q"},
    {"show_refinery", Action::kShowRefinery, "SHIFT-F"},
    {"show_ships", Action::kShowShips, "N"},
    {"show_shipconstruction", Action::kShowShipConstruction, "SHIFT-N"},
    {"show_shipswap", Action::kShowShipSwap, "ALT-N"},
    {"show_shields", Action::kShowShields, "CTRL-S"},
    {"show_battlelogs", Action::kShowBattlelogs, "SHIFT-B"},
    {"show_stationexterior", Action::kShowStationExterior, "SHIFT-G"},
    {"show_stationinterior", Action::kShowStationInterior, "SHIFT-H"},
    {"show_haven", Action::kShowHaven, "ALT-H"},
    {"toggle_queue", Action::kToggleQueue, "CTRL-Q"},
    {"toggle_instant_warp", Action::kToggleAutoConfirmInstantWarp, "ALT-I"},
    {"show_alliance", Action::kShowAlliance, "ALT-'", Tier::kExtended},
    {"show_alliance_help", Action::kShowAllianceHelp, "SHIFT-'", Tier::kExperimental},
    {"show_alliance_armada", Action::kShowAllianceArmada, "CTRL-'", Tier::kExperimental},
    {"show_bookmarks", Action::kShowBookmarks, "B", Tier::kExtended},
    {"show_lookup", Action::kShowLookup, "L", Tier::kExperimental},
    {"set_zoom_preset1", Action::kSetZoomPreset1, "SHIFT-F1", Tier::kExtended},
    {"set_zoom_preset2", Action::kSetZoomPreset2, "SHIFT-F2", Tier::kExtended},
    {"set_zoom_preset3", Action::kSetZoomPreset3, "SHIFT-F3", Tier::kExtended},
    {"set_zoom_preset4", Action::kSetZoomPreset4, "SHIFT-F4", Tier::kExtended},
    {"set_zoom_preset5", Action::kSetZoomPreset5, "SHIFT-F5", Tier::kExtended},
    {"set_zoom_default", Action::kSetZoomDefault, "CTRL-=", Tier::kExtended},
    {"toggle_preview_locate", Action::kTogglePreviewLocate, "CTRL-R", Tier::kExtended},
    {"toggle_preview_recall", Action::kTogglePreviewRecall, "CTRL-T", Tier::kExtended},
    {"toggle_cargo_default", Action::kToggleCargoDefault, "ALT-1", Tier::kExtended},
    {"toggle_cargo_player", Action::kToggleCargoPlayer, "ALT-2", Tier::kExtended},
    {"toggle_cargo_station", Action::kToggleCargoStation, "ALT-3", Tier::kExtended},
    {"toggle_cargo_hostile", Action::kToggleCargoHostile, "ALT-4", Tier::kExtended},
    {"toggle_cargo_armada", Action::kToggleCargoArmada, "ALT-5", Tier::kExtended},
};

// Binds every chord in `value` ("CTRL-G|F8"). Reports whether any was valid.
bool BindChords(const Setting& setting, std::string_view key, std::string_view value)
{
  bool bound = false;
  for (const auto& text : StrSplit(value, '|')) {
    if (auto chord = KeyChord::Parse(text); chord.key != unity::KeyCode::None) {
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
  if (chord.key != unity::KeyCode::None)
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
    const auto* key      = i + 1 == tokens.size() && this->key != unity::KeyCode::None ? FindKey(tokens[i]) : nullptr;
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

void Bind(Action action, KeyChord chord)
{ BindingsOf(action).push_back(std::move(chord)); }

void Clear()
{
  for (auto& chords : bindings)
    chords.clear();
  used_keys.clear();
}

bool Owns(unity::KeyCode key)
{ return IsUsed(key); }

bool HasBinding(Action action)
{
  return std::ranges::any_of(BindingsOf(action), [](const auto& chord) { return chord.key != unity::KeyCode::None; });
}

bool IsPressed(Action action)
{ return Matches<unity::Key::Pressed>(action); }

bool IsDown(Action action)
{ return Matches<unity::Key::Down>(action); }

std::string Describe(Action action)
{
  std::string text;
  for (const auto& chord : BindingsOf(action)) {
    if (!text.empty())
      text += " | ";
    text += chord.Text();
  }
  return text;
}

std::string Hint(Action action)
{
  // The game's badges have room for one binding.
  return BindingsOf(action).empty() ? std::string{} : BindingsOf(action).front().Hint();
}
} // namespace Shortcut

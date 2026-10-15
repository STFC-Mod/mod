#include "patches/shortcuts.h"

#include "patches/input.h"
#include "str_utils.h"

#include <spdlog/spdlog.h>

#include <array>
#include <optional>
#include <unordered_map>

namespace
{
const std::unordered_map<std::string_view, KeyCode> kKeyNames = {
    {"LALT", KeyCode::LeftAlt},
    {"LAPPLE", KeyCode::LeftApple},
    {"LCOM", KeyCode::LeftCommand},
    {"LCTRL", KeyCode::LeftControl},
    {"ALTGR", KeyCode::AltGr},
    {"END", KeyCode::End},
    {"HOME", KeyCode::Home},
    {"PGDOWN", KeyCode::PageDown},
    {"PGUP", KeyCode::PageUp},
    {"DOWN", KeyCode::DownArrow},
    {"LEFT", KeyCode::LeftArrow},
    {"RIGHT", KeyCode::RightArrow},
    {"UP", KeyCode::UpArrow},
    {"BACKSPACE", KeyCode::Backspace},
    {"BREAK", KeyCode::Break},
    {"CAPS", KeyCode::CapsLock},
    {"CLEAR", KeyCode::Clear},
    {"DELETE", KeyCode::Delete},
    {"ESCAPE", KeyCode::Escape},
    {"HELP", KeyCode::Help},
    {"INSERT", KeyCode::Insert},
    {"LSHIFT", KeyCode::LeftShift},
    {"LWIN", KeyCode::LeftWindows},
    {"MENU", KeyCode::Menu},
    {"PAUSE", KeyCode::Pause},
    {"PRINT", KeyCode::Print},
    {"RALT", KeyCode::RightAlt},
    {"RAPPLE", KeyCode::RightApple},
    {"RCOM", KeyCode::RightCommand},
    {"RCTRL", KeyCode::RightControl},
    {"RETURN", KeyCode::Return},
    {"RSHIFT", KeyCode::RightShift},
    {"RWIN", KeyCode::RightWindows},
    {"SCROLL", KeyCode::ScrollLock},
    {"SYSREQ", KeyCode::SysReq},
    {"TAB", KeyCode::Tab},
    {"MOUSE0", KeyCode::Mouse0},
    {"MOUSE1", KeyCode::Mouse1},
    {"MOUSE2", KeyCode::Mouse2},
    {"MOUSE3", KeyCode::Mouse3},
    {"MOUSE4", KeyCode::Mouse4},
    {"MOUSE5", KeyCode::Mouse5},
    {"MOUSE6", KeyCode::Mouse6},
    {"SPACE", KeyCode::Space},
    // Chords are split on '-', so the minus key needs a name.
    {"MINUS", KeyCode::Minus},
    {"_", KeyCode::Underscore},
    {",", KeyCode::Comma},
    {";", KeyCode::Semicolon},
    {":", KeyCode::Colon},
    {"!", KeyCode::Exclaim},
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
    {"#", KeyCode::Hash},
    {"%", KeyCode::Percent},
    {"`", KeyCode::BackQuote},
    {"^", KeyCode::Caret},
    {"+", KeyCode::Plus},
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

// Modifier names that match either side of the keyboard.
std::optional<std::vector<KeyCode>> ModifierGroup(std::string_view token)
{
  if (token == "SHIFT")
    return {{KeyCode::LeftShift, KeyCode::RightShift}};
  if (token == "ALT")
    return {{KeyCode::LeftAlt, KeyCode::RightAlt}};
  if (token == "CTRL")
    return {{KeyCode::LeftControl, KeyCode::RightControl}};
  if (token == "APPLE")
    return {{KeyCode::LeftApple, KeyCode::RightApple}};
  if (token == "CMD")
    return {{KeyCode::LeftCommand, KeyCode::RightCommand}};
  if (token == "WIN")
    return {{KeyCode::LeftWindows, KeyCode::RightWindows}};
  if (const auto key = ParseKeyName(token); IsModifierKey(key))
    return {{key}};
  return std::nullopt;
}

struct HintToken {
  std::string_view token;
  std::string_view compact;
  bool             primary_key_only = false;
};

// AutoHotkey's ASCII modifier notation, so the game's limited badge font can render every hint.
constexpr auto kHintTokens = std::to_array<HintToken>({
    {"SHIFT", "+"},      {"CTRL", "^"},      {"ALT", "!"},       {"ALTGR", "!"},    {"APPLE", "#"},
    {"CMD", "#"},        {"WIN", "#"},       {"LSHIFT", "<+"},   {"RSHIFT", ">+"},  {"LCTRL", "<^"},
    {"RCTRL", ">^"},     {"LALT", "<!"},     {"RALT", ">!"},     {"LAPPLE", "<#"},  {"LCOM", "<#"},
    {"LWIN", "<#"},      {"RAPPLE", ">#"},   {"RCOM", ">#"},     {"RWIN", ">#"},    {"+", "PLS", true},
    {"^", "CAR", true},  {"!", "EXC", true}, {"#", "HSH", true}, {"SPACE", "SPC"},  {"MOUSE0", "M0"},
    {"MOUSE1", "M1"},    {"MOUSE2", "M2"},   {"MOUSE3", "M3"},   {"MOUSE4", "M4"},  {"MOUSE5", "M5"},
    {"MOUSE6", "M6"},    {"ENTER", "ENT"},   {"RETURN", "ENT"},  {"ESCAPE", "ESC"}, {"TAB", "TAB"},
    {"BACKSPACE", "BS"}, {"DELETE", "DEL"},  {"MINUS", "-"},     {"EQUAL", "="},    {"LEFT", "LT"},
    {"RIGHT", "RT"},     {"UP", "UP"},       {"DOWN", "DN"},     {"PGUP", "PU"},    {"PGDOWN", "PD"},
    {"HOME", "HM"},      {"END", "END"},
});

constexpr std::string_view CompactToken(std::string_view token, bool is_primary_key)
{
  for (const auto& mapping : kHintTokens)
    if (mapping.token == token && (!mapping.primary_key_only || is_primary_key))
      return mapping.compact;
  return token;
}

static_assert(CompactToken("CTRL", false) == "^");
static_assert(CompactToken("+", true) == "PLS");
static_assert(CompactToken("+", false) == "+");
static_assert(CompactToken("F7", true) == "F7");

std::array<std::vector<KeyChord>, GameFunction::Max> bindings;

// Movement reads its own direction, so claiming it would suppress its own pan on the next frame. Other modified
// actions keep ownership of an arrow until it is released.
bool IsMovement(GameFunction action)
{
  return action == GameFunction::MoveLeft || action == GameFunction::MoveRight || action == GameFunction::MoveUp
         || action == GameFunction::MoveDown;
}

bool ModifiersMatch(const KeyChord& chord)
{
  if (chord.modifiers.empty())
    return !Key::IsModified();
  return std::ranges::all_of(chord.modifiers,
                             [](const auto& group) { return std::ranges::any_of(group, Key::Pressed); });
}

template <bool (*KeyState)(KeyCode)> bool Matches(GameFunction action)
{
  for (const auto& chord : bindings[action]) {
    if (chord.key == KeyCode::None || !KeyState(chord.key) || !ModifiersMatch(chord))
      continue;
    if (!chord.modifiers.empty() && !IsMovement(action))
      Key::ClaimDirectionalInput(chord.key);
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
    {"set_hotkeys_enabled",
     GameFunction::EnableHotKeys,
     "CTRL-ALT-=",
     Tier::kAlways,
     {"set_hotkeys_enable"}},
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
    {"show_galaxy_native",
     GameFunction::NativeShortcutGalaxy,
     "CTRL-G",
     Tier::kAlways,
     {"native_shortcut_galaxy"}},
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
    {"show_awayteam", GameFunction::ShowAwayTeam, "T"},
    {"show_gifts", GameFunction::ShowGifts, "/"},
    {"show_artifacts", GameFunction::ShowArtifacts, "SHIFT-I"},
    {"show_commander", GameFunction::ShowCommander, "O"},
    {"show_daily", GameFunction::ShowDaily, "Z"},
    {"show_events", GameFunction::ShowEvents, "SHIFT-E"},
    {"show_events_native",
     GameFunction::NativeShortcutEvents,
     "CTRL-E",
     Tier::kAlways,
     {"native_shortcut_events"}},
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

// The value a setting resolves to before parsing: the configured string, an alias, or the default.
struct Source {
  std::string value;
  std::string key; // where the value came from
  bool        from_config = false;
  bool        valid_type  = true;

  std::string Label(std::string_view setting) const
  {
    if (!valid_type)
      return "invalid";
    if (!from_config)
      return "default";
    return key == setting ? "config" : "alias:" + key;
  }
};

Source Lookup(const toml::table* shortcuts, std::string_view key, std::string_view fallback)
{
  const auto* node = shortcuts ? shortcuts->get(key) : nullptr;
  if (!node)
    return {std::string(fallback), std::string(key), false, true};
  if (auto value = node->value<std::string>())
    return {*value, std::string(key), true, true};
  return {"", std::string(key), true, false};
}

Source Resolve(const toml::table* shortcuts, const Setting& setting)
{
  auto source = Lookup(shortcuts, setting.key, setting.fallback);
  for (const auto alias : setting.aliases) {
    if (alias.empty() || !shortcuts || !shortcuts->contains(alias))
      continue;
    if (source.from_config) {
      spdlog::warn("Ignoring deprecated shortcut alias [shortcuts].{} because [shortcuts].{} is set.", alias,
                   setting.key);
      continue;
    }
    spdlog::warn("Deprecated shortcut alias [shortcuts].{} is supported for compatibility; use [shortcuts].{}.", alias,
                 setting.key);
    return Lookup(shortcuts, alias, setting.fallback);
  }
  return source;
}

void LoadSetting(const toml::table* shortcuts, toml::table& out, toml::table& out_source, const Setting& setting)
{
  auto source = Resolve(shortcuts, setting);
  if (!source.valid_type) {
    spdlog::error("Invalid shortcut value [shortcuts].{} must be a string; using default for [shortcuts].{}.",
                  source.key, setting.key);
    source = {std::string(setting.fallback), std::string(setting.key), false, true};
  }

  const auto disable = [&](std::string label) {
    out.insert_or_assign(setting.key, "NONE");
    out_source.insert_or_assign(setting.key, std::move(label));
    spdlog::debug("shortcut value shortcuts.{} value: NONE", setting.key);
  };

  // An explicit empty string disables the binding, just like NONE. Missing or invalid settings fall back to
  // their defaults.
  const auto value = AsciiStrToUpper(StripAsciiWhitespace(source.value));
  if (value.empty() || value == "NONE")
    return disable(source.Label(setting.key));

  bool bound = false;
  for (const auto& text : StrSplit(value, '|')) {
    if (auto chord = KeyChord::Parse(text); chord.key != KeyCode::None) {
      Shortcut::Bind(setting.action, std::move(chord));
      bound = true;
    } else {
      spdlog::warn("Invalid shortcut token [shortcuts].{} token='{}' value='{}'; ignoring token.", source.key, text,
                   source.value);
    }
  }

  if (!bound) {
    if (source.from_config) {
      spdlog::error("No valid shortcut tokens for [shortcuts].{} value='{}'; using default for [shortcuts].{}.",
                    source.key, source.value, setting.key);
      return LoadSetting(nullptr, out, out_source, setting);
    }
    spdlog::error("Default shortcut [shortcuts].{} value='{}' has no valid tokens; action disabled.", setting.key,
                  source.value);
    return disable("invalid");
  }

  const auto described = Shortcut::Describe(setting.action);
  out.insert_or_assign(setting.key, described);
  out_source.insert_or_assign(setting.key, source.Label(setting.key));
  spdlog::debug("shortcut value shortcuts.{} value: {}", setting.key, described);
}
} // namespace

KeyCode ParseKeyName(std::string_view name)
{
  const auto it = kKeyNames.find(AsciiStrToUpper(name));
  return it == kKeyNames.end() ? KeyCode::None : it->second;
}

bool IsModifierKey(KeyCode key)
{
  switch (key) {
    case KeyCode::LeftAlt:
    case KeyCode::LeftCommand:
    case KeyCode::LeftControl:
    case KeyCode::AltGr:
    case KeyCode::LeftShift:
    case KeyCode::LeftWindows:
    case KeyCode::RightAlt:
    case KeyCode::RightCommand:
    case KeyCode::RightControl:
    case KeyCode::RightShift:
    case KeyCode::RightWindows:
      return true;
    default:
      return false;
  }
}

KeyChord KeyChord::Parse(std::string_view text)
{
  KeyChord chord;
  for (auto& token : StrSplit(AsciiStrToUpper(text), '-')) {
    if (auto group = ModifierGroup(token)) {
      chord.modifiers.push_back(std::move(*group));
      chord.tokens.push_back(std::move(token));
    } else if (const auto key = ParseKeyName(token); key != KeyCode::None) {
      chord.key = key;
      chord.tokens.push_back(std::move(token));
    }
  }
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
  for (size_t i = 0; i < tokens.size(); ++i)
    hint += CompactToken(tokens[i], i + 1 == tokens.size());
  return hint;
}

namespace Shortcut
{
void Load(const toml::table& config, toml::table& parsed, bool extended, bool experimental)
{
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
}

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

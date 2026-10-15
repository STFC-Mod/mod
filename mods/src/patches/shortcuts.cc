#include "patches/shortcuts.h"

#include "defaultconfig.h"
#include "patches/input.h"
#include "str_utils.h"

#include <spdlog/spdlog.h>

#include <array>
#include <optional>
#include <unordered_map>

namespace
{
namespace DCSH = DefaultConfig::Shortcuts;

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
    {"move_up", GameFunction::MoveUp, DCSH::move_up},
    {"move_down", GameFunction::MoveDown, DCSH::move_down},
    {"move_left", GameFunction::MoveLeft, DCSH::move_left},
    {"move_right", GameFunction::MoveRight, DCSH::move_right},
    {"set_hotkeys_disabled",
     GameFunction::DisableHotKeys,
     DCSH::set_hotkeys_disabled,
     Tier::kAlways,
     {"set_hotkeys_disble", "set_hotkeys_disable"}},
    {"set_hotkeys_enabled",
     GameFunction::EnableHotKeys,
     DCSH::set_hotkeys_enabled,
     Tier::kAlways,
     {"set_hotkeys_enable"}},
    {"select_chatalliance", GameFunction::SelectChatAlliance, DCSH::select_chatalliance},
    {"select_chatglobal", GameFunction::SelectChatGlobal, DCSH::select_chatglobal},
    {"select_chatprivate", GameFunction::SelectChatPrivate, DCSH::select_chatprivate},
    {"quit", GameFunction::Quit, DCSH::quit},
    {"select_ship1", GameFunction::SelectShip1, DCSH::select_ship1},
    {"select_ship2", GameFunction::SelectShip2, DCSH::select_ship2},
    {"select_ship3", GameFunction::SelectShip3, DCSH::select_ship3},
    {"select_ship4", GameFunction::SelectShip4, DCSH::select_ship4},
    {"select_ship5", GameFunction::SelectShip5, DCSH::select_ship5},
    {"select_ship6", GameFunction::SelectShip6, DCSH::select_ship6},
    {"select_ship7", GameFunction::SelectShip7, DCSH::select_ship7},
    {"select_ship8", GameFunction::SelectShip8, DCSH::select_ship8},
    {"select_current", GameFunction::SelectCurrent, DCSH::select_current},
    {"focus_search", GameFunction::FocusSearch, DCSH::focus_search},
    {"action_primary", GameFunction::ActionPrimary, DCSH::action_primary},
    {"action_secondary", GameFunction::ActionSecondary, DCSH::action_secondary},
    {"action_queue", GameFunction::ActionQueue, DCSH::action_queue},
    {"action_queue_clear", GameFunction::ActionQueueClear, DCSH::action_queue_clear},
    {"action_view", GameFunction::ActionView, DCSH::action_view},
    {"action_recall", GameFunction::ActionRecall, DCSH::action_recall},
    {"action_recall_cancel", GameFunction::ActionRecallCancel, DCSH::action_recall_cancel},
    {"action_repair", GameFunction::ActionRepair, DCSH::action_repair},
    {"show_chat", GameFunction::ShowChat, DCSH::show_chat},
    {"show_chatside1", GameFunction::ShowChatSide1, DCSH::show_chatside1},
    {"show_chatside2", GameFunction::ShowChatSide2, DCSH::show_chatside2},
    {"show_galaxy", GameFunction::ShowGalaxy, DCSH::show_galaxy},
    {"show_galaxy_native",
     GameFunction::NativeShortcutGalaxy,
     DCSH::show_galaxy_native,
     Tier::kAlways,
     {"native_shortcut_galaxy"}},
    {"show_system", GameFunction::ShowSystem, DCSH::show_system},
    {"zoom_preset1", GameFunction::ZoomPreset1, DCSH::zoom_preset1},
    {"zoom_preset2", GameFunction::ZoomPreset2, DCSH::zoom_preset2},
    {"zoom_preset3", GameFunction::ZoomPreset3, DCSH::zoom_preset3},
    {"zoom_preset4", GameFunction::ZoomPreset4, DCSH::zoom_preset4},
    {"zoom_preset5", GameFunction::ZoomPreset5, DCSH::zoom_preset5},
    {"zoom_in", GameFunction::ZoomIn, DCSH::zoom_in},
    {"zoom_out", GameFunction::ZoomOut, DCSH::zoom_out},
    {"zoom_max", GameFunction::ZoomMax, DCSH::zoom_max},
    {"zoom_min", GameFunction::ZoomMin, DCSH::zoom_min},
    {"zoom_reset", GameFunction::ZoomReset, DCSH::zoom_reset},
    {"ui_scaleup", GameFunction::UiScaleUp, DCSH::ui_scaleup},
    {"ui_scaledown", GameFunction::UiScaleDown, DCSH::ui_scaledown},
    {"ui_scaleshipup", GameFunction::UiShipScaleUp, DCSH::ui_scaleshipup},
    {"ui_scaleshipdown", GameFunction::UiShipScaleDown, DCSH::ui_scaleshipdown},
    {"ui_scaleviewerup", GameFunction::UiViewerScaleUp, DCSH::ui_scaleviewerup},
    {"ui_scaleviewerdown", GameFunction::UiViewerScaleDown, DCSH::ui_scaleviewerdown},
    {"log_debug", GameFunction::LogLevelDebug, DCSH::log_debug},
    {"log_trace", GameFunction::LogLevelTrace, DCSH::log_trace},
    {"log_info", GameFunction::LogLevelInfo, DCSH::log_info},
    {"log_warn", GameFunction::LogLevelWarn, DCSH::log_warn},
    {"log_error", GameFunction::LogLevelError, DCSH::log_error},
    {"log_off", GameFunction::LogLevelOff, DCSH::log_off},
    {"restart", GameFunction::Restart, DCSH::restart},
    {"show_awayteam", GameFunction::ShowAwayTeam, DCSH::show_awayteam},
    {"show_gifts", GameFunction::ShowGifts, DCSH::show_gifts},
    {"show_artifacts", GameFunction::ShowArtifacts, DCSH::show_artifacts},
    {"show_commander", GameFunction::ShowCommander, DCSH::show_commander},
    {"show_daily", GameFunction::ShowDaily, DCSH::show_daily},
    {"show_events", GameFunction::ShowEvents, DCSH::show_events},
    {"show_events_native",
     GameFunction::NativeShortcutEvents,
     DCSH::show_events_native,
     Tier::kAlways,
     {"native_shortcut_events"}},
    {"show_exocomp", GameFunction::ShowExoComp, DCSH::show_exocomp},
    {"show_factions", GameFunction::ShowFactions, DCSH::show_factions},
    {"show_inventory", GameFunction::ShowInventory, DCSH::show_inventory},
    {"show_missions", GameFunction::ShowMissions, DCSH::show_missions},
    {"show_research", GameFunction::ShowResearch, DCSH::show_research},
    {"show_scrapyard", GameFunction::ShowScrapYard, DCSH::show_scrapyard},
    {"show_settings", GameFunction::ShowSettings, DCSH::show_settings},
    {"toggle_shortcut_hints", GameFunction::ToggleShortcutHints, DCSH::toggle_shortcut_hints},
    {"show_officers", GameFunction::ShowOfficers, DCSH::show_officers},
    {"show_officerpresets", GameFunction::ShowOfficerPresets, DCSH::show_officerpresets},
    {"show_officerswap", GameFunction::ShowOfficerSwap, DCSH::show_officerswap},
    {"show_qtrials", GameFunction::ShowQTrials, DCSH::show_qtrials},
    {"show_refinery", GameFunction::ShowRefinery, DCSH::show_refinery},
    {"show_ships", GameFunction::ShowShips, DCSH::show_ships},
    {"show_shipconstruction", GameFunction::ShowShipConstruction, DCSH::show_shipconstruction},
    {"show_shipswap", GameFunction::ShowShipSwap, DCSH::show_shipswap},
    {"show_shields", GameFunction::ShowShields, DCSH::show_shields},
    {"show_battlelogs", GameFunction::ShowBattlelogs, DCSH::show_battlelogs},
    {"show_stationexterior", GameFunction::ShowStationExterior, DCSH::show_stationexterior},
    {"show_stationinterior", GameFunction::ShowStationInterior, DCSH::show_stationinterior},
    {"show_haven", GameFunction::ShowHaven, DCSH::show_haven},
    {"toggle_queue", GameFunction::ToggleQueue, DCSH::toggle_queue},
    {"toggle_instant_warp", GameFunction::ToggleAutoConfirmInstantWarp, DCSH::toggle_instant_warp},
    {"show_alliance", GameFunction::ShowAlliance, DCSH::show_alliance, Tier::kExtended},
    {"show_alliance_help", GameFunction::ShowAllianceHelp, DCSH::show_alliance_help, Tier::kExperimental},
    {"show_alliance_armada", GameFunction::ShowAllianceArmada, DCSH::show_alliance_armada, Tier::kExperimental},
    {"show_bookmarks", GameFunction::ShowBookmarks, DCSH::show_bookmarks, Tier::kExtended},
    {"show_lookup", GameFunction::ShowLookup, DCSH::show_lookup, Tier::kExperimental},
    {"set_zoom_preset1", GameFunction::SetZoomPreset1, DCSH::set_zoom_preset1, Tier::kExtended},
    {"set_zoom_preset2", GameFunction::SetZoomPreset2, DCSH::set_zoom_preset2, Tier::kExtended},
    {"set_zoom_preset3", GameFunction::SetZoomPreset3, DCSH::set_zoom_preset3, Tier::kExtended},
    {"set_zoom_preset4", GameFunction::SetZoomPreset4, DCSH::set_zoom_preset4, Tier::kExtended},
    {"set_zoom_preset5", GameFunction::SetZoomPreset5, DCSH::set_zoom_preset5, Tier::kExtended},
    {"set_zoom_default", GameFunction::SetZoomDefault, DCSH::set_zoom_default, Tier::kExtended},
    {"toggle_preview_locate", GameFunction::TogglePreviewLocate, DCSH::toggle_preview_locate, Tier::kExtended},
    {"toggle_preview_recall", GameFunction::TogglePreviewRecall, DCSH::toggle_preview_recall, Tier::kExtended},
    {"toggle_cargo_default", GameFunction::ToggleCargoDefault, DCSH::toggle_cargo_default, Tier::kExtended},
    {"toggle_cargo_player", GameFunction::ToggleCargoPlayer, DCSH::toggle_cargo_player, Tier::kExtended},
    {"toggle_cargo_station", GameFunction::ToggleCargoStation, DCSH::toggle_cargo_station, Tier::kExtended},
    {"toggle_cargo_hostile", GameFunction::ToggleCargoHostile, DCSH::toggle_cargo_hostile, Tier::kExtended},
    {"toggle_cargo_armada", GameFunction::ToggleCargoArmada, DCSH::toggle_cargo_armada, Tier::kExtended},
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

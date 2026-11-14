#include "patches/shortcuts.h"

#include "str_utils.h"

#include <spdlog/spdlog.h>
#include <unity/input.h>

#include <algorithm>
#include <array>
#include <charconv>
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
    {"NUMLOCK", unity::KeyCode::Numlock},
    {"KEYDIVIDE", unity::KeyCode::KeypadDivide},
    {"KEYENTER", unity::KeyCode::KeypadEnter},
    {"KEYEQUAL", unity::KeyCode::KeypadEquals},
    {"KEYMINUS", unity::KeyCode::KeypadMinus},
    {"KEYMULTI", unity::KeyCode::KeypadMultiply},
    {"KEYPERIOD", unity::KeyCode::KeypadPeriod},
    {"KEYPLUS", unity::KeyCode::KeypadPlus},
};

// Modifier names. The unsided ones match either side of the keyboard. Badges use AutoHotkey's ASCII notation.
constexpr KeyChord::Modifier kModifiers[] = {
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

// Unity assigns contiguous codes to letters, digits, function keys and keypad digits.
std::optional<KeyName> FindKey(std::string_view name)
{
  if (name.size() == 1) {
    const char c = name[0];
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
      return KeyName{name, static_cast<unity::KeyCode>(c >= 'A' ? c + ('a' - 'A') : c)};
  }
  struct Range {
    std::string_view prefix;
    int32_t          first, last;
    unity::KeyCode   key;
  };
  constexpr Range ranges[] = {{"F", 1, 15, unity::KeyCode::F1}, {"KEY", 0, 9, unity::KeyCode::Keypad0}};
  for (const auto& range : ranges) {
    if (!name.starts_with(range.prefix))
      continue;
    const auto digits       = name.substr(range.prefix.size());
    int32_t    number       = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (error == std::errc{} && end == digits.data() + digits.size() && number >= range.first && number <= range.last
        && digits == std::to_string(number))
      return KeyName{name, static_cast<unity::KeyCode>(static_cast<int32_t>(range.key) + number - range.first)};
  }
  const auto it = std::ranges::find(kKeys, name, &KeyName::name);
  return it == std::end(kKeys) ? std::nullopt : std::optional(*it);
}

const KeyChord::Modifier* FindModifier(std::string_view name)
{
  const auto it = std::ranges::find(kModifiers, name, &KeyChord::Modifier::name);
  return it == std::end(kModifiers) ? nullptr : it;
}

Shortcut::Bindings bindings;

std::string DescribeChords(const std::vector<KeyChord>& chords)
{
  std::string text;
  for (const auto& chord : chords) {
    if (!text.empty())
      text += " | ";
    text += chord.Text();
  }
  return text;
}

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
  return std::ranges::all_of(
      chord.modifiers, [](const auto* modifier) { return std::ranges::any_of(modifier->keys, unity::Key::Pressed); });
}

template <bool (*KeyState)(unity::KeyCode)> bool Matches(Action action)
{
  for (const auto& chord : bindings[static_cast<size_t>(action)]) {
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
  Action                          action;
  std::string_view                key;
  std::string_view                fallback;
  Tier                            tier    = Tier::kAlways;
  std::array<std::string_view, 2> aliases = {};
};

// Order is the order of the debug log; the TOML output is sorted by key anyway.
constexpr Setting kSettings[] = {
#define SHORTCUT(action, ...) {Action::action, __VA_ARGS__},
#include "shortcuts.def"
#undef SHORTCUT
};
static_assert(std::size(kSettings) == static_cast<size_t>(Action::kCount));

// Binds every chord in `value` ("CTRL-G|F8"). Reports whether any was valid.
bool BindChords(std::vector<KeyChord>& chords, std::string_view key, std::string_view value)
{
  bool bound = false;
  for (const auto& text : StrSplit(value, '|')) {
    if (auto chord = KeyChord::Parse(text); chord.key != unity::KeyCode::None) {
      chords.push_back(std::move(chord));
      bound = true;
    } else {
      spdlog::warn("Invalid shortcut token [shortcuts].{} token='{}' value='{}'; ignoring token.", key, text, value);
    }
  }
  return bound;
}

// Binds one [shortcuts] setting and records the effective binding, and where it came from, in `out`.
void LoadSetting(const toml::table* shortcuts, toml::table& out, toml::table& out_source, const Setting& setting,
                 std::vector<KeyChord>& chords)
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

  if (!BindChords(chords, key, value)) {
    if (configured)
      spdlog::error("No valid shortcut tokens for [shortcuts].{} value='{}'; using default for [shortcuts].{}.", key,
                    *configured, setting.key);
    if (!configured || !BindChords(chords, setting.key, setting.fallback)) {
      spdlog::error("Default shortcut [shortcuts].{} value='{}' has no valid tokens; action disabled.", setting.key,
                    setting.fallback);
      return record("NONE", "invalid");
    }
    label = "default";
  }
  record(DescribeChords(chords), label);
}
} // namespace

KeyChord KeyChord::Parse(std::string_view text)
{
  KeyChord chord;
  for (auto& token : StrSplit(AsciiStrToUpper(text), '-')) {
    if (const auto* modifier = FindModifier(token)) {
      chord.modifiers.push_back(modifier);
    } else if (const auto key = FindKey(token)) {
      // A chord has one key; a later one replaces an earlier one, so the text shows what is bound.
      chord.key       = key->key;
      chord.key_name_ = std::move(token);
    }
  }
  return chord;
}

std::string KeyChord::Text() const
{
  std::string text;
  for (const auto* modifier : modifiers) {
    text += modifier->name;
    text += '-';
  }
  text += key_name_;
  if (key_name_.empty() && !text.empty())
    text.pop_back();
  return text;
}

std::string KeyChord::Hint() const
{
  std::string hint;
  for (const auto* modifier : modifiers)
    hint += modifier->hint;
  const auto key = FindKey(key_name_);
  hint += key && !key->hint.empty() ? key->hint : std::string_view(key_name_);
  return hint;
}

namespace Shortcut
{
Bindings Parse(const toml::table& config, toml::table& parsed, bool extended, bool experimental)
{
  Bindings    result;
  const auto* shortcuts = config["shortcuts"].as_table();
  auto&       out       = *parsed.emplace<toml::table>("shortcuts").first->second.as_table();
  auto&       source    = *parsed.emplace<toml::table>("shortcuts_source").first->second.as_table();

  for (const auto& setting : kSettings) {
    if ((setting.tier == Tier::kExtended && !extended)
        || (setting.tier == Tier::kExperimental && !(extended && experimental)))
      continue;
    LoadSetting(shortcuts, out, source, setting, result[static_cast<size_t>(setting.action)]);
  }
  return result;
}

void Set(Bindings next) noexcept
{
  bindings = std::move(next);
  used_keys.clear();
}

bool Owns(unity::KeyCode key)
{ return IsUsed(key); }

bool HasBinding(Action action)
{
  return std::ranges::any_of(bindings[static_cast<size_t>(action)],
                             [](const auto& chord) { return chord.key != unity::KeyCode::None; });
}

bool IsPressed(Action action)
{ return Matches<unity::Key::Pressed>(action); }

bool IsDown(Action action)
{ return Matches<unity::Key::Down>(action); }

std::string Describe(Action action)
{ return DescribeChords(bindings[static_cast<size_t>(action)]); }

std::string Hint(Action action)
{
  // The game's badges have room for one binding.
  const auto& chords = bindings[static_cast<size_t>(action)];
  return chords.empty() ? std::string{} : chords.front().Hint();
}
} // namespace Shortcut

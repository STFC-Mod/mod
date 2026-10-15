// Settings parsing and shortcut matching, run against production code with Unity input replaced by a fake.
#include "patches/chat_startup.h"
#include "patches/input.h"
#include "patches/parts/toast_text.h"
#include "patches/ship_shortcut_hint.h"
#include "patches/shortcuts.h"
#include "ship_name.h"
#include "str_utils.h"

#include <toml++/toml.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <source_location>

namespace
{
std::array<bool, static_cast<size_t>(KeyCode::Max)> held{};
int                                                 failures = 0;

void Check(bool condition, const char* what, std::source_location where = std::source_location::current())
{
  if (!condition) {
    std::cerr << where.file_name() << ":" << where.line() << ": " << what << '\n';
    ++failures;
  }
}

void Hold(std::initializer_list<KeyCode> keys)
{
  held.fill(false);
  for (auto key : keys)
    held[static_cast<size_t>(key)] = true;
}

toml::table Load(std::string_view settings, bool extended = true, bool experimental = false)
{
  Shortcut::Clear();
  toml::table parsed;
  Shortcut::Load(toml::parse(settings), parsed, extended, experimental);
  return parsed;
}

std::string Value(const toml::table& parsed, std::string_view section, std::string_view key)
{ return parsed[section][key].value_or(std::string("<missing>")); }

void ShipHints()
{
  Shortcut::Clear();
  // A locked dock F leaves native G at the sixth selectable position (the reported 1,2,3,4,5,7 layout).
  const std::array gapped_fleets{0, 1, 2, 3, 4, 6};
  auto             ship_hint = [&](GameFunction action) {
    return ShipShortcutHintAction(action, gapped_fleets.size(), [&](std::size_t i) { return gapped_fleets[i]; });
  };
  Check(ship_hint(GameFunction::SelectShip7) == GameFunction::SelectShip6,
        "Native ship G must advertise the sixth mod shortcut when dock F is locked");
  Check(ship_hint(GameFunction::SelectShip5) == GameFunction::SelectShip5, "Earlier ship hints must stay unchanged");
  Check(ship_hint(GameFunction::SelectShip6) == GameFunction::Max, "Missing dock must not advertise a shortcut");
  Check(ship_hint(GameFunction::ShowGalaxy) == GameFunction::ShowGalaxy, "Non-ship hint mapping changed");
  Check(ShipShortcutHintAction(GameFunction::SelectShip1, 0, [](std::size_t) { return 0; }) == GameFunction::Max,
        "Unavailable fleet context must not advertise a guessed ship shortcut");
  for (int index = 0; index < 8; ++index) {
    auto action = static_cast<GameFunction>(GameFunction::SelectShip1 + index);
    Check(ShipShortcutHintAction(action, 8, [](std::size_t i) { return static_cast<int>(i); }) == action,
          "Contiguous eight-dock hints must keep their original shortcuts");
  }
  const std::array reordered_fleets{7, 0, 2, -1, 6, 4, 1, 3, 5};
  auto             reordered_hint = [&](GameFunction action) {
    return ShipShortcutHintAction(action, reordered_fleets.size(), [&](std::size_t i) { return reordered_fleets[i]; });
  };
  Check(reordered_hint(GameFunction::SelectShip8) == GameFunction::SelectShip1, "Ship hints must follow bar order");
  Check(reordered_hint(GameFunction::SelectShip7) == GameFunction::SelectShip5,
        "Null fleet entries must preserve the positions passed to RequestSelect");
  Check(reordered_hint(GameFunction::SelectShip6) == GameFunction::Max,
        "Fleet beyond the eight supported mod shortcuts must not advertise a shortcut");
  Check(Shortcut::Hint(ship_hint(GameFunction::SelectShip7)).empty(),
        "Unbound ship selection must not advertise a working shortcut");
  Shortcut::Bind(GameFunction::SelectShip6, KeyChord::Parse("CTRL-F8"));
  Check(Shortcut::Hint(ship_hint(GameFunction::SelectShip7)) == "^F8",
        "Gapped ship hint must use the actual custom binding, not a hardcoded number");
}

void StringHelpers()
{
  Check(StrSplit(" a, ,b ,", ',') == std::vector<std::string>{"a", "b"}, "StrSplit trims and drops empty tokens");
  Check(StrSplit("", ',').empty(), "StrSplit of nothing");
  // A view into a longer string must not pick up the text after it.
  constexpr std::string_view padded = " warp  ";
  Check(AsciiStrToUpper(StripAsciiWhitespace(padded)) == "WARP", "upper-casing a trimmed view");
  Check(AsciiStrToLower("FeDeRaTiOn") == "federation", "lower-casing");
}

void Timestamps()
{
  Check(ParseUtcTimestamp("1970-01-01T00:00:00") == 0, "epoch");
  Check(ParseUtcTimestamp("2024-05-01T12:34:56Z") == 1714566896, "UTC with zone suffix");
  Check(ParseUtcTimestamp("2024-05-01T12:34:56.789Z") == 1714566896, "fraction ignored");
  Check(ParseUtcTimestamp("2024-02-30T00:00:00") == std::nullopt, "invalid date");
  Check(ParseUtcTimestamp("2024-05-01 12:34:56") == std::nullopt, "wrong separator");
  Check(ParseUtcTimestamp("") == std::nullopt, "empty");
}

void ToastText()
{
  using namespace toast_text;
  const std::vector<std::string> values{"Kirk", "7", ""};
  Check(FormatPlaceholders("{0} scored {1}", values) == "Kirk scored 7", "plain placeholders");
  Check(FormatPlaceholders("Lv.{1:000}", values) == "Lv.007", "zero-padded placeholder");
  Check(FormatPlaceholders("{0}{0}", values) == "KirkKirk", "repeated placeholder");
  Check(FormatPlaceholders("{10} and {1}", values) == "{10} and 7", "{1} does not match {10}");
  Check(FormatPlaceholders("{2} stays", values) == "{2} stays", "empty values are left alone");
  Check(FormatPlaceholders("{1:x2}", values) == "7", "other formats are filled unpadded");
  Check(StripRichText("<color=#FF0000>Red</color> <b>bold</b>") == "Red bold", "rich text tags removed");
  Check(StripRichText("a < b") == "a < b", "an unclosed '<' is text");
  Check(TitleCase("USS ENTERPRISE-D") == "Uss Enterprise-D", "title case");
  Check(HullKeyName("Hull_L30_Destroyer_Klingon_LIVE") == "Lv.30 Destroyer Klingon", "hull key with level");
  Check(HullKeyName("Hull_BORG_CUBE") == "Borg Cube", "hull key without level");
}

void ShipNames()
{
  Check(NormalizeShipName("  U.S.S. Enterprise-D ") == "ENTERPRISE D", "ship names drop USS and punctuation");
  Check(NormalizeShipName("d'deridex__b") == "DDERIDEX B", "ship names collapse separators");
  const auto academy = ShipNameWords("Academy Athena");
  Check(ShipNameMatches(academy, ShipNameWords("athena")), "a ship name matches its last word");
  Check(ShipNameMatches(academy, ShipNameWords("ACADEMY ATHENA")), "a ship name matches all its words");
  Check(!ShipNameMatches(academy, ShipNameWords("academy")), "a ship name does not match its first word");
  Check(!ShipNameMatches(academy, ShipNameWords("")), "an empty pattern matches nothing");
  Check(!ShipNameMatches(ShipNameWords("Thena"), ShipNameWords("Athena")), "words match whole");
}

void Chords()
{
  const auto chord = KeyChord::Parse("ctrl-shift-f1");
  Check(chord.key == KeyCode::F1, "chord key");
  Check(chord.modifiers.size() == 2, "chord modifiers");
  Check(chord.Text() == "CTRL-SHIFT-F1", "chord text");
  Check(chord.Hint() == "^+F1", "chord badge");
  Check(KeyChord::Parse("SHIFT-+").Hint() == "+PLS", "plus key as the primary key");
  Check(KeyChord::Parse("CTRL").key == KeyCode::None, "modifier alone has no key");
  Check(KeyChord::Parse("NONSENSE").key == KeyCode::None, "unknown key");
  Check(KeyChord::Parse("CTRL-A-B").key == KeyCode::B && KeyChord::Parse("CTRL-A-B").Text() == "CTRL-B",
        "with two keys the last counts, and the text shows it");
  Check(KeyChord::Parse("pgup-lshift").Hint() == "<+PU", "modifiers come first in the badge");
}

void Loading()
{
  auto parsed = Load(R"(
    [shortcuts]
    show_galaxy = "CTRL-G | F8"
    show_system = ""
    show_events = "BOGUS"
    quit = 42
    set_hotkeys_disble = "F3"
  )");
  Check(Shortcut::Describe(GameFunction::ShowGalaxy) == "CTRL-G | F8", "both alternatives bound");
  Check(Shortcut::Hint(GameFunction::ShowGalaxy) == "^G", "badge shows the first alternative");
  Check(Value(parsed, "shortcuts", "show_galaxy") == "CTRL-G | F8", "vars records the bindings");
  Check(Value(parsed, "shortcuts_source", "show_galaxy") == "config", "source is the settings file");

  Check(!Shortcut::HasBinding(GameFunction::ShowSystem), "an empty string disables the binding");
  Check(Value(parsed, "shortcuts", "show_system") == "NONE", "disabled binding recorded as NONE");

  Check(Shortcut::Describe(GameFunction::ShowEvents) == std::string("SHIFT-E"),
        "a binding with no valid key falls back to the default");
  Check(Value(parsed, "shortcuts_source", "show_events") == "default", "fallback recorded as default");
  Check(Shortcut::Describe(GameFunction::Quit) == std::string("F10"), "a non-string falls back too");

  Check(Shortcut::Describe(GameFunction::DisableHotKeys) == "F3", "deprecated alias still binds");
  Check(Value(parsed, "shortcuts_source", "set_hotkeys_disabled") == "alias:set_hotkeys_disble", "alias source");

  Check(Shortcut::HasBinding(GameFunction::ShowBookmarks), "extended shortcuts load when extended");
  Shortcut::Bind(GameFunction::Quit, KeyChord::Parse("F1"));
  Shortcut::Load(toml::table{}, parsed, true, false);
  Check(Shortcut::Describe(GameFunction::Quit) == "F10", "loading again replaces earlier bindings");
  Check(!Shortcut::HasBinding(GameFunction::ShowLookup), "experimental shortcuts need experimental");
  parsed = Load("", false);
  Check(!Shortcut::HasBinding(GameFunction::ShowBookmarks), "extended shortcuts are skipped otherwise");
  Check(Value(parsed, "shortcuts", "show_bookmarks") == "<missing>", "skipped shortcuts are not recorded");
}

void Matching()
{
  Load(""); // defaults: move_down = "S|DOWN", toggle_shortcut_hints = "HOME"

  Hold({KeyCode::DownArrow});
  Check(Shortcut::IsPressed(GameFunction::MoveDown), "default movement key");
  for (auto modifier : {KeyCode::LeftControl, KeyCode::RightShift, KeyCode::LeftAlt, KeyCode::LeftCommand}) {
    Hold({KeyCode::DownArrow, modifier});
    Check(!Shortcut::IsPressed(GameFunction::MoveDown), "a modified chord must not trigger the plain binding");
  }
  Hold({});
  Check(!Shortcut::IsPressed(GameFunction::MoveDown), "released key");

  // Releasing the modifier first must not turn a held CTRL-PGUP into PGUP.
  Load(""); // ui_scaleup = "PGUP", ui_scaleshipup = "CTRL-PGUP"
  Hold({KeyCode::LeftControl, KeyCode::PageUp});
  Check(Shortcut::IsPressed(GameFunction::UiShipScaleUp), "modified chord");
  Check(!Shortcut::IsPressed(GameFunction::UiScaleUp), "plain chord blocked while modified");
  Check(Shortcut::Owns(KeyCode::PageUp), "the modified chord owns its key");
  Hold({KeyCode::PageUp});
  Check(!Shortcut::IsPressed(GameFunction::UiScaleUp), "plain chord waits for the key's release");
  Hold({});
  Check(!Shortcut::Owns(KeyCode::PageUp), "releasing the key frees it");
  Hold({KeyCode::PageUp});
  Check(Shortcut::IsPressed(GameFunction::UiScaleUp), "plain chord after a release");

  // A modified movement binding keeps moving while its modifier is held.
  Shortcut::Bind(GameFunction::MoveDown, KeyChord::Parse("CTRL-DOWN"));
  Hold({KeyCode::LeftControl, KeyCode::DownArrow});
  Check(Shortcut::IsDown(GameFunction::MoveDown) && Shortcut::IsDown(GameFunction::MoveDown),
        "explicit movement chord keeps matching");
  held.fill(false);
}
} // namespace

// The fake Unity input that the shortcut code reads.
namespace Key
{
bool Pressed(KeyCode key)
{ return held[static_cast<size_t>(key)]; }
bool Down(KeyCode key)
{ return Pressed(key); }
bool IsModified()
{
  for (auto key : {KeyCode::LeftControl, KeyCode::RightControl, KeyCode::LeftShift, KeyCode::RightShift,
                   KeyCode::LeftAlt, KeyCode::RightAlt, KeyCode::LeftCommand, KeyCode::RightCommand})
    if (Pressed(key))
      return true;
  return false;
}
} // namespace Key

void ChatStartupGate()
{
  ChatStartup::Gate gate;
  // A slow initial load never opens, whatever the elapsed wall time.
  Check(!gate.Tick(false, false, 0) && !gate.Tick(false, false, 100), "chat gate waits while not ready");
  Check(!gate.Tick(true, false, 101) && !gate.Tick(true, false, 103.99), "chat gate waits three seconds");
  // A transition or lost readiness restarts the full settling period.
  Check(!gate.Tick(false, false, 104) && !gate.Tick(true, false, 110) && !gate.Tick(true, false, 112.99),
        "lost readiness restarts the chat gate");
  Check(gate.Tick(true, false, 113), "chat gate opens after three ready seconds");
  Check(!gate.Tick(true, false, 200), "chat gate opens once; a closed chat is not reopened");
  gate = {}; // Login or reload permits one new open.
  Check(!gate.Tick(true, false, 201) && gate.Tick(true, false, 204), "a reset chat gate opens again");
  gate = {};
  Check(!gate.Tick(false, true, 205) && !gate.Tick(true, false, 300) && !gate.Tick(true, false, 303),
        "chat already open consumes the gate, even before readiness");
}

int main()
{
  ShipHints();
  StringHelpers();
  Timestamps();
  ToastText();
  ShipNames();
  Chords();
  Loading();
  Matching();
  ChatStartupGate();
  if (failures)
    return EXIT_FAILURE;
  std::cout << "mod logic tests passed\n";
  return EXIT_SUCCESS;
}

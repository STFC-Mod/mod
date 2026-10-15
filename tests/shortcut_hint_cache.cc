// Link against the built mods library. Only Unity-dependent Key operations are stubbed;
// MapKey parsing, ModifierKey parsing, binding detection, and hint caching are production code.
#include "defaultconfig.h"
#include "patches/mapkey.h"
#include "patches/ship_shortcut_hint.h"
#include <array>

#include <cstdlib>
#include <iostream>

static std::array<bool, static_cast<int>(KeyCode::Max)> held{};
static KeyCode claimed = KeyCode::None;

KeyCode Key::Parse(std::string_view key)
{
  if (key == "F7") return KeyCode::F7;
  if (key == "F8") return KeyCode::F8;
  if (key == "G") return KeyCode::G;
  for (const auto [token, code] : {std::pair{"W", KeyCode::W}, {"A", KeyCode::A}, {"S", KeyCode::S},
                                   {"D", KeyCode::D}, {"UP", KeyCode::UpArrow}, {"DOWN", KeyCode::DownArrow},
                                   {"LEFT", KeyCode::LeftArrow}, {"RIGHT", KeyCode::RightArrow}})
    if (key == token) return code;
  return KeyCode::None;
}
bool Key::IsModifier(KeyCode) { return false; }
bool Key::Pressed(KeyCode key) { return held[static_cast<int>(key)]; }
bool Key::Down(KeyCode key) { return Key::Pressed(key); }
bool Key::IsModified() {
  for (auto key : {KeyCode::LeftControl, KeyCode::RightControl, KeyCode::LeftShift, KeyCode::LeftAlt, KeyCode::LeftCommand})
    if (Key::Pressed(key)) return true;
  return false;
}
void Key::ClaimDirectionalInput(KeyCode key) { claimed = key; }

void Check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

int main()
{
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
  Check(MapKey::GetShortcutHint(ship_hint(GameFunction::SelectShip7)).empty(),
        "Unbound ship selection must not advertise a working shortcut");
  MapKey::AddMappedKey(GameFunction::SelectShip6, MapKey::Parse("CTRL-F8"));
  MapKey::CacheShortcutHints();
  Check(MapKey::GetShortcutHint(ship_hint(GameFunction::SelectShip7)) == "^F8",
        "Gapped ship hint must use the actual custom binding, not a hardcoded number");

  constexpr auto toggle = GameFunction::ToggleShortcutHints;
  constexpr auto galaxy = GameFunction::ShowGalaxy;
  Check(!MapKey::HasBinding(toggle), "Absent binding must not enable hints");

  // Match config registration: only keys with a primary key are accepted.
  for (const auto* text : {"NONE", "", "INVALID", "CTRL"}) {
    auto parsed = MapKey::Parse(text);
    Check(parsed.Key == KeyCode::None, "Disabled/invalid binding acquired a key");
    if (parsed.Key != KeyCode::None) MapKey::AddMappedKey(toggle, std::move(parsed));
    Check(!MapKey::HasBinding(toggle), "Disabled/invalid binding enabled hints");
  }

  MapKey::AddMappedKey(galaxy, MapKey::Parse("CTRL-G"));
  MapKey::AddMappedKey(galaxy, MapKey::Parse("F8"));
  Check(MapKey::GetShortcutHint(galaxy).empty(), "Parsing must not populate hint cache");
  Check(MapKey::GetShortcuts(galaxy) == "CTRL-G | F8", "Normal shortcut labels changed");

  // The toggle may be parsed after bindings whose badges it enables.
  MapKey::AddMappedKey(toggle, MapKey::Parse("SHIFT-F7"));
  Check(MapKey::HasBinding(toggle), "Valid chord must enable hints");
  Check(MapKey::GetShortcutHint(toggle).empty(), "Binding registration must not populate hint cache");
  Check(MapKey::GetShortcutHint(galaxy).empty(), "Registering toggle must not format earlier bindings");
  MapKey::CacheShortcutHints();
  Check(MapKey::GetShortcutHint(galaxy) == "^G", "Cache must include earlier binding, first alternative only");
  Check(MapKey::GetShortcutHint(toggle) == "+F7", "Toggle badge missing");
  Check(MapKey::GetShortcutHint(GameFunction::ShowResearch).empty(), "Unbound action acquired a badge");
  MapKey::CacheShortcutHints();
  Check(MapKey::GetShortcutHint(galaxy) == "^G", "Repeated cache preparation changed label");
  // Pan uses the production shortcut matcher: both default alternatives work,
  // but adding a modifier must not also match the unmodified movement action.
  for (const auto [action, defaults] : {
           std::pair{GameFunction::MoveUp, DefaultConfig::Shortcuts::move_up},
           std::pair{GameFunction::MoveDown, DefaultConfig::Shortcuts::move_down},
           std::pair{GameFunction::MoveLeft, DefaultConfig::Shortcuts::move_left},
           std::pair{GameFunction::MoveRight, DefaultConfig::Shortcuts::move_right}}) {
    std::string_view remaining = defaults;
    while (!remaining.empty()) {
      auto end = remaining.find('|');
      auto binding = MapKey::Parse(std::string(remaining.substr(0, end)));
      Check(binding.Key != KeyCode::None, "Invalid movement default");
      MapKey::AddMappedKey(action, binding);
      held[static_cast<int>(binding.Key)] = true;
      Check(MapKey::IsPressed(action), "Default movement key did not match");
      for (auto modifier : {KeyCode::LeftControl, KeyCode::RightControl, KeyCode::LeftShift,
                             KeyCode::LeftAlt, KeyCode::LeftCommand}) {
        held[static_cast<int>(modifier)] = true;
        Check(!MapKey::IsPressed(action), "Modified chord leaked into plain movement");
        held[static_cast<int>(modifier)] = false;
      }
      held[static_cast<int>(binding.Key)] = false;
      Check(!MapKey::IsPressed(action), "Released movement key remained active");
      if (end == std::string_view::npos) break;
      remaining.remove_prefix(end + 1);
    }
  }
  // A modified movement binding must not claim its own direction. Non-movement
  // actions still claim it, preserving the existing hold-until-release guard.
  MapKey::AddMappedKey(GameFunction::MoveDown, MapKey::Parse("CTRL-DOWN"));
  held[static_cast<int>(KeyCode::LeftControl)] = true;
  held[static_cast<int>(KeyCode::DownArrow)] = true;
  claimed = KeyCode::None;
  Check(MapKey::IsDown(GameFunction::MoveDown) && MapKey::IsPressed(GameFunction::MoveDown),
        "Explicit movement chord did not match");
  Check(claimed == KeyCode::None, "Movement claimed and blocked itself");
  MapKey::AddMappedKey(toggle, MapKey::Parse("CTRL-DOWN"));
  Check(MapKey::IsDown(toggle) && claimed == KeyCode::DownArrow, "Other chord lost directional ownership");
  held.fill(false);
  std::cout << "Shortcut hint and movement binding tests passed\n";

}

# Star Trek Fleet Command - Community Mod

<p align="center">
  <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3">
  <img src="https://img.shields.io/github/sponsors/netniv" alt="Sponsorship">
</p>

<p align="center">
   A community mod (patch) that adds a couple of tweaks to the mobile game <b>Star Trek Fleet Command&#8482;</b>
</p>

## Valid shortcut key strings

Each game function can be assigned a shortcut KEY (with the exception of escape that is fixed).

To use a Modifier, combine it with a single KEY (and optionally other modifiers) separated by a minus symbol:

Value | Keys Required
--: | :--
"SHIFT-Q" | (Left Or Right) Shift + Q
"ALT-LSHIFT-T" | (Left Or Right) Alt + Left Shift + T
"Q" | Q (no Shift, Ctrl, etc)

### Screen shortcuts

These bindings can be changed in the `[shortcuts]` section of `community_patch_settings.toml`.

| Setting | Default | Action |
| --- | --- | --- |
| `show_officerpresets` | `CTRL-O` | Open Officer Presets. |
| `show_officerswap` | `ALT-O` | Open officer assignment for the selected ship. |
| `show_shipconstruction` | `SHIFT-N` | Open Ships / Ship Construction (Shipyard → Build Ship). |
| `show_shipswap` | `ALT-N` | Open Ship Swap for the selected ship. |
| `show_shields` | `CTRL-S` | Open the Peace Shield selection popup. |
| `show_battlelogs` | `SHIFT-B` | Open the Battle Reports inbox. |

`show_ships = "N"` continues to manage the selected ship. The shield shortcut opens the selection popup; activating a shield still requires choosing one.

### Modifiers

Modifiers are not required, any specified must be used together:

Value | Key | Value | Key | Value | Key
--: | :-- | --: | :-- | --: | :--
"SHIFT" | LeftShirt or RightShift | "LSHIFT" | LeftShift | "RSHIFT" | RightShift
"CTRL" | LeftControl or RightControl | "LCTRL" | LeftControl | "RCTRL" | RightControl
"ALT" | LeftAlt or RightAlt | "LALT" | LeftAlt | "RALT" | RightAlt
"WIN" | LeftWindows or RightWindows | "LWIN" | LeftWindows | "RWIN" | RightWindows
"ALTGR" | AltGr

### Keys

Only key can be set at one time:

Value | Key | Value | Key | Value | Key | Value | Key
--: | :-- | --: | :-- | --: | :-- | --: | :--
"HOME" | Home | "END" | End  | "PGUP" | PageUp | "PGDOWN" | PageDown
"LEFT" | LeftArrow | "RIGHT" | RightArrow | "UP" | UpArrow | "DOWN" | DownArrow
"BACKSPACE" | Backspace | "CLEAR" | Clear | "CAPS" | CapsLock | "BREAK" | Break
"INSERT" | Insert | "DELETE" | Delete | "HELP" | Help | "MENU" | Menu
"PAUSE" | Pause | "PRINT" | Print | "SPACE" | Space | "RETURN" | Return
"SCROLL" | ScrollLock | "SYSREQ" | SysReq | "TAB" | Tab | "SPACE" | Space
"MOUSE0" | Mouse0 | "MOUSE1" | Mouse1 | "MOUSE2" | Mouse2 | "MOUSE3" | Mouse3
"MOUSE4" | Mouse4 | "MOUSE5" | Mouse5 | "MOUSE6" | Mouse6 | "MINUS" | Minus |
"'" | Quote | "_" | Underscore | "," | Comma | "." | Period
";" | Semicolon | ":" | Colon | "!" | Exclaim | "?" | Question
"(" | LeftParen | ")" | RightParen | "[" | LeftBracket | "]" | RightBracket
"{" | LeftCurlyBracket | "}" | RightCurlyBracket | "@" | At | "*" | Asterisk
"/" | Slash | "\" | Backslash | "\"" | DoubleQuote | "&" | Ampersand
"#" | Hash | "%" | Percent | "`" | BackQuote | "^" | Caret
"+" | Plus | "<" | Less | "=" | Equals | ">" | Greater
"|" | Pipe | "~" | Tilde | "$" | Dollar | - | -
"F1" | F1 | "F2" | F2 | "F3" | F3 | "F4" | F4
"F5" | F5 | "F6" | F6 | "F7" | F7 | "F8" | F8
"F9" | F9 | "F10" | F10 | "F11" | F11 | "F12" | F12
"F13" | F13 | "F14" | F14 | "F15" | F15
"0" | Alpha0 | "1" | Alpha1 | "2" | Alpha2 | "3" | Alpha3
"4" | Alpha4 | "5" | Alpha5 | "6" | Alpha6 | "7" | Alpha7
"8" | Alpha8 | "9" | Alpha9
"A" | A | "B" | B | "C" | C | "D" | D
"E" | E | "F" | F | "G" | G | "H" | H
"I" | I | "J" | J | "K" | K | "L" | L
"M" | M | "N" | N | "O" | O | "P" | P
"Q" | Q | "R" | R | "S" | S | "T" | T
"U" | U | "V" | V | "W" | W | "X" | X
"Y" | Y | "Z" | Z
"KEY0" | Keypad0 | "KEY1" | Keypad1 | "KEY2" | Keypad2 | "KEY3" | Keypad3
"KEY4" | Keypad4 | "KEY5" | Keypad5 | "KEY6" | Keypad6 | "KEY7" | Keypad7
"KEY8" | Keypad8 | "KEY9" | Keypad9 | "KEYDIVIDE" | KeypadDivide | "KEYENTER" | KeypadEnter
"KEYEQUAL" | KeypadEquals | "KEYMINUS" | KeypadMinus | "KEYMULTI" | KeypadMultiply | "KEYPERIOD" | KeypadPeriod
"KEYPLUS" | KeypadPlus | "NUMLOCK" | Numlock

**NOTE**: Default shortcuts are shown in the [README.md](README.md)

## License

- GPLv3

## Camera movement

In mod-hotkey mode, system and galaxy panning uses `move_up`, `move_down`, `move_left`, and `move_right`.
Defaults are `W|UP`, `S|DOWN`, `A|LEFT`, and `D|RIGHT`. Existing configured values take precedence;
for example, `move_left = "LEFT"` keeps left-arrow-only movement until you add `A`.
Modifiers follow the normal shortcut rules, so `CTRL-S` does not also match plain `S` movement.
`disable_move_keys` disables keyboard panning; Scopely-hotkey mode retains native pan input.

#pragma once

// Text handling for toast notifications that does not touch the game, kept apart so it can be tested.

#include <algorithm>
#include <charconv>
#include <span>
#include <string>
#include <string_view>

namespace toast_text
{
// Replaces {N} with values[N]. The zero-padded form {N:000} pads the value to that width; other formats are
// filled without padding. Placeholders whose value is empty are left as they are.
inline std::string FormatPlaceholders(std::string_view text, std::span<const std::string> values)
{
  std::string result;
  result.reserve(text.size());
  for (auto open = text.find('{'); open != std::string_view::npos; open = text.find('{')) {
    result += text.substr(0, open);
    text.remove_prefix(open + 1);
    // An index without leading zeros, then '}' or ":format}". Digits hold no '}', so `close` is never before
    // `digits` and a found one leaves text[digits] in range.
    size_t index            = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), index);
    const auto digits       = static_cast<size_t>(end - text.data());
    const auto close        = text.find('}');
    if (error != std::errc{} || (digits > 1 && text[0] == '0') || close == std::string_view::npos
        || (text[digits] != '}' && text[digits] != ':') || index >= values.size() || values[index].empty()) {
      result += '{';
      continue;
    }
    const auto& value  = values[index];
    const auto  format = text[digits] == ':' ? text.substr(digits + 1, close - digits - 1) : std::string_view{};
    if (format.find_first_not_of('0') == std::string_view::npos && value.size() < format.size())
      result.append(format.size() - value.size(), '0');
    // Only the template is scanned; a player name containing "{1}" stays literal.
    result += value;
    text.remove_prefix(close + 1);
  }
  result += text;
  return result;
}

// Removes Unity rich text tags such as <color=#FF0000>, <b> and </size>. An unclosed '<' is text.
inline std::string StripRichText(std::string_view text)
{
  std::string result;
  result.reserve(text.size());
  for (auto open = text.find('<'); open != std::string_view::npos; open = text.find('<')) {
    const auto close = text.find('>', open);
    if (close == std::string_view::npos)
      break;
    result += text.substr(0, open);
    text.remove_prefix(close + 1);
  }
  result += text;
  return result;
}

// Upper-cases the first letter of each word and lower-cases the rest. Words split on space, '-' and '_'.
inline std::string TitleCase(std::string text)
{
  bool word_start = true;
  for (auto& c : text) {
    if (word_start && c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 'A');
    else if (!word_start && c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
    word_start = c == ' ' || c == '-' || c == '_';
  }
  return text;
}

// A readable name from a hull's internal key, for hulls the game cannot localize:
// "Hull_L30_Destroyer_Klingon_LIVE" -> "Lv.30 Destroyer Klingon".
inline std::string HullKeyName(std::string_view key)
{
  if (key.size() > 5 && key.ends_with("_LIVE"))
    key.remove_suffix(5);
  if (key.starts_with("Hull_"))
    key.remove_prefix(5);

  std::string name(key);
  std::ranges::replace(name, '_', ' ');
  if (name.size() >= 2 && name[0] == 'L' && name[1] >= '0' && name[1] <= '9')
    name = "Lv." + name.substr(1);
  return TitleCase(std::move(name));
}
} // namespace toast_text

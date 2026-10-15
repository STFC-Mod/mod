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
  for (size_t pos = 0; pos < text.size();) {
    if (text[pos] == '{') {
      const auto* first         = text.data() + pos + 1;
      size_t      index         = 0;
      const auto [after, error] = std::from_chars(first, text.data() + text.size(), index);
      if (error == std::errc{} && after != text.data() + text.size() && (*after == '}' || *after == ':')
          && !(first[0] == '0' && after != first + 1) && index < values.size() && !values[index].empty()) {
        const auto offset = static_cast<size_t>(after - text.data());
        const auto end    = text.find('}', offset);
        if (end == std::string_view::npos) {
          result += text.substr(pos);
          break;
        }
        const auto& value  = values[index];
        const auto  format = *after == ':' ? text.substr(offset + 1, end - offset - 1) : std::string_view{};
        if (format.find_first_not_of('0') == std::string_view::npos && value.size() < format.size())
          result.append(format.size() - value.size(), '0');
        // Only the template is scanned; a player name containing "{1}" stays literal.
        result += value;
        pos = end + 1;
        continue;
      }
    }
    result += text[pos++];
  }
  return result;
}

// Removes Unity rich text tags such as <color=#FF0000>, <b> and </size>.
inline std::string StripRichText(std::string_view text)
{
  std::string result;
  result.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '<') {
      if (const auto end = text.find('>', i); end != std::string_view::npos) {
        i = end + 1;
        continue;
      }
    }
    result += text[i++];
  }
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

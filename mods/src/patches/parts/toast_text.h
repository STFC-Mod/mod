#pragma once

// Text handling for toast notifications that does not touch the game, kept apart so it can be tested.

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

namespace toast_text
{
// Replaces {N} with values[N]. The zero-padded form {N:000} pads the value to that width; other formats are
// filled without padding. Placeholders whose value is empty are left as they are.
inline std::string FormatPlaceholders(std::string text, std::span<const std::string> values)
{
  for (size_t i = 0; i < values.size(); ++i) {
    const auto& value = values[i];
    if (value.empty())
      continue;
    const auto prefix = "{" + std::to_string(i);
    for (auto pos = text.find(prefix); pos != std::string::npos; pos = text.find(prefix, pos)) {
      const auto after = pos + prefix.size();
      const auto end   = text.find('}', after);
      if (end == std::string::npos)
        break;
      auto replacement = value;
      if (after != end) {
        // "{1" also starts "{10}"; only "{1}" and "{1:...}" are this placeholder.
        if (text[after] != ':') {
          pos = after;
          continue;
        }
        const auto format = std::string_view(text).substr(after + 1, end - after - 1);
        if (!format.empty() && format.find_first_not_of('0') == std::string_view::npos
            && replacement.size() < format.size())
          replacement.insert(0, format.size() - replacement.size(), '0');
      }
      text.replace(pos, end - pos + 1, replacement);
      pos += replacement.size();
    }
  }
  return text;
}

inline size_t CountPlaceholders(std::string_view text)
{ return static_cast<size_t>(std::ranges::count(text, '{')); }

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

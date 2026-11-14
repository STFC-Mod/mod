#pragma once

// Text helpers that never touch the game, so the tests can use them: ASCII case and trimming for settings, server
// timestamps, and ship name matching.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

constexpr std::string_view StripAsciiWhitespace(std::string_view str)
{
  constexpr std::string_view kSpace = " \t\n\r\f\v";
  while (!str.empty() && kSpace.contains(str.front()))
    str.remove_prefix(1);
  while (!str.empty() && kSpace.contains(str.back()))
    str.remove_suffix(1);
  return str;
}

constexpr std::string AsciiStrToUpper(std::string_view s)
{
  std::string str(s);
  for (auto& c : str)
    if (c >= 'a' && c <= 'z')
      c = static_cast<char>(c - 'a' + 'A');
  return str;
}

constexpr std::string AsciiStrToLower(std::string_view s)
{
  std::string str(s);
  for (auto& c : str)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return str;
}

// Splits on `delimiter`, trims each piece, and drops the empty ones: "a, ,b" -> {"a", "b"}.
constexpr std::vector<std::string> StrSplit(std::string_view input, char delimiter)
{
  std::vector<std::string> result;
  for (const auto piece : std::views::split(input, delimiter))
    if (const auto token = StripAsciiWhitespace(std::string_view(piece)); !token.empty())
      result.emplace_back(token);
  return result;
}

// Seconds since the Unix epoch for a "YYYY-MM-DDTHH:MM:SS" server timestamp, which is UTC. A fraction or
// zone suffix after the seconds is ignored.
inline std::optional<int64_t> ParseUtcTimestamp(std::string_view timestamp)
{
  using namespace std::chrono;
  // Field widths and the separator that follows each one.
  constexpr std::array<std::pair<size_t, char>, 6> kFields{{{4, '-'}, {2, '-'}, {2, 'T'}, {2, ':'}, {2, ':'}, {2, 0}}};
  std::array<int, 6>                               values{};
  for (size_t i = 0; i < kFields.size(); ++i) {
    const auto [width, separator] = kFields[i];
    if (timestamp.size() < width)
      return std::nullopt;
    const auto [end, error] = std::from_chars(timestamp.data(), timestamp.data() + width, values[i]);
    if (error != std::errc{} || end != timestamp.data() + width)
      return std::nullopt;
    timestamp.remove_prefix(width);
    if (separator) {
      if (timestamp.empty() || timestamp.front() != separator)
        return std::nullopt;
      timestamp.remove_prefix(1);
    }
  }
  const auto [year_, month_, day_, hour, minute, second] = values;
  const year_month_day date{std::chrono::year{year_}, std::chrono::month(static_cast<unsigned>(month_)),
                            std::chrono::day(static_cast<unsigned>(day_))};
  // 60 allows a leap second.
  if (!date.ok() || hour > 23 || minute > 59 || second > 60)
    return std::nullopt;
  const auto time = sys_days{date} + hours{hour} + minutes{minute} + seconds{second};
  return time.time_since_epoch() / seconds{1};
}

// Ship names for pinned_ships and the instant_warp_* filters are compared as words, so a setting can name a ship
// by its last words: "Athena" matches "Academy Athena". Names are normalized first: upper case, '_' and '-' as
// spaces, '.' and '\'' dropped, single spaces, no leading "USS": "U.S.S. Enterprise-D" -> "ENTERPRISE D".
inline std::string NormalizeShipName(std::string_view name)
{
  std::string normalized;
  normalized.reserve(name.size());
  for (char c : AsciiStrToUpper(name)) {
    if (c == '.' || c == '\'')
      continue;
    if (c == '_' || c == '-')
      c = ' ';
    if (c == ' ' && (normalized.empty() || normalized.back() == ' '))
      continue;
    normalized.push_back(c);
  }
  if (normalized.ends_with(' '))
    normalized.pop_back();
  if (normalized.starts_with("USS "))
    normalized.erase(0, 4);
  return normalized;
}

inline std::vector<std::string> ShipNameWords(std::string_view name)
{
  // Normalized names have single spaces and none at either end, so no word is empty.
  std::vector<std::string> words;
  for (const auto word : std::views::split(NormalizeShipName(name), ' '))
    words.emplace_back(std::string_view(word));
  return words;
}

// Whether `pattern` is the last words of `name`; both from ShipNameWords.
inline bool ShipNameMatches(std::span<const std::string> name, std::span<const std::string> pattern)
{ return !pattern.empty() && std::ranges::ends_with(name, pattern); }

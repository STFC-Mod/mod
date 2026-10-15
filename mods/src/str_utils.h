#pragma once

// ASCII string helpers for config parsing and UTF-16 conversion of managed strings.

#include <il2cpp/il2cpp.h>
#include <simdutf.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

constexpr bool IsAsciiSpace(char c)
{ return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

constexpr std::string_view StripTrailingAsciiWhitespace(std::string_view str)
{
  while (!str.empty() && IsAsciiSpace(str.back()))
    str.remove_suffix(1);
  return str;
}

constexpr std::string_view StripLeadingAsciiWhitespace(std::string_view str)
{
  while (!str.empty() && IsAsciiSpace(str.front()))
    str.remove_prefix(1);
  return str;
}

constexpr std::string_view StripAsciiWhitespace(std::string_view str)
{ return StripTrailingAsciiWhitespace(StripLeadingAsciiWhitespace(str)); }

constexpr std::string_view StripPrefix(std::string_view str, std::string_view prefix)
{ return str.starts_with(prefix) ? str.substr(prefix.size()) : str; }

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
  while (true) {
    const auto end   = input.find(delimiter);
    const auto token = StripAsciiWhitespace(input.substr(0, end));
    if (!token.empty())
      result.emplace_back(token);
    if (end == std::string_view::npos)
      return result;
    input.remove_prefix(end + 1);
  }
}

// Compares a managed string to an ASCII literal without converting it; for checks on hot paths.
inline bool StringIs(const Il2CppString* str, std::string_view ascii)
{
  if (!str || static_cast<size_t>(str->length) != ascii.size())
    return false;
  for (size_t i = 0; i < ascii.size(); ++i)
    if (str->chars[i] != static_cast<Il2CppChar>(ascii[i]))
      return false;
  return true;
}

inline std::string to_string(const Il2CppString* str)
{
  if (!str)
    return {};
  const auto* chars = reinterpret_cast<const char16_t*>(str->chars);
  std::string result(simdutf::utf8_length_from_utf16(chars, str->length), '\0');
  result.resize(simdutf::convert_utf16_to_utf8(chars, str->length, result.data()));
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
  if (!date.ok() || hour > 23 || minute > 59 || second > 60)
    return std::nullopt;
  const auto time = sys_days{date} + hours{hour} + minutes{minute} + seconds{second};
  return time.time_since_epoch() / seconds{1};
}

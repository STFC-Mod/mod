#pragma once

// ASCII string helpers for config parsing and UTF-16 conversion of managed strings.

#include <il2cpp/il2cpp.h>
#include <simdutf.h>

#include <array>
#include <charconv>
#include <chrono>
#include <ctime>
#include <sstream>
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

inline std::string to_string(const Il2CppString* str)
{
  if (!str)
    return {};
  const auto* chars = reinterpret_cast<const char16_t*>(str->chars);
  std::string result(simdutf::utf8_length_from_utf16(chars, str->length), '\0');
  result.resize(simdutf::convert_utf16_to_utf8(chars, str->length, result.data()));
  return result;
}

inline std::optional<std::chrono::time_point<std::chrono::system_clock>> parse_timestamp(const std::string& timestamp)
{
#ifdef _WIN32
  std::istringstream ss(timestamp);
  std::chrono::system_clock::time_point time_point;

  if (!std::chrono::from_stream(ss, "%Y-%m-%dT%H:%M:%S", time_point)) {
    return std::nullopt;
  }

  return time_point;
#else
  std::tm tm = {};
  if (strptime(timestamp.c_str(), "%Y-%m-%dT%H:%M:%S", &tm) == nullptr) {
    return std::nullopt;
  }

  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
#endif
}

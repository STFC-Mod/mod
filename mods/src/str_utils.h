#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <simdutf.h>
#include <il2cpp/il2cpp_helper.h>

#if _WIN32
#include <winrt/base.h>
#else
#include <time.h>
#endif

inline bool ascii_isspace(unsigned char c)
{
  return std::isspace(c);
}

constexpr std::string_view StripTrailingAsciiWhitespace(const std::string_view str)
{
  const auto it = std::find_if_not(str.rbegin(), str.rend(), ascii_isspace);
  return str.substr(0, static_cast<size_t>(str.rend() - it));
}

constexpr std::string_view StripLeadingAsciiWhitespace(const std::string_view str)
{
  const auto it = std::ranges::find_if_not(str, ascii_isspace);
  return str.substr(static_cast<size_t>(it - str.begin()));
}

constexpr std::string_view StripAsciiWhitespace(const std::string_view str)
{
  return StripTrailingAsciiWhitespace(StripLeadingAsciiWhitespace(str));
}

constexpr std::string_view StripSuffix(std::string_view str, const std::string_view suffix)
{
  if (str.size() >= suffix.size() && str.substr(str.size() - suffix.size()) == suffix) {
    return str.substr(0, str.size() - suffix.size());
  }
  return str;
}

constexpr std::string_view StripPrefix(std::string_view str, const std::string_view prefix)
{
  if (str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix) {
    return str.substr(prefix.size());
  }
  return str;
}

constexpr std::string AsciiStrToUpper(const std::string_view s)
{
  std::string str = s.data();
  std::ranges::transform(str, str.begin(), ::toupper);
  return str;
}

constexpr std::string AsciiStrToLower(const std::string_view s)
{
  std::string str = s.data();
  std::ranges::transform(str, str.begin(), ::tolower);
  return str;
}

constexpr std::vector<std::string> StrSplit(const std::string& input, const char delimiter)
{
  std::vector<std::string> result;
  int                      last_pos = 0;
  for (int i = 0; i < input.length(); i++) {
    if (input[i] != delimiter) {
      continue;
    }

    if (i - last_pos > 0) {
      result.emplace_back(input.substr(last_pos, i - last_pos));
    }
    last_pos = i + 1;
  }

  if (last_pos != input.length()) {
    auto sp = input.substr(last_pos, input.length() - last_pos);
    sp      = StripAsciiWhitespace(sp);
    if (!sp.empty()) {
      result.emplace_back(sp);
    }
  }

  return result;
}

inline std::wstring to_wstring(const std::string& str)
{
#if _WIN32
  return winrt::to_hstring(str).c_str();
#else
  size_t                      expected_utf32words = simdutf::utf32_length_from_utf8(str.data(), str.length());
  std::unique_ptr<char32_t[]> utf32_output{new char32_t[expected_utf32words]};
  size_t                      utf16words = simdutf::convert_utf8_to_utf32(str.data(), str.length(), utf32_output.get());
  return std::wstring(utf32_output.get(), utf32_output.get() + utf16words);
#endif
}

inline std::wstring to_wstring(Il2CppString* str)
{
#if _WIN32
  return str->chars;
#else
  size_t                      expected_utf32words = simdutf::utf32_length_from_utf16(str->chars, str->length);
  std::unique_ptr<char32_t[]> utf32_output{new char32_t[expected_utf32words]};
  size_t                      utf16words = simdutf::convert_utf16_to_utf32(str->chars, str->length, utf32_output.get());
  return std::wstring(utf32_output.get(), utf32_output.get() + utf16words);
#endif
}

inline std::string to_string(const std::wstring& str)
{
#if _WIN32
  size_t                     expected_utf16words = simdutf::utf8_length_from_utf16((char16_t*)str.data(), str.length());
  std::unique_ptr<char8_t[]> utf8_output{new char8_t[expected_utf16words]};
  size_t utf16words = simdutf::convert_utf16_to_utf8((char16_t*)str.data(), str.length(), (char*)utf8_output.get());
  return {utf8_output.get(), utf8_output.get() + utf16words};
#else
  size_t                     expected_utf32words = simdutf::utf8_length_from_utf32((char32_t*)str.data(), str.length());
  std::unique_ptr<char8_t[]> utf32_output{new char8_t[expected_utf32words]};
  size_t utf16words = simdutf::convert_utf32_to_utf8((char32_t*)str.data(), str.length(), (char*)utf32_output.get());
  return std::string(utf32_output.get(), utf32_output.get() + utf16words);
#endif
}

inline std::string to_string(const Il2CppString* str)
{
  size_t expected_utf8_bytes = simdutf::utf8_length_from_utf16(
    reinterpret_cast<const char16_t*>(str->chars), str->length);
  std::string result(expected_utf8_bytes, '\0');
  size_t actual_utf8_bytes = simdutf::convert_utf16_to_utf8(
    reinterpret_cast<const char16_t*>(str->chars), str->length, result.data());
  result.resize(actual_utf8_bytes);
  return result;
}

inline bool operator==(const Il2CppString* lhs, const std::string& rhs)
{
  return to_string(lhs) == rhs;
}

inline bool operator==(const std::string& lhs, const Il2CppString* rhs)
{
  return lhs == to_string(rhs);
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

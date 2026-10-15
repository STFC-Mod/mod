#pragma once

// Ship name matching for pinned_ships and the instant_warp_* filters. Names are compared as words, so a setting can
// name a ship by its last words: "Athena" matches "Academy Athena".

#include "str_utils.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Upper case, '_' and '-' as spaces, '.' and '\'' dropped, single spaces, no leading "USS":
// "U.S.S. Enterprise-D" -> "ENTERPRISE D".
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
  if (!normalized.empty() && normalized.back() == ' ')
    normalized.pop_back();
  return std::string(StripPrefix(normalized, "USS "));
}

inline std::vector<std::string> ShipNameWords(std::string_view name)
{
  std::vector<std::string> words;
  const auto               normalized = NormalizeShipName(name);
  for (size_t start = 0; start < normalized.size();) {
    const auto end = std::min(normalized.find(' ', start), normalized.size());
    words.emplace_back(normalized.substr(start, end - start));
    start = end + 1;
  }
  return words;
}

// Whether `pattern` is the last words of `name`; both from ShipNameWords.
inline bool ShipNameMatches(std::span<const std::string> name, std::span<const std::string> pattern)
{
  return !pattern.empty() && name.size() >= pattern.size()
         && std::ranges::equal(name.subspan(name.size() - pattern.size()), pattern);
}

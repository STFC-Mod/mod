#pragma once

#include "str_utils.h"

#include <il2cpp/il2cpp.h>

#include <prime/fleet.h>
#include <prime/ui.h>

#include <string>
#include <string_view>
#include <vector>

namespace ShipNameMatch {

inline std::string NormalizeKey(std::string_view raw)
{
  auto upper = AsciiStrToUpper(raw);

  std::string cleaned;
  cleaned.reserve(upper.size());
  for (const char c : upper) {
    if (c == '_' || c == '-') {
      cleaned.push_back(' ');
    } else if (c == '.' || c == '\'') {
      continue; // drop, don't split: "U.S.S." -> "USS", not "U S S"
    } else {
      cleaned.push_back(c);
    }
  }

  std::string collapsed;
  collapsed.reserve(cleaned.size());
  bool prev_space = false;
  for (const char c : cleaned) {
    const bool is_space = (c == ' ');
    if (is_space && prev_space) continue;
    collapsed.push_back(c);
    prev_space = is_space;
  }

  auto trimmed = StripAsciiWhitespace(collapsed);
  return std::string(StripPrefix(trimmed, "USS "));
}

inline std::vector<std::string> SplitWords(std::string_view key)
{
  std::vector<std::string> words;
  std::string              current;
  for (const char c : key) {
    if (c == ' ') {
      if (!current.empty()) {
        words.push_back(std::move(current));
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) words.push_back(std::move(current));
  return words;
}

// True if `pinned` is a trailing (suffix) subsequence of `candidate`'s words,
// e.g. pinned=["ATHENA"] matches candidate=["ACADEMY","ATHENA"].
inline bool EndsWithWords(const std::vector<std::string>& candidate, const std::vector<std::string>& pinned)
{
  if (pinned.empty() || candidate.size() < pinned.size()) return false;
  const size_t offset = candidate.size() - pinned.size();
  for (size_t i = 0; i < pinned.size(); ++i) {
    if (candidate[offset + i] != pinned[i]) return false;
  }
  return true;
}

inline std::string LocalizedShipName(int64_t loca_id)
{
  if (loca_id <= 0) return {};

  auto* id_str  = il2cpp_string_new(("ship_name_" + std::to_string(loca_id)).c_str());
  auto* cat_str = il2cpp_string_new("ships");
  auto* ctx     = il2cpp::New<LocaleTextContext>(id_str, cat_str);
  if (!ctx) return {};

  if (const auto* str = Localizer::Localise(ctx); str != nullptr) {
    return to_string(str);
  }
  return {};
}

inline std::string GameDisplayName(FleetPlayerData* ship)
{
  return ship && ship->Hull() ? LocalizedShipName(ship->GetLocaId()) : std::string{};
}

inline std::vector<std::string> DisplayWords(FleetPlayerData* ship)
{
  if (!ship) return {};

  return SplitWords(NormalizeKey(GameDisplayName(ship)));
}

inline bool MatchesDisplay(const std::vector<std::string>& display_words, const std::vector<std::string>& pinned_words)
{
  return EndsWithWords(display_words, pinned_words);
}

} // namespace ShipNameMatch

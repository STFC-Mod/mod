#include "config.h"
#include "prime/Toast.h"
#include "ship_name_match.h"
#include "str_utils.h"
#include <algorithm>
#include <utility>

static const std::pair<const char*, int> bannerTypes[] = {
    {"All", ToastState::All},
    {"Standard", ToastState::Standard},
    {"FactionWarning", ToastState::FactionWarning},
    {"FactionLevelUp", ToastState::FactionLevelUp},
    {"FactionLevelDown", ToastState::FactionLevelDown},
    {"FactionDiscovered", ToastState::FactionDiscovered},
    {"IncomingAttackFaction", ToastState::IncomingAttackFaction},
    {"FleetBattle", ToastState::FleetBattle},
    {"Victory", ToastState::Victory},
    {"Defeat", ToastState::Defeat},
    {"Event", ToastState::Tournament},
    {"ArmadaCreated", ToastState::ArmadaCreated},
    {"ArmadaCanceled", ToastState::ArmadaCanceled},
    {"ArmadaIncomingAttack", ToastState::ArmadaIncomingAttack},
    {"ArmadaBattleWon", ToastState::ArmadaBattleWon},
    {"ArmadaBattleLost", ToastState::ArmadaBattleLost},
    {"DiplomacyUpdated", ToastState::DiplomacyUpdated},
    {"JoinedTakeover", ToastState::JoinedTakeover},
    {"CompetitorJoinedTakeover", ToastState::CompetitorJoinedTakeover},
    {"AbandonedTerritory", ToastState::AbandonedTerritory},
    {"TakeoverVictory", ToastState::TakeoverVictory},
    {"TakeoverDefeat", ToastState::TakeoverDefeat},
    {"TreasuryProgress", ToastState::TreasuryProgress},
    {"TreasuryFull", ToastState::TreasuryFull},
    {"Achievement", ToastState::Achievement},
    {"AssaultVictory", ToastState::AssaultVictory},
    {"AssaultDefeat", ToastState::AssaultDefeat},
    {"ChallengeComplete", ToastState::ChallengeComplete},
    {"ChallengeFailed", ToastState::ChallengeFailed},
    {"StrikeHit", ToastState::StrikeHit},
    {"StrikeDefeat", ToastState::StrikeDefeat},
    {"WarchestProgress", ToastState::WarchestProgress},
    {"WarchestFull", ToastState::WarchestFull},
    {"PartialVictory", ToastState::PartialVictory},
    {"ArenaTimeLeft", ToastState::ArenaTimeLeft},
    {"ChainedEventScored", ToastState::ChainedEventScored},
    {"FleetPresetApplied", ToastState::FleetPresetApplied},
    {"SurgeWarmUpEnded", ToastState::SurgeWarmUpEnded},
    {"SurgeHostileGroupDefeated", ToastState::SurgeHostileGroupDefeated},
    {"SurgeTimeLeft", ToastState::SurgeTimeLeft},
};

bool ParseShipFilter(std::string_view value, std::vector<std::string>& names, bool& match_all)
{
  names.clear();
  match_all = false;
  if (AsciiStrToUpper(StripAsciiWhitespace(value)) == "*") {
    match_all = true;
    return true;
  }
  bool valid = true;
  for (const auto& token : StrSplit(std::string(value), ',')) {
    auto stripped = StripAsciiWhitespace(token);
    if (stripped.empty())
      continue;
    if (auto normalized = ShipNameMatch::NormalizeKey(stripped); !normalized.empty() && stripped != "*") {
      names.emplace_back(std::move(normalized));
    } else {
      valid = false;
    }
  }
  return valid;
}

bool ParseFactionFilter(std::string_view value, std::vector<std::string>& factions)
{
  static constexpr std::array kKnownFactions{"federation", "klingon", "romulan"};

  factions.clear();
  bool valid = true;
  for (const auto& token : StrSplit(std::string(value), ',')) {
    const auto stripped = StripAsciiWhitespace(token);
    if (stripped.empty())
      continue;

    auto lowered = AsciiStrToLower(stripped);

    if (std::ranges::find(kKnownFactions, lowered) == kKnownFactions.end()) {
      valid = false;
      continue;
    }

    if (std::ranges::find(factions, lowered) == factions.end()) {
      factions.emplace_back(std::move(lowered));
    }
  }
  return valid;
}

bool ParseBannerFilter(std::string_view value, std::vector<int>& types)
{
  types.clear();
  bool valid = true;
  for (const auto& token : StrSplit(std::string(value), ',')) {
    const auto name = AsciiStrToUpper(StripAsciiWhitespace(token));
    if (name.empty())
      continue;
    bool found = false;
    for (const auto& [key, type] : bannerTypes) {
      if (name == "ALL" || name == AsciiStrToUpper(key)) {
        if (std::ranges::find(types, type) == types.end())
          types.push_back(type);
        found = true;
      }
    }
    valid &= found;
  }
  return valid;
}

std::string FormatBannerFilter(const std::vector<int>& types)
{
  std::string result;
  for (const auto& [key, type] : bannerTypes)
    if (std::ranges::find(types, type) != types.end()) {
      if (!result.empty())
        result += ", ";
      result += key;
    }
  return result;
}

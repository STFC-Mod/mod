#include "config.h"

#include "defaultconfig.h"
#include "file.h"
#include "patches/shortcuts.h"
#include "ship_name_match.h"
#include "str_utils.h"
#include "version.h"

#include <prime/ui.h>

#include <spdlog/spdlog.h>
#include <toml++/toml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <ranges>
#include <tuple>

namespace
{
namespace DCP  = DefaultConfig::Patches;
namespace DCA  = DefaultConfig::Audio;
namespace DCG  = DefaultConfig::Graphics;
namespace DCC  = DefaultConfig::Control;
namespace DCU  = DefaultConfig::UI;
namespace DCBS = DefaultConfig::Buffs;
namespace DCS  = DefaultConfig::Sync;
namespace DCSC = DefaultConfig::SystemConfig;

// Names of ToastState values as written in disabled_banner_types and notify_banner_types.
constexpr std::pair<std::string_view, ToastState> kBannerTypes[] = {
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

template <typename E> struct Choice {
  std::string_view name;
  E                value;
};

// The first name listed for a value is the one written back; later ones are accepted synonyms.
constexpr Choice<MissionHudVisibility> kHudVisibility[] = {
    {"auto", MissionHudVisibility::Auto},
    {"always", MissionHudVisibility::Always},
    {"never", MissionHudVisibility::Never},
    {"", MissionHudVisibility::Auto},
};

constexpr Choice<InstantWarpConfirmation> kInstantWarp[] = {
    {"none", InstantWarpConfirmation::None},  {"warp", InstantWarpConfirmation::Warp},
    {"jump", InstantWarpConfirmation::Jump},  {"left", InstantWarpConfirmation::Warp},
    {"right", InstantWarpConfirmation::Jump}, {"", InstantWarpConfirmation::None},
};

constexpr Choice<FleetLabelDetail> kFleetLabelDetail[] = {
    {"native", FleetLabelDetail::Native},    {"expanded", FleetLabelDetail::Expanded},
    {"compact", FleetLabelDetail::Compact},  {"threshold", FleetLabelDetail::Threshold},
    {"always", FleetLabelDetail::Expanded},  {"never", FleetLabelDetail::Compact},
    {"custom", FleetLabelDetail::Threshold}, {"auto", FleetLabelDetail::Native},
    {"none", FleetLabelDetail::Native},      {"off", FleetLabelDetail::Native},
    {"", FleetLabelDetail::Native},
};

std::string MaskToken(const std::string& token)
{
  if (token.size() <= 21)
    return token;
  auto masked = token;
  for (size_t i = 9; i < token.size() - 12; ++i)
    if (masked[i] != '-')
      masked[i] = '*';
  return masked;
}

// Reads settings from the user's file and records every effective value in `parsed`, which is written out as the
// runtime vars file so users can see what the mod actually used.
class Reader
{
public:
  Reader(const toml::table& config, toml::table& parsed, bool log)
      : config_(config)
      , parsed_(parsed)
      , log_(log)
  {
  }

  const toml::table& Config() const
  { return config_; }

  toml::table& Section(std::string_view name)
  { return *parsed_.emplace<toml::table>(name).first->second.as_table(); }

  // The configured value converted to T, or `fallback` when missing or of the wrong type. TOML integers read fine
  // into float settings; fractions into int settings do not.
  template <typename T> T Get(std::string_view section, std::string_view key, T fallback)
  {
    T    value = fallback;
    auto node  = config_[section][key];
    if (auto read = node.template value<T>())
      value = *read;
    else if (node)
      spdlog::warn("invalid config value {}.{}; using {}", section, key, fallback);
    Record(section, key, value);
    return value;
  }

  std::string Get(std::string_view section, std::string_view key, const char* fallback)
  { return Get<std::string>(section, key, fallback); }

  // A setting that was renamed: `key`, or `old_key` when only the old name is set. The vars file records `key`.
  template <typename T>
  T GetRenamed(std::string_view section, std::string_view key, std::string_view old_key, T fallback)
  {
    if (!config_[section][key] && config_[section][old_key]) {
      T value = fallback;
      if (auto read = config_[section][old_key].template value<T>())
        value = *read;
      else
        spdlog::warn("invalid config value {}.{}; using {}", section, old_key, fallback);
      Record(section, key, value);
      return value;
    }
    return Get(section, key, fallback);
  }

  // A case-insensitive choice from a fixed list of names.
  template <typename E, size_t N>
  E Choose(std::string_view section, std::string_view key, std::string_view fallback, const Choice<E> (&choices)[N])
  {
    const auto raw        = Get<std::string>(section, key, std::string(fallback));
    const auto normalized = AsciiStrToLower(StripAsciiWhitespace(raw));
    auto       found      = std::ranges::find(choices, normalized, &Choice<E>::name);
    if (found == std::end(choices)) {
      spdlog::warn("invalid config value {}.{}: '{}'; using {}", section, key, raw, fallback);
      found = std::ranges::find(choices, fallback, &Choice<E>::name);
      if (found == std::end(choices)) // a default missing from the list is a programmer mistake; take the first
        found = std::begin(choices);
    }
    Record(section, key, std::string(std::ranges::find(choices, found->value, &Choice<E>::value)->name));
    return found->value;
  }

  template <typename T> void Record(std::string_view section, std::string_view key, const T& value)
  {
    Section(section).insert_or_assign(key, value);
    if (log_)
      spdlog::debug("config value {}.{} value: {}", section, key, value);
  }

private:
  const toml::table& config_;
  toml::table&       parsed_;
  bool               log_;
};

// Sync options renamed since; their old names are still read, per target as well.
constexpr std::pair<SyncConfig::Type, std::string_view> kRenamedSyncOptions[] = {
    {SyncConfig::Type::Officer, "officer"}};

std::string_view OldName(const SyncConfig::Option& option)
{
  const auto it =
      std::ranges::find(kRenamedSyncOptions, option.type, &std::pair<SyncConfig::Type, std::string_view>::first);
  return it == std::end(kRenamedSyncOptions) ? std::string_view{} : it->second;
}

// "*" matches every ship; otherwise a comma list of ship names, normalized for ShipNameMatch.
void ReadShipFilter(Reader& reader, std::string_view key, const char* fallback, std::vector<std::string>& names,
                    bool& match_all)
{
  const auto value = reader.Get("ui", key, fallback);
  names.clear();
  match_all = StripAsciiWhitespace(value) == "*";
  if (match_all)
    return;
  for (const auto& token : StrSplit(value, ','))
    if (auto normalized = ShipNameMatch::NormalizeKey(token); !normalized.empty())
      names.push_back(std::move(normalized));
}

std::vector<std::string> ReadFactions(Reader& reader)
{
  static constexpr std::string_view kKnownFactions[] = {"federation", "klingon", "romulan"};

  std::vector<std::string> factions;
  auto                     value = reader.Get("ui", "daily_bulk_claim_factions", DCU::daily_bulk_claim_factions);
  if (StripAsciiWhitespace(value).empty()) {
    value = DCU::daily_bulk_claim_factions;
    reader.Section("ui").insert_or_assign("daily_bulk_claim_factions", value);
  }
  bool none = false;
  for (const auto& token : StrSplit(value, ',')) {
    auto faction = AsciiStrToLower(token);
    if (faction == "none") {
      none = true;
      continue;
    }
    if (std::ranges::find(kKnownFactions, faction) == std::end(kKnownFactions)) {
      spdlog::warn("Unrecognised faction '{}' in daily_bulk_claim_factions; expected one of: federation, klingon, "
                   "romulan, or none by itself. Ignoring.",
                   token);
    } else if (std::ranges::find(factions, faction) == factions.end()) {
      factions.push_back(std::move(faction));
    }
  }
  if (none) {
    if (!factions.empty())
      spdlog::warn("daily_bulk_claim_factions: none overrides named factions; use none by itself.");
    factions = {"none"};
  }
  return factions;
}

// A comma list of banner names, or "All". The vars file gets the names that matched.
std::vector<int> ReadBannerTypes(Reader& reader, std::string_view key, const char* fallback)
{
  std::vector<std::string> wanted;
  for (const auto& token : StrSplit(reader.Get("ui", key, fallback), ','))
    wanted.push_back(AsciiStrToUpper(token));
  const bool all = std::erase(wanted, "ALL") > 0;

  std::vector<int> types;
  std::string      names;
  for (const auto& [name, state] : kBannerTypes) {
    if (!all && std::erase(wanted, AsciiStrToUpper(name)) == 0)
      continue;
    types.push_back(state);
    names += names.empty() ? "" : ", ";
    names += name;
  }
  for (const auto& unknown : wanted)
    spdlog::warn("Unknown banner type '{}' in ui.{}", unknown, key);

  reader.Record("ui", key, names);
  return types;
}

void ReadSyncTargets(Reader& reader, std::map<std::string, SyncTargetConfig>& sync_targets, const SyncConfig& defaults)
{
  // The built-in targets are listed in the vars file as examples; they only sync once given a token.
  auto& parsed_targets =
      *reader.Section("sync")
           .insert_or_assign("targets",
                             toml::table{
                                 {"stfcdata", toml::table{{"token", ""}, {"url", DCS::stfcdata_url}}},
                                 {"nextspocksclub", toml::table{{"token", ""}, {"url", DCS::nextspocksclub_url}}},
                                 {"spocksclub", toml::table{{"token", ""}, {"url", DCS::spocksclub_url}}},
                             })
           .first->second.as_table();

  const auto* targets = reader.Config()["sync"]["targets"].as_table();
  if (!targets)
    return;

  for (const auto& [name, node] : *targets) {
    const auto* values = node.as_table();
    if (!values)
      continue;

    const auto section = "sync.targets." + std::string(name.str());
    if (!values->contains("url") || !values->contains("token")) {
      spdlog::warn("Skipping invalid target [{}]. Missing url or token.", section);
      continue;
    }
    const auto url   = (*values)["url"].value<std::string>();
    const auto token = (*values)["token"].value<std::string>();
    if (!url || !token || url->empty() || token->empty())
      continue;

    SyncTargetConfig target;
    target.url        = *url;
    target.token      = *token;
    target.proxy      = (*values)["proxy"].value<std::string>().value_or(defaults.proxy);
    target.verify_ssl = (*values)["verify_ssl"].value<bool>().value_or(defaults.verify_ssl);

    toml::table parsed{
        {"url", target.url}, {"token", target.token}, {"proxy", target.proxy}, {"verify_ssl", target.verify_ssl}};
    for (const auto& option : SyncOptions) {
      const auto key =
          !values->contains(option.option_str) && !OldName(option).empty() ? OldName(option) : option.option_str;
      target.*option.option = (*values)[key].value<bool>().value_or(defaults.*option.option);
      parsed.insert(option.option_str, target.*option.option);
    }

    if (sync_targets.emplace(name.str(), target).second) {
      parsed_targets.insert_or_assign(name.str(), std::move(parsed));
      spdlog::debug("config value {} url: {}, token: {}", section, target.url, MaskToken(target.token));
      spdlog::info("target [{}] proxy: '{}', verify_ssl: {}", section, target.proxy, target.verify_ssl);
    }
  }
}

void Save(const toml::table& table, const std::string& name, bool warning_header)
{
  std::ofstream file(File::Path(name, true));
  if (warning_header) {
    file << "#######################################################################\n";
    file << "#######################################################################\n";
    file << "####                                                               ####\n";
    file << "#### NOTE: This file is not the configuration file that is used    ####\n";
    file << "####       by the STFC Community Mod.  It is provided to help      ####\n";
    file << "####       see what configuration is being used by the runtime     ####\n";
    file << "####       and any desired settings should be copied to the same   ####\n";
    file << "####       section in: " << File::DefaultConfig() << "\n";
    file << "####                                                               ####\n";
    file << "####        Config in: " << File::Config() << "\n";
    file << "####                                                               ####\n";
    file << "#######################################################################\n";
    file << "#######################################################################\n\n";
  }
  file << table;
}
} // namespace

bool SyncConfig::enabled(SyncConfig::Type type) const
{
  for (const auto& opt : SyncOptions)
    if (opt.type == type)
      return this->*opt.option;
  return false;
}

Config::Config()
{ Load(); }

Config& Config::Get()
{
  static Config config;
  return config;
}

MissionHudVisibility Config::MissionHudButtonVisibility(std::string_view button_name) const
{
  const auto it = mission_hud_buttons.find(std::string(button_name));
  return it == mission_hud_buttons.end() ? MissionHudVisibility::Auto : it->second;
}

void Config::Load()
{
  File::MigrateLegacyFiles();

  const auto  config_path = File::Path(File::Config());
  toml::table config;
  bool        loaded = false;
  try {
    const auto path = config_path.u8string();
    config          = toml::parse_file(std::string_view(reinterpret_cast<const char*>(path.data()), path.size()));
    loaded          = true;
  } catch (const toml::parse_error& e) {
    spdlog::warn("Failed to load config file, falling back to default settings: {}", e.description());
  } catch (...) {
    spdlog::warn("Failed to load config file, falling back to default settings");
  }

  toml::table parsed;
  Reader      r(config, parsed, loaded);
  interior_zoom_scale = r.Get("graphics", "interior_zoom_scale", 2.0f);
  if (!std::isfinite(interior_zoom_scale) || interior_zoom_scale < 1.0f) {
    spdlog::warn("invalid graphics.interior_zoom_scale {}; using 2", interior_zoom_scale);
    interior_zoom_scale = 2.0f;
    r.Record("graphics", "interior_zoom_scale", interior_zoom_scale);
  }
  kirshara_fixes = r.Get("control", "kirshara_fixes", true);
  if (!config["control"]["kirshara_fixes"]
      && (config["control"]["faster_queue_recovery"] || config["control"]["thin_queue_protection"]
          || config["control"]["queue_address_guard"] || config["advanced"]["queue"]["thin_queue_protection"]))
    spdlog::warn("The separate queue feature settings have been replaced by [control] kirshara_fixes (default=true). "
                 "Set kirshara_fixes=false to disable the combined fixes; old feature settings are ignored.");
  primary_requires_visible_target = r.Get("control", "primary_requires_visible_target", false);
  show_player_title               = r.Get("ui", "show_player_title", true);
  auto_confirm_reveal_ability     = r.Get("ui", "auto_confirm_reveal_ability", false);
  dock_syncmanagement             = r.Get("ui", "dock_syncmanagement", true);
  installInteriorZoomHooks        = r.Get("patches", "interiorzoomhooks", true);
  installPlayerTitleDisplayHooks  = r.Get("patches", "chattitledisplayhooks", true);
  installRevealConfirmationHooks  = r.Get("patches", "revealconfirmhooks", true);
  installActionQueueRecoveryHooks = r.Get("patches", "actionqueuerecoveryhooks", true);
  installQueueAddressGuardHooks   = r.Get("patches", "queueaddressguardhooks", true);
  installThinQueueProtectionHooks = r.Get("patches", "thinqueueprotectionhooks", true);
  installPinnedShipClickToggle    = r.Get("patches", "pinnedshipclicktoggle", true);
  installHavenZoomHooks           = r.Get("patches", "havenzoomhooks", true);
  installHavenHistoryHooks        = r.Get("patches", "havenhistoryhooks", true);
  installAllianceGamesStoreHooks  = r.Get("patches", "alliancegamesstorehooks", true);
  haven_zoom                      = r.Get("graphics", "haven_zoom", 4000.0f);
  if (!std::isfinite(haven_zoom) || haven_zoom < 0.0f) {
    spdlog::warn("invalid graphics.haven_zoom {}; using 4000", haven_zoom);
    haven_zoom = 4000.0f;
    r.Record("graphics", "haven_zoom", haven_zoom);
  }
  reverse_haven_history = r.Get("ui", "reverse_haven_history", true);

  // Patch switches are honoured in release builds too: they let users disable hook groups that collide with
  // BepInEx plugins hooking the same IL2CPP methods (a native and a managed detour on one prologue crash).
  installUiScaleHooks               = r.Get("patches", "uiscalehooks", DCP::uiscalehooks);
  installZoomHooks                  = r.Get("patches", "zoomhooks", DCP::zoomhooks);
  installBuffFixHooks               = r.Get("patches", "bufffixhooks", DCP::bufffixhooks);
  installToastBannerHooks           = r.Get("patches", "toastbannerhooks", DCP::toastbannerhooks);
  installPanHooks                   = r.Get("patches", "panhooks", DCP::panhooks);
  installHotkeyHooks                = r.Get("patches", "hotkeyhooks", DCP::hotkeyhooks);
  installFreeResizeHooks            = r.Get("patches", "freeresizehooks", DCP::freeresizehooks);
  installTempCrashFixes             = r.Get("patches", "tempcrashfixes", DCP::tempcrashfixes);
  installTestPatches                = r.Get("patches", "testpatches", DCP::testpatches);
  installMiscPatches                = r.Get("patches", "miscpatches", DCP::miscpatches);
  installMissionHudTweaksHooks      = r.Get("patches", "missionhudtweakshooks", DCP::missionhudtweakshooks);
  installChatPatches                = r.Get("patches", "chatpatches", DCP::chatpatches);
  installSyncPatches                = r.Get("patches", "syncpatches", DCP::syncpatches);
  installGameVersionHook            = r.Get("patches", "game_version", DCP::game_version);
  installObjectTracker              = r.Get("patches", "objecttracker", DCP::objecttracker);
  installLoadingScreenHooks         = r.Get("patches", "loadingscreenhooks", DCP::loadingscreenhooks);
  installTransitionScreenHooks      = r.Get("patches", "transitionscreenhooks", DCP::transitionscreenhooks);
  installGiftsBulkClaimHooks        = r.Get("patches", "giftsbulkclaimhooks", DCP::giftsbulkclaimhooks);
  installDailyFactionBulkClaimHooks = r.Get("patches", "dailyfactionbulkclaimhooks", DCP::dailyfactionbulkclaimhooks);
  installGalacticAnomalyTimerHooks  = r.Get("patches", "galacticanomalytimerhooks", DCP::galacticanomalytimerhooks);
  installLoadingTipHooks            = r.Get("patches", "loadingtiphooks", DCP::loadingtiphooks);
  installDoubleClickAssignShipHooks = r.Get("patches", "doubleclickassignshiphooks", DCP::doubleclickassignshiphooks);
  installForbiddenTechConfirmationHooks = r.Get("patches", "forbiddentechconfirmhooks", DCP::forbiddentechconfirmhooks);
  installForbiddenTechShipNameHooks     = r.Get("patches", "forbiddentechshipnamehooks", DCP::forbiddentechshipnamehooks);
  installAudioEventHooks                = r.Get("patches", "audioeventhooks", DCP::audioeventhooks);
  installInstantCargoCounterHooks       = r.Get("patches", "instantcargocounterhooks", DCP::instantcargocounterhooks);
  installCargoFormatHooks               = r.Get("patches", "cargoformathooks", DCP::cargoformathooks);
  installOfficerSortHooks               = r.Get("patches", "officersorthooks", DCP::officersorthooks);
  installPinnedShipSortHooks            = r.Get("patches", "pinnedshiphooks", DCP::pinnedshiphooks);
  installArtifactExchangeHooks          = r.Get("patches", "artifactexchangehooks", DCP::artifactexchangehooks);
  installInstantWarpConfirmationHooks   = true;

  queue_enabled       = r.Get("control", "queue_enabled", DCC::queue_enabled);
  hotkeys_enabled     = r.Get("control", "hotkeys_enabled", DCC::hotkeys_enabled);
  hotkeys_extended    = r.Get("control", "hotkeys_extended", DCC::hotkeys_extended);
  use_scopely_hotkeys = r.Get("control", "use_scopely_hotkeys", DCC::use_scopely_hotkeys);
  select_timer        = r.Get("control", "select_timer", DCC::select_timer);
  enable_experimental = r.Get("control", "enable_experimental", DCC::enable_experimental);

  ui_scale        = r.Get("graphics", "ui_scale", DCG::ui_scale);
  ui_scale_step   = r.GetRenamed("graphics", "ui_scale_step", "ui_scale_adjust", DCG::ui_scale_step);
  ui_scale_ship   = r.Get("graphics", "ui_scale_ship", DCG::ui_scale_ship);
  ui_scale_viewer = r.Get("graphics", "ui_scale_viewer", DCG::ui_scale_viewer);
  zoom            = r.Get("graphics", "zoom", DCG::zoom);
  fr_scale        = r.Get("graphics", "fr_scale", DCG::fr_scale);

  for (auto [profile, prefix, detail, threshold] :
       {std::tuple{&zoom_label_player, "zoom_label_player", DCG::zoom_label_player_detail,
                   DCG::zoom_label_player_threshold},
        std::tuple{&zoom_label_non_player, "zoom_label_non_player", DCG::zoom_label_non_player_detail,
                   DCG::zoom_label_non_player_threshold}}) {
    const std::string key = prefix;
    profile->detail       = r.Choose("graphics", key + "_detail", detail, kFleetLabelDetail);
    auto zoom_threshold   = r.Get("graphics", key + "_threshold", threshold);
    if (!std::isfinite(zoom_threshold) || zoom_threshold < 0.0f || zoom_threshold > 1.0f) {
      spdlog::warn("invalid config value graphics.{}_threshold: {}; using {}", key, zoom_threshold, threshold);
      zoom_threshold = threshold;
      r.Record("graphics", key + "_threshold", zoom_threshold);
    }
    profile->zoom_threshold = zoom_threshold;
  }

  free_resize         = r.Get("graphics", "free_resize", DCG::free_resize);
  allow_cursor        = r.Get("graphics", "allow_cursor", DCG::allow_cursor);
  keyboard_zoom_speed = r.Get("graphics", "keyboard_zoom_speed", DCG::keyboard_zoom_speed);
  if (enable_experimental)
    system_pan_momentum = r.Get("graphics", "system_pan_momentum", DCG::system_pan_momentum);
  system_pan_momentum_falloff = r.Get("graphics", "system_pan_momentum_falloff", DCG::system_pan_momentum_falloff);
  disable_target_camera_lock  = r.Get("graphics", "disable_target_camera_lock", DCG::disable_target_camera_lock);
  borderless_fullscreen       = r.Get("graphics", "borderless_fullscreen", DCG::borderless_fullscreen);
  transition_time             = r.Get("graphics", "transition_time", DCG::transition_time);
  default_system_zoom         = r.Get("graphics", "default_system_zoom", DCG::default_system_zoom);
  system_zoom_preset_1        = r.Get("graphics", "system_zoom_preset_1", DCG::system_zoom_preset_1);
  system_zoom_preset_2        = r.Get("graphics", "system_zoom_preset_2", DCG::system_zoom_preset_2);
  system_zoom_preset_3        = r.Get("graphics", "system_zoom_preset_3", DCG::system_zoom_preset_3);
  system_zoom_preset_4        = r.Get("graphics", "system_zoom_preset_4", DCG::system_zoom_preset_4);
  system_zoom_preset_5        = r.Get("graphics", "system_zoom_preset_5", DCG::system_zoom_preset_5);
  use_presets_as_default      = r.Get("graphics", "use_presets_as_default", DCG::use_presets_as_default);

  loader_enabled          = r.Get("graphics", "loader_enabled", DCG::loader_enabled);
  loader_transition       = r.Get("graphics", "loader_transition", DCG::loader_transition);
  loader_transition_black = r.Get("graphics", "loader_transition_black", DCG::loader_transition_black);
#ifdef _USE_ORIGINAL_BG
  loader_transition_black = true;
#endif
  // Without transition customization the game's own black background stays.
  if (!loader_transition)
    loader_transition_black = true;
  loader_image           = r.Get("graphics", "loader_image", DCG::loader_image);
  loader_logo_scale      = r.Get("graphics", "loader_logo_scale", DCG::loader_logo_scale);
  loader_tip_enabled     = r.Get("graphics", "loader_tip_enabled", DCG::loader_tip_enabled);
  galactic_anomaly_timer = r.Get("graphics", "galactic_anomaly_timer", DCG::galactic_anomaly_timer);

  use_out_of_dock_power = r.Get("buffs", "use_out_of_dock_power", DCBS::use_out_of_dock_power);

  trace_audio_events       = r.Get("audio", "trace_events", DCA::trace_events);
  disable_all_audio_events = false;
  disabled_audio_events.clear();
  for (const auto& event : StrSplit(r.Get<std::string>("audio", "disabled_events", ""), ',')) {
    if (AsciiStrToLower(event) == "all")
      disable_all_audio_events = true;
    else
      disabled_audio_events.push_back(event);
  }
  bool has_audio_alert = false;
  for (auto [key, value] : std::initializer_list<std::pair<const char*, NotificationSound*>>{
           {"alert_victory", &alert_victory},
           {"alert_defeat", &alert_defeat},
           {"alert_armada_created", &alert_armada_created},
           {"alert_armada_battle_won", &alert_armada_battle_won},
           {"alert_armada_battle_lost", &alert_armada_battle_lost}}) {
    const auto name  = r.Get<std::string>("audio", key, "none");
    const auto sound = notification_sound_from_name(StripAsciiWhitespace(name));
    if (!sound)
      spdlog::warn("invalid config value audio.{}: '{}'; using none", key, name);
    *value = sound.value_or(NotificationSound::None);
    r.Record("audio", key, notification_sound_name(*value));
    has_audio_alert |= *value != NotificationSound::None;
  }
  if (has_audio_alert && !installToastBannerHooks)
    spdlog::warn("audio alerts require patches.toastbannerhooks = true");

  disable_escape_exit       = r.Get("ui", "disable_escape_exit", DCU::disable_escape_exit);
  disable_escape_exit_timer = r.Get("ui", "disable_escape_exit_timer", DCU::disable_escape_exit_timer);
  disable_preview_locate    = r.Get("ui", "disable_preview_locate", DCU::disable_preview_locate);
  disable_preview_recall    = r.Get("ui", "disable_preview_recall", DCU::disable_preview_recall);
  disable_first_popup       = r.Get("ui", "disable_first_popup", DCU::disable_first_popup);
  only_show_first_popup     = r.Get("ui", "only_show_first_popup", false);
  disable_move_keys         = r.Get("ui", "disable_move_keys", DCU::disable_move_keys);
  disable_toast_banners     = r.Get("ui", "disable_toast_banners", DCU::disable_toast_banners);
  default_chat_width        = r.Get("ui", "default_chat_width", DCU::default_chat_width);
  auto_open_chat            = r.Get("ui", "auto_open_chat", DCU::auto_open_chat);
  disable_galaxy_chat       = r.Get("ui", "disable_galaxy_chat", DCU::disable_galaxy_chat);
  disable_veil_chat         = r.Get("ui", "disable_veil_chat", DCU::disable_veil_chat);

  auto_open_bulk_claim_flyout = r.Get("ui", "auto_open_bulk_claim_flyout", DCU::auto_open_bulk_claim_flyout);
  daily_bulk_claim_factions   = ReadFactions(r);
  daily_bulk_claim_toggle_default_on =
      r.Get("ui", "daily_bulk_claim_toggle_default_on", DCU::daily_bulk_claim_toggle_default_on);

  auto_confirm_instant_warp = r.Choose("ui", "auto_confirm_instant_warp", DCU::auto_confirm_instant_warp, kInstantWarp);
  ReadShipFilter(r, "instant_warp_auto_jump", DCU::instant_warp_auto_jump, instant_warp_auto_jump,
                 instant_warp_auto_jump_all);
  ReadShipFilter(r, "instant_warp_auto_warp", DCU::instant_warp_auto_warp, instant_warp_auto_warp,
                 instant_warp_auto_warp_all);
  ReadShipFilter(r, "instant_warp_always_ask", DCU::instant_warp_always_ask, instant_warp_always_ask,
                 instant_warp_always_ask_all);
  bool pin_all = false; // "*" pins nothing
  ReadShipFilter(r, "pinned_ships", DCU::pinned_ships, pinned_ships, pin_all);

  double_click_to_assign_ship = r.Get("ui", "double_click_to_assign_ship", DCU::double_click_to_assign_ship);
  focus_search                = r.Get("ui", "focus_search", DCU::focus_search);
  format_cargo_values         = r.GetRenamed("ui", "format_cargo_values", "cargo_format", DCU::format_cargo_values);
  officer_sort                = r.Get("ui", "officer_sort", DCU::officer_sort);
  arrow_keys_to_select_ship   = r.Get("ui", "arrow_keys_to_select_ship", DCU::arrow_keys_to_select_ship);
  auto_confirm_ft_upgrade     = r.Get("ui", "auto_confirm_ft_upgrade", DCU::auto_confirm_ft_upgrade);
  forbidden_tech_ship_name    = r.Get("ui", "forbidden_tech_ship_name", DCU::forbidden_tech_ship_name);
  auto_confirm_alliance_contribution =
      r.Get("ui", "auto_confirm_alliance_contribution", DCU::auto_confirm_alliance_contribution);

  extend_chest_purchase_max = r.Get("ui", "extend_chest_purchase_max", DCU::extend_chest_purchase_max);
  extend_donation_slider    = r.Get("ui", "extend_donation_slider", DCU::extend_donation_slider);
  extend_donation_max       = r.Get("ui", "extend_donation_max", DCU::extend_donation_max);

  show_cargo_default         = r.Get("ui", "show_cargo_default", DCU::show_cargo_default);
  show_player_cargo          = r.Get("ui", "show_player_cargo", DCU::show_player_cargo);
  show_station_cargo         = r.Get("ui", "show_station_cargo", DCU::show_station_cargo);
  show_hostile_cargo         = r.Get("ui", "show_hostile_cargo", DCU::show_hostile_cargo);
  show_armada_cargo          = r.Get("ui", "show_armada_cargo", DCU::show_armada_cargo);
  instant_cargo_counter      = r.Get("ui", "instant_cargo_counter", DCU::instant_cargo_counter);
  cargo_significant_decimals = r.Get("ui", "cargo_significant_decimals", DCU::cargo_significant_decimals);

  always_skip_reveal_sequence = r.Get("ui", "always_skip_reveal_sequence", DCU::always_skip_reveal_sequence);
  disable_exchange_all =
      r.GetRenamed("ui", "disable_exchange_all", "hide_artifact_exchange_all", DCU::disable_exchange_all);
  mission_hud_buttons = {
      {"q_trials", r.Choose("ui", "hud_q_trials", DCU::hud_q_trials, kHudVisibility)},
      {"field_training", r.Choose("ui", "hud_field_training", DCU::hud_field_training, kHudVisibility)},
      {"outposts", r.Choose("ui", "hud_outposts", DCU::hud_outposts, kHudVisibility)},
      {"daily_goals", r.Choose("ui", "hud_daily_goals", DCU::hud_daily_goals, kHudVisibility)},
      {"missions", r.Choose("ui", "hud_missions", DCU::hud_missions, kHudVisibility)},
  };

  disabled_banner_types = ReadBannerTypes(r, "disabled_banner_types", DCU::disabled_banner_types);
  notify_banner_types   = ReadBannerTypes(r, "notify_banner_types", DCU::notify_banner_types);

  sync_debug              = r.Get("sync", "debug", DCS::debug);
  sync_logging            = r.Get("sync", "logging", DCS::logging);
  sync_resolver_cache_ttl = r.Get("sync", "resolver_cache_ttl", DCS::resolver_cache_ttl);

  SyncConfig sync_defaults;
  sync_defaults.proxy      = r.Get("sync", "proxy", DCS::proxy);
  sync_defaults.verify_ssl = r.Get("sync", "verify_ssl", DCS::verify_ssl);
  for (const auto& option : SyncOptions)
    sync_defaults.*option.option = OldName(option).empty()
                                       ? r.Get("sync", option.option_str, false)
                                       : r.GetRenamed("sync", option.option_str, OldName(option), false);
  ReadSyncTargets(r, sync_targets, sync_defaults);

  if (auto sync_file = config["sync"]["file"].value<std::string>(); sync_file && !sync_file->empty()) {
    spdlog::error("Deprecation Notice: The 'sync_file' config option has been deprecated and removed. "
                  "For capturing sync output, please use a local HTTP server instead.");
  }

  // The global options say whether any target wants a category, so the game hooks can skip the rest.
  sync_options.proxy      = sync_defaults.proxy;
  sync_options.verify_ssl = sync_defaults.verify_ssl;
  for (const auto& option : SyncOptions) {
    sync_options.*option.option = std::ranges::any_of(sync_targets | std::views::values,
                                                      [&](const auto& target) { return target.*option.option; });
  }

  config_settings_url        = r.Get("config", "settings_url", DCSC::settings_url);
  config_assets_url_override = r.Get("config", "assets_url_override", DCSC::assets_url_override);

  Shortcut::Load(config, parsed, hotkeys_extended, enable_experimental);

  if (!std::filesystem::exists(config_path)) {
    spdlog::warn("Creating {} (default config file)", File::Config());
    Save(parsed, File::Config(), false);
  }
  spdlog::info("Creating {} (final config file)", File::Vars());
  Save(parsed, File::Vars(), true);

  std::cout << "\n\n-----------------------------\n\n"
            << parsed << "\n\n-----------------------------\n"
#if VERSION_PATCH
            << "Loaded beta version " << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_REVISION << " (Patch "
            << VERSION_PATCH << ")\n\nNOTE: Beta versions may have unexpected bugs and issues.\n\n";
#else
            << "Loaded release version " << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_REVISION << "\n\n";
#endif
}

NotificationSound Config::NotificationSoundForToast(int toast_state) const
{
  switch (toast_state) {
    case static_cast<int>(ToastState::Victory):
      return alert_victory;
    case static_cast<int>(ToastState::Defeat):
      return alert_defeat;
    case static_cast<int>(ToastState::ArmadaCreated):
      return alert_armada_created;
    case static_cast<int>(ToastState::ArmadaBattleWon):
      return alert_armada_battle_won;
    case static_cast<int>(ToastState::ArmadaBattleLost):
      return alert_armada_battle_lost;
    default:
      return NotificationSound::None;
  }
}

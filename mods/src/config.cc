#include "config.h"

#include "file.h"
#include "patches/shortcuts.h"
#include "ship_name_match.h"
#include "str_utils.h"
#include "version.h"

#include <prime/ui.h>

#include <spdlog/spdlog.h>
#include <toml++/toml.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <tuple>

namespace
{
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

  // Reads `key` into `value`, whose current value (the member's initializer) is the default.
  template <typename T> void Read(std::string_view section, std::string_view key, T& value)
  { value = Get(section, key, value); }

  // A setting that was renamed: `key`, or `old_key` when only the old name is set. The vars file records `key`.
  template <typename T>
  void ReadRenamed(std::string_view section, std::string_view key, std::string_view old_key, T& value)
  {
    if (config_[section][key] || !config_[section][old_key])
      return Read(section, key, value);
    if (auto read = config_[section][old_key].template value<T>())
      value = *read;
    else
      spdlog::warn("invalid config value {}.{}; using {}", section, old_key, value);
    Record(section, key, value);
  }

  // A case-insensitive choice from a fixed list of names, read into `value`, whose current value is the default.
  template <typename E, size_t N>
  void Choose(std::string_view section, std::string_view key, E& value, const Choice<E> (&choices)[N])
  {
    const auto fallback   = Name(value, choices);
    const auto raw        = Get<std::string>(section, key, std::string(fallback));
    const auto normalized = AsciiStrToLower(StripAsciiWhitespace(raw));
    const auto found      = std::ranges::find(choices, normalized, &Choice<E>::name);
    if (found == std::end(choices))
      spdlog::warn("invalid config value {}.{}: '{}'; using {}", section, key, raw, fallback);
    else
      value = found->value;
    Record(section, key, std::string(Name(value, choices)));
  }

  template <typename T> void Record(std::string_view section, std::string_view key, const T& value)
  {
    Section(section).insert_or_assign(key, value);
    if (log_)
      spdlog::debug("config value {}.{} value: {}", section, key, value);
  }

private:
  // The canonical name: the first one listed for the value.
  template <typename E, size_t N> static std::string_view Name(E value, const Choice<E> (&choices)[N])
  { return std::ranges::find(choices, value, &Choice<E>::value)->name; }

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
ShipFilter ReadShipFilter(Reader& reader, std::string_view key)
{
  const auto value = reader.Get<std::string>("ui", key, "");
  ShipFilter filter;
  filter.all = StripAsciiWhitespace(value) == "*";
  if (!filter.all)
    for (const auto& token : StrSplit(value, ','))
      if (auto normalized = ShipNameMatch::NormalizeKey(token); !normalized.empty())
        filter.names.push_back(std::move(normalized));
  return filter;
}

std::vector<std::string> ReadFactions(Reader& reader)
{
  static constexpr std::string_view kKnownFactions[] = {"federation", "klingon", "romulan"};

  std::vector<std::string> factions;
  auto value = reader.Get<std::string>("ui", "daily_bulk_claim_factions", "federation,klingon,romulan");
  if (StripAsciiWhitespace(value).empty()) {
    value = "federation,klingon,romulan";
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
std::vector<int> ReadBannerTypes(Reader& reader, std::string_view key)
{
  std::vector<std::string> wanted;
  for (const auto& token : StrSplit(reader.Get<std::string>("ui", key, ""), ','))
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
           .insert_or_assign(
               "targets",
               toml::table{
                   {"stfcdata", toml::table{{"token", ""}, {"url", "https://sync.stfcdata.de"}}},
                   {"nextspocksclub", toml::table{{"token", ""}, {"url", "https://next.spocks.club/sync/ingress/"}}},
                   {"spocksclub", toml::table{{"token", ""}, {"url", "https://spocks.club/sync/ingress/"}}},
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

// The settings file as parsed. `loaded` is false when it was missing or did not parse and defaults are used.
struct SettingsFile {
  toml::table table;
  bool        loaded = false;
};

namespace
{
// The current settings. Replaced settings are never freed: a sync thread may still be reading them, and they only
// pile up as fast as someone edits the file.
std::atomic<Config*> current_config{nullptr};

toml::table ParseSettingsFile()
{
  const auto path = File::Path(File::Config()).u8string();
  return toml::parse_file(std::string_view(reinterpret_cast<const char*>(path.data()), path.size()));
}
} // namespace

Config& Config::Get()
{
  if (auto* config = current_config.load(std::memory_order_acquire))
    return *config;
  // First use, inside il2cpp_init on the game thread. A broken file falls back to the defaults here.
  static Config* const first = [] {
    File::MigrateLegacyFiles();
    SettingsFile file;
    try {
      file.table  = ParseSettingsFile();
      file.loaded = true;
    } catch (const toml::parse_error& e) {
      spdlog::warn("Failed to load config file, falling back to default settings: {}", e.description());
    } catch (...) {
      spdlog::warn("Failed to load config file, falling back to default settings");
    }
    auto* config = new Config;
    config->Load(file, true);
    current_config.store(config, std::memory_order_release);
    return config;
  }();
  return *first;
}

bool Config::Reload()
{
  SettingsFile file;
  try {
    file.table  = ParseSettingsFile();
    file.loaded = true;
  } catch (const toml::parse_error& e) {
    spdlog::warn("Settings not reloaded, {} does not parse: {}", File::Config(), e.description());
    return false;
  } catch (const std::exception& e) {
    spdlog::warn("Settings not reloaded, {} could not be read: {}", File::Config(), e.what());
    return false;
  }

  spdlog::info("Reloading settings from {}", File::Config());
  auto* next = new Config;
  try {
    next->Load(file, false);
  } catch (const std::exception& e) {
    // Shortcuts may already be rebound from the new file; everything else keeps the current settings.
    spdlog::error("Settings not reloaded: {}", e.what());
    delete next;
    return false;
  }
  // Hooks are installed once, so the startup hook groups stay in force.
  if (!(next->patches == Get().patches))
    spdlog::warn("Changes to [patches] take effect after a restart");
  next->patches = Get().patches;
  current_config.store(next, std::memory_order_release);
  return true;
}

void Config::ReloadIfChanged()
{
  // Editors save in steps (truncate, then write) and an empty file is valid TOML, so a change is only applied once
  // the file has kept the same modification time for a whole check.
  using Clock = std::chrono::steady_clock;
  static Clock::time_point                              next_check{};
  static std::optional<std::filesystem::file_time_type> applied, pending;

  const auto now = Clock::now();
  if (now < next_check)
    return;
  next_check = now + std::chrono::seconds(1);

  std::error_code error;
  const auto      time = std::filesystem::last_write_time(File::Path(File::Config()), error);
  if (error)
    return;
  if (!applied || time == *applied) {
    applied = time;
    pending.reset();
    return;
  }
  if (pending != time) {
    pending = time;
    return;
  }
  applied = time;
  pending.reset();
  Reload();
}

MissionHudVisibility Config::Ui::MissionHudButtonVisibility(std::string_view button) const
{
  const auto it = mission_hud_buttons.find(button);
  return it == mission_hud_buttons.end() ? MissionHudVisibility::Auto : it->second;
}

void Config::Load(const SettingsFile& file, bool first)
{
  const auto& config = file.table;
  toml::table parsed;
  Reader      r(config, parsed, file.loaded);
  r.Read("graphics", "interior_zoom_scale", graphics.interior_zoom_scale);
  if (!std::isfinite(graphics.interior_zoom_scale) || graphics.interior_zoom_scale < 1.0f) {
    spdlog::warn("invalid graphics.interior_zoom_scale {}; using 2", graphics.interior_zoom_scale);
    graphics.interior_zoom_scale = 2.0f;
    r.Record("graphics", "interior_zoom_scale", graphics.interior_zoom_scale);
  }
  r.Read("control", "kirshara_fixes", control.kirshara_fixes);
  if (!config["control"]["kirshara_fixes"]
      && (config["control"]["faster_queue_recovery"] || config["control"]["thin_queue_protection"]
          || config["control"]["queue_address_guard"] || config["advanced"]["queue"]["thin_queue_protection"]))
    spdlog::warn("The separate queue feature settings have been replaced by [control] kirshara_fixes (default=true). "
                 "Set kirshara_fixes=false to disable the combined fixes; old feature settings are ignored.");
  r.Read("control", "primary_requires_visible_target", control.primary_requires_visible_target);
  r.Read("ui", "show_player_title", ui.show_player_title);
  r.Read("ui", "auto_confirm_reveal_ability", ui.auto_confirm_reveal_ability);
  r.Read("ui", "dock_syncmanagement", ui.dock_syncmanagement);
  r.Read("patches", "interiorzoomhooks", patches.interiorzoomhooks);
  r.Read("patches", "chattitledisplayhooks", patches.chattitledisplayhooks);
  r.Read("patches", "revealconfirmhooks", patches.revealconfirmhooks);
  r.Read("patches", "actionqueuerecoveryhooks", patches.actionqueuerecoveryhooks);
  r.Read("patches", "queueaddressguardhooks", patches.queueaddressguardhooks);
  r.Read("patches", "thinqueueprotectionhooks", patches.thinqueueprotectionhooks);
  r.Read("patches", "pinnedshipclicktoggle", patches.pinnedshipclicktoggle);

  // [patches] toggles are read in every build: they let users disable hook groups that collide with BepInEx
  // plugins hooking the same IL2CPP methods (a native and a managed detour on one prologue crash).
  for (auto [key, value] : std::initializer_list<std::pair<const char*, bool*>>{
           {"uiscalehooks", &patches.uiscalehooks},
           {"zoomhooks", &patches.zoomhooks},
           {"havenzoomhooks", &patches.havenzoomhooks},
           {"havenhistoryhooks", &patches.havenhistoryhooks},
           {"alliancegamesstorehooks", &patches.alliancegamesstorehooks},
           {"bufffixhooks", &patches.bufffixhooks},
           {"toastbannerhooks", &patches.toastbannerhooks},
           {"panhooks", &patches.panhooks},
           {"hotkeyhooks", &patches.hotkeyhooks},
           {"freeresizehooks", &patches.freeresizehooks},
           {"tempcrashfixes", &patches.tempcrashfixes},
           {"testpatches", &patches.testpatches},
           {"miscpatches", &patches.miscpatches},
           {"missionhudtweakshooks", &patches.missionhudtweakshooks},
           {"chatpatches", &patches.chatpatches},
           {"syncpatches", &patches.syncpatches},
           {"game_version", &patches.game_version},
           {"objecttracker", &patches.objecttracker},
           {"loadingscreenhooks", &patches.loadingscreenhooks},
           {"transitionscreenhooks", &patches.transitionscreenhooks},
           {"giftsbulkclaimhooks", &patches.giftsbulkclaimhooks},
           {"dailyfactionbulkclaimhooks", &patches.dailyfactionbulkclaimhooks},
           {"galacticanomalytimerhooks", &patches.galacticanomalytimerhooks},
           {"loadingtiphooks", &patches.loadingtiphooks},
           {"doubleclickassignshiphooks", &patches.doubleclickassignshiphooks},
           {"forbiddentechconfirmhooks", &patches.forbiddentechconfirmhooks},
           {"forbiddentechshipnamehooks", &patches.forbiddentechshipnamehooks},
           {"audioeventhooks", &patches.audioeventhooks},
           {"instantcargocounterhooks", &patches.instantcargocounterhooks},
           {"cargoformathooks", &patches.cargoformathooks},
           {"officersorthooks", &patches.officersorthooks},
           {"pinnedshiphooks", &patches.pinnedshiphooks},
           {"artifactexchangehooks", &patches.artifactexchangehooks},
       })
    r.Read("patches", key, *value);

  r.Read("control", "queue_enabled", control.queue_enabled);
  r.Read("control", "hotkeys_enabled", control.hotkeys_enabled);
  r.Read("control", "hotkeys_extended", control.hotkeys_extended);
  r.Read("control", "use_scopely_hotkeys", control.use_scopely_hotkeys);
  r.Read("control", "select_timer", control.select_timer);
  r.Read("control", "enable_experimental", control.enable_experimental);

  auto& g = graphics;
  r.Read("graphics", "ui_scale", g.ui_scale);
  r.ReadRenamed("graphics", "ui_scale_step", "ui_scale_adjust", g.ui_scale_step);
  r.Read("graphics", "ui_scale_ship", g.ui_scale_ship);
  r.Read("graphics", "ui_scale_viewer", g.ui_scale_viewer);
  r.Read("graphics", "zoom", g.zoom);
  r.Read("graphics", "haven_zoom", g.haven_zoom);
  if (!std::isfinite(g.haven_zoom) || g.haven_zoom < 0.0f) {
    spdlog::warn("invalid g.haven_zoom {}; using 4000", g.haven_zoom);
    g.haven_zoom = 4000.0f;
    r.Record("graphics", "haven_zoom", g.haven_zoom);
  }
  r.Read("graphics", "fr_scale", g.fr_scale);
  for (auto [profile, prefix] : {std::pair{&g.zoom_label_player, std::string("zoom_label_player")},
                                 std::pair{&g.zoom_label_non_player, std::string("zoom_label_non_player")}}) {
    const auto fallback = profile->zoom_threshold;
    r.Choose("graphics", prefix + "_detail", profile->detail, kFleetLabelDetail);
    r.Read("graphics", prefix + "_threshold", profile->zoom_threshold);
    if (!std::isfinite(profile->zoom_threshold) || profile->zoom_threshold < 0.0f || profile->zoom_threshold > 1.0f) {
      spdlog::warn("invalid config value graphics.{}_threshold: {}; using {}", prefix, profile->zoom_threshold,
                   fallback);
      profile->zoom_threshold = fallback;
      r.Record("graphics", prefix + "_threshold", fallback);
    }
  }
  r.Read("graphics", "free_resize", g.free_resize);
  r.Read("graphics", "allow_cursor", g.allow_cursor);
  r.Read("graphics", "keyboard_zoom_speed", g.keyboard_zoom_speed);
  if (control.enable_experimental)
    r.Read("graphics", "system_pan_momentum", g.system_pan_momentum);
  r.Read("graphics", "system_pan_momentum_falloff", g.system_pan_momentum_falloff);
  r.Read("graphics", "disable_target_camera_lock", g.disable_target_camera_lock);
  r.Read("graphics", "borderless_fullscreen", g.borderless_fullscreen);
  r.Read("graphics", "transition_time", g.transition_time);
  r.Read("graphics", "default_system_zoom", g.default_system_zoom);
  r.Read("graphics", "system_zoom_preset_1", g.system_zoom_preset_1);
  r.Read("graphics", "system_zoom_preset_2", g.system_zoom_preset_2);
  r.Read("graphics", "system_zoom_preset_3", g.system_zoom_preset_3);
  r.Read("graphics", "system_zoom_preset_4", g.system_zoom_preset_4);
  r.Read("graphics", "system_zoom_preset_5", g.system_zoom_preset_5);
  r.Read("graphics", "use_presets_as_default", g.use_presets_as_default);
  r.Read("graphics", "loader_enabled", g.loader_enabled);
  r.Read("graphics", "loader_transition", g.loader_transition);
  r.Read("graphics", "loader_transition_black", g.loader_transition_black);
#ifdef _USE_ORIGINAL_BG
  g.loader_transition_black = true;
#endif
  // Without transition customization the game's own black background stays.
  if (!g.loader_transition)
    g.loader_transition_black = true;
  r.Read("graphics", "loader_image", g.loader_image);
  r.Read("graphics", "loader_logo_scale", g.loader_logo_scale);
  r.Read("graphics", "loader_tip_enabled", g.loader_tip_enabled);
  r.Read("graphics", "galactic_anomaly_timer", g.galactic_anomaly_timer);

  r.Read("buffs", "use_out_of_dock_power", buffs.use_out_of_dock_power);

  r.Read("audio", "trace_events", audio.trace_events);
  audio.disable_all_events = false;
  audio.disabled_events.clear();
  for (const auto& event : StrSplit(r.Get<std::string>("audio", "disabled_events", ""), ',')) {
    if (AsciiStrToLower(event) == "all")
      audio.disable_all_events = true;
    else
      audio.disabled_events.push_back(event);
  }
  bool has_audio_alert = false;
  for (auto [key, value] : std::initializer_list<std::pair<const char*, NotificationSound*>>{
           {"alert_victory", &audio.alert_victory},
           {"alert_defeat", &audio.alert_defeat},
           {"alert_armada_created", &audio.alert_armada_created},
           {"alert_armada_battle_won", &audio.alert_armada_battle_won},
           {"alert_armada_battle_lost", &audio.alert_armada_battle_lost}}) {
    const auto name  = r.Get<std::string>("audio", key, "none");
    const auto sound = notification_sound_from_name(StripAsciiWhitespace(name));
    if (!sound)
      spdlog::warn("invalid config value audio.{}: '{}'; using none", key, name);
    *value = sound.value_or(NotificationSound::None);
    r.Record("audio", key, notification_sound_name(*value));
    has_audio_alert |= *value != NotificationSound::None;
  }
  if (has_audio_alert && !patches.toastbannerhooks)
    spdlog::warn("audio alerts require patches.toastbannerhooks = true");

  auto& u = ui;
  r.Read("ui", "disable_escape_exit", u.disable_escape_exit);
  r.Read("ui", "disable_escape_exit_timer", u.disable_escape_exit_timer);
  r.Read("ui", "disable_preview_locate", u.disable_preview_locate);
  r.Read("ui", "disable_preview_recall", u.disable_preview_recall);
  r.Read("ui", "disable_first_popup", u.disable_first_popup);
  r.Read("ui", "only_show_first_popup", u.only_show_first_popup);
  r.Read("ui", "disable_move_keys", u.disable_move_keys);
  r.Read("ui", "disable_toast_banners", u.disable_toast_banners);
  r.Read("ui", "default_chat_width", u.default_chat_width);
  r.Read("ui", "auto_open_chat", u.auto_open_chat);
  r.Read("ui", "disable_galaxy_chat", u.disable_galaxy_chat);
  r.Read("ui", "disable_veil_chat", u.disable_veil_chat);
  r.Read("ui", "auto_open_bulk_claim_flyout", u.auto_open_bulk_claim_flyout);
  u.daily_bulk_claim_factions = ReadFactions(r);
  r.Read("ui", "daily_bulk_claim_toggle_default_on", u.daily_bulk_claim_toggle_default_on);
  r.Choose("ui", "auto_confirm_instant_warp", u.auto_confirm_instant_warp, kInstantWarp);
  u.instant_warp_auto_jump  = ReadShipFilter(r, "instant_warp_auto_jump");
  u.instant_warp_auto_warp  = ReadShipFilter(r, "instant_warp_auto_warp");
  u.instant_warp_always_ask = ReadShipFilter(r, "instant_warp_always_ask");
  // "*" pins nothing.
  u.pinned_ships = ReadShipFilter(r, "pinned_ships").names;
  r.Read("ui", "double_click_to_assign_ship", u.double_click_to_assign_ship);
  r.Read("ui", "focus_search", u.focus_search);
  r.ReadRenamed("ui", "format_cargo_values", "cargo_format", u.format_cargo_values);
  r.Read("ui", "officer_sort", u.officer_sort);
  r.Read("ui", "reverse_haven_history", u.reverse_haven_history);
  r.Read("ui", "arrow_keys_to_select_ship", u.arrow_keys_to_select_ship);
  r.Read("ui", "auto_confirm_ft_upgrade", u.auto_confirm_ft_upgrade);
  r.Read("ui", "forbidden_tech_ship_name", u.forbidden_tech_ship_name);
  r.Read("ui", "auto_confirm_alliance_contribution", u.auto_confirm_alliance_contribution);
  r.Read("ui", "extend_chest_purchase_max", u.extend_chest_purchase_max);
  r.Read("ui", "extend_donation_slider", u.extend_donation_slider);
  r.Read("ui", "extend_donation_max", u.extend_donation_max);
  r.Read("ui", "show_cargo_default", u.show_cargo_default);
  r.Read("ui", "show_player_cargo", u.show_player_cargo);
  r.Read("ui", "show_station_cargo", u.show_station_cargo);
  r.Read("ui", "show_hostile_cargo", u.show_hostile_cargo);
  r.Read("ui", "show_armada_cargo", u.show_armada_cargo);
  r.Read("ui", "instant_cargo_counter", u.instant_cargo_counter);
  r.Read("ui", "cargo_significant_decimals", u.cargo_significant_decimals);
  r.Read("ui", "always_skip_reveal_sequence", u.always_skip_reveal_sequence);
  r.ReadRenamed("ui", "disable_exchange_all", "hide_artifact_exchange_all", u.disable_exchange_all);
  for (const char* button : {"q_trials", "field_training", "outposts", "daily_goals", "missions"})
    r.Choose("ui", std::string("hud_") + button, u.mission_hud_buttons[button], kHudVisibility);
  u.disabled_banner_types = ReadBannerTypes(r, "disabled_banner_types");
  u.notify_banner_types   = ReadBannerTypes(r, "notify_banner_types");

  r.Read("sync", "debug", sync.debug);
  r.Read("sync", "logging", sync.logging);
  r.Read("sync", "resolver_cache_ttl", sync.resolver_cache_ttl);
  SyncConfig sync_defaults;
  r.Read("sync", "proxy", sync_defaults.proxy);
  r.Read("sync", "verify_ssl", sync_defaults.verify_ssl);
  for (const auto& option : SyncOptions)
    if (OldName(option).empty())
      r.Read("sync", option.option_str, sync_defaults.*option.option);
    else
      r.ReadRenamed("sync", option.option_str, OldName(option), sync_defaults.*option.option);
  ReadSyncTargets(r, sync.targets, sync_defaults);

  if (auto sync_file = config["sync"]["file"].value<std::string>(); sync_file && !sync_file->empty()) {
    spdlog::error("Deprecation Notice: The 'sync_file' config option has been deprecated and removed. "
                  "For capturing sync output, please use a local HTTP server instead.");
  }

  // The global options say whether any target wants a category, so the game hooks can skip the rest.
  sync.options.proxy      = sync_defaults.proxy;
  sync.options.verify_ssl = sync_defaults.verify_ssl;
  for (const auto& option : SyncOptions) {
    sync.options.*option.option = std::ranges::any_of(sync.targets | std::views::values,
                                                      [&](const auto& target) { return target.*option.option; });
  }

  r.Read("config", "settings_url", game.settings_url);
  r.Read("config", "assets_url_override", game.assets_url_override);

  Shortcut::Load(config, parsed, control.hotkeys_extended, control.enable_experimental);

  if (first && !std::filesystem::exists(File::Path(File::Config()))) {
    spdlog::warn("Creating {} (default config file)", File::Config());
    Save(parsed, File::Config(), false);
  }
  spdlog::info("Creating {} (final config file)", File::Vars());
  Save(parsed, File::Vars(), true);

  if (!first)
    return;
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
      return audio.alert_victory;
    case static_cast<int>(ToastState::Defeat):
      return audio.alert_defeat;
    case static_cast<int>(ToastState::ArmadaCreated):
      return audio.alert_armada_created;
    case static_cast<int>(ToastState::ArmadaBattleWon):
      return audio.alert_armada_battle_won;
    case static_cast<int>(ToastState::ArmadaBattleLost):
      return audio.alert_armada_battle_lost;
    default:
      return NotificationSound::None;
  }
}

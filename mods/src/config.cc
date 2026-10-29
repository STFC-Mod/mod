#include "config.h"
#include "file.h"
#include "patches/mapkey.h"
#include "prime/KeyCode.h"
#include "ship_name_match.h"
#include "str_utils.h"
#include "version.h"
#include <prime/Toast.h>

#include <EASTL/tuple.h>
#include <spdlog/spdlog.h>

#include "defaultconfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace DCP  = DefaultConfig::Patches;
namespace DCA  = DefaultConfig::Audio;
namespace DCG  = DefaultConfig::Graphics;
namespace DCC  = DefaultConfig::Control;
namespace DCU  = DefaultConfig::UI;
namespace DCBS = DefaultConfig::Buffs;
namespace DCS  = DefaultConfig::Sync;
namespace DCSC = DefaultConfig::SystemConfig;
namespace DCSH = DefaultConfig::Shortcuts;

namespace
{
constexpr bool is_all_audio_wildcard(std::string_view value)
{
  return value.size() == 3 && (value[0] == 'a' || value[0] == 'A') && (value[1] == 'l' || value[1] == 'L')
         && (value[2] == 'l' || value[2] == 'L');
}

static_assert(is_all_audio_wildcard("aLl"));
static_assert(!is_all_audio_wildcard(std::string_view{"All\0suffix", 10}));
static_assert(!is_all_audio_wildcard(std::string_view{"\xffll", 3}));

struct ToastAudioAlertConfig {
  int                       toast_state;
  std::string_view          config_name;
  std::string_view          default_sound;
  NotificationSound Config::* config_member;
};

constexpr auto kToastAudioAlerts = std::to_array<ToastAudioAlertConfig>({
    {ToastState::Victory, "alert_victory", DCA::alert_victory, &Config::alert_victory},
    {ToastState::Defeat, "alert_defeat", DCA::alert_defeat, &Config::alert_defeat},
    {ToastState::ArmadaCreated, "alert_armada_created", DCA::alert_armada_created, &Config::alert_armada_created},
    {ToastState::ArmadaBattleWon, "alert_armada_battle_won", DCA::alert_armada_battle_won,
     &Config::alert_armada_battle_won},
    {ToastState::ArmadaBattleLost, "alert_armada_battle_lost", DCA::alert_armada_battle_lost,
     &Config::alert_armada_battle_lost},
});
} // namespace

static const eastl::tuple<const char*, int> bannerTypes[] = {
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

bool SyncConfig::enabled(SyncConfig::Type type) const
{
  for (const auto& opt : SyncOptions) {
    if (opt.type == type) {
      return this->*opt.option;
    }
  }

  return false;
}

Config::Config()
{
  Load();
}

void Config::Save(const toml::table& config, const std::string_view filename, bool apply_warning)
{
  std::ofstream config_file;

  auto config_path = File::MakePath(filename, true);
  config_file.open(config_path);

  if (apply_warning) {
    char defaultFile[255], configFile[255];
    snprintf(defaultFile, 255, "%s", File::Default());
    snprintf(configFile, 255, "%s", File::Config());

    config_file << "#######################################################################\n";
    config_file << "#######################################################################\n";
    config_file << "####                                                               ####\n";
    config_file << "#### NOTE: This file is not the configuration file that is used    ####\n";
    config_file << "####       by the STFC Community Mod.  It is provided to help      ####\n";
    config_file << "####       see what configuration is being used by the runtime     ####\n";
    config_file << "####       and any desired settings should be copied to the same   ####\n";
    config_file << "####       section in: " << defaultFile << "\n";
    config_file << "####                                                               ####\n";
    config_file << "####        Config in: " << configFile << "\n";
    config_file << "####                                                               ####\n";
    config_file << "#######################################################################\n";
    config_file << "#######################################################################\n\n";
  }

  config_file << config;
  config_file.close();
}

Config& Config::Get()
{
  static Config config;
  return config;
}

NotificationSound Config::NotificationSoundForToast(int toast_state) const
{
  const auto alert = std::ranges::find(kToastAudioAlerts, toast_state, &ToastAudioAlertConfig::toast_state);
  return alert == kToastAudioAlerts.end() ? NotificationSound::None : this->*(alert->config_member);
}

MissionHudVisibility Config::MissionHudButtonVisibility(std::string_view button_name) const
{
  const auto it = this->mission_hud_buttons.find(std::string(button_name));
  return it == this->mission_hud_buttons.end() ? MissionHudVisibility::Auto : it->second;
}

#if _WIN32
static HMONITOR lastMonitor = (HMONITOR)-1;
static float    dpi         = 1.0f;

HWND Config::WindowHandle()
{
  static HWND hwnd = nullptr;

  if (hwnd == nullptr) {
    DWORD processId = GetCurrentProcessId();
    hwnd            = GetTopWindow(nullptr); // Start with the first top-level window

    while (hwnd != nullptr) {
      DWORD windowProcessId;
      GetWindowThreadProcessId(hwnd, &windowProcessId);

      // Check if the window belongs to the current process and is the main window
      if (windowProcessId == processId && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd)) {
        break;
      }

      hwnd = GetNextWindow(hwnd, GW_HWNDNEXT); // Move to the next top-level window
    }
  }

  return hwnd;
}

float Config::RefreshDPI()
{
  lastMonitor = (HMONITOR)-1;

  return Config::GetDPI();
}

float Config::GetDPI()
{
  auto     activeWindow = GetActiveWindow();
  HMONITOR monitor      = MonitorFromWindow(activeWindow, MONITOR_DEFAULTTONEAREST);

  if (monitor != lastMonitor) {
    // Get the logical width and height of the monitor
    MONITORINFOEX monitorInfoEx;
    monitorInfoEx.cbSize = sizeof(monitorInfoEx);
    GetMonitorInfo(monitor, &monitorInfoEx);
    auto cxLogical = monitorInfoEx.rcMonitor.right - monitorInfoEx.rcMonitor.left;
    auto cyLogical = monitorInfoEx.rcMonitor.bottom - monitorInfoEx.rcMonitor.top;

    // Get the physical width and height of the monitor
    DEVMODE devMode;
    devMode.dmSize        = sizeof(devMode);
    devMode.dmDriverExtra = 0;
    EnumDisplaySettings(monitorInfoEx.szDevice, ENUM_CURRENT_SETTINGS, &devMode);
    auto cxPhysical = devMode.dmPelsWidth;
    auto cyPhysical = devMode.dmPelsHeight;

    // Calculate the scaling factor
    auto horizontalScale = ((double)cxPhysical / (double)cxLogical);
    auto verticalScale   = ((double)cyPhysical / (double)cyLogical);

    spdlog::trace("Horizontal scaling: {}", horizontalScale);
    spdlog::trace("Vertical scaling: {}", verticalScale);

    dpi         = horizontalScale;
    lastMonitor = monitor;
  }

  return dpi;
}
#else
float Config::RefreshDPI()
{
  return Config::GetDPI();
}

float Config::GetDPI()
{
  return 1.0f;
}
#endif

void Config::AdjustUiScale(bool scaleUp)
{
  if (this->ui_scale != 0.0f) {
    auto old_scale    = this->ui_scale;
    auto scale_factor = (scaleUp ? 1.0f : -1.0f) * this->ui_scale_step;
    auto new_scale    = this->ui_scale + scale_factor;
    this->ui_scale    = std::clamp(new_scale, 0.1f, 2.0f);

    auto dpi = Config::RefreshDPI();
    spdlog::info("UI has been scaled {}, was {}, now {} (unclamped {}) @ {} DPI Scaling", (scaleUp ? "UP" : "DOWN"),
                 old_scale, this->ui_scale, new_scale, dpi);
  }
}

void Config::AdjustUiViewerScale(bool scaleUp)
{
  if (this->ui_scale_viewer != 0.0f) {
    auto old_scale        = this->ui_scale_viewer;
    auto scale_factor     = (scaleUp ? 1.0f : -1.0f) * this->ui_scale_step;
    auto new_scale        = this->ui_scale_viewer + (scale_factor * 0.25f);
    this->ui_scale_viewer = std::clamp(new_scale, 0.1f, 2.0f);

    spdlog::info("UI Viewer has been scaled {}, was {}, now {} (unclamped {})", (scaleUp ? "UP" : "DOWN"), old_scale,
                 this->ui_scale_viewer, new_scale);
  }
}

void Config::AdjustUiShipScale(bool scaleUp)
{
  const auto old_scale    = this->ui_scale_ship;
  const auto scale_factor = (scaleUp ? 1.0f : -1.0f) * this->ui_scale_step;
  const auto new_scale    = this->ui_scale_ship + scale_factor;
  this->ui_scale_ship     = std::clamp(new_scale, 0.1f, 20.0f);

  spdlog::info("System ship models have been scaled {}, was {}, now {} (unclamped {})", (scaleUp ? "UP" : "DOWN"),
               old_scale, this->ui_scale_ship, new_scale);
  ApplyUiShipScaleToLoadedShips(old_scale, this->ui_scale_ship);
}

inline std::string mask_token(const std::string& token)
{
  if (token.size() > 21) {
    std::string masked = token;
    for (size_t i = 9; i < token.size() - 12; ++i) {
      if (masked[i] != '-') {
        masked[i] = '*';
      }
    }
    return masked;
  }

  return token;
}

std::string get_config_type_as_string(const toml::node_type type)
{
  switch (type) {
    case toml::node_type::none:
      return "Not-a-node.";
    case toml::node_type::table:
      return "toml::table.";
    case toml::node_type::array:
      return "toml::array.";
    case toml::node_type::string:
      return "toml::value<std::string>.";
    case toml::node_type::integer:
      return "toml::value<int64_t>.";
    case toml::node_type::floating_point:
      return "toml::value<double>.";
    case toml::node_type::boolean:
      return "toml::value<bool>.";
    case toml::node_type::date:
      return "toml::value<date>.";
    case toml::node_type::time:
      return "toml::value<time>.";
    case toml::node_type::date_time:
      return "toml::value<date_time>.";
  };

  return "The node type is unknown";
}

template <typename T>
T get_config_or_default(toml::table& config, toml::table& new_config, std::string_view section, std::string_view item,
                        T default_value, bool write_log)
{
  new_config.emplace<toml::table>(section, toml::table());

  auto sectionTable = new_config[section];
  T    final_value  = default_value;

  try {
    if (config.contains(section)) {
      auto parsed_value = (T)config[section][item].value_or(default_value);
      final_value       = parsed_value;
    }
  } catch (...) {
    spdlog::warn("invalid config value {}.{}", section, item);
  }

  sectionTable.as_table()->insert_or_assign(item, final_value);

  if (write_log) {
    spdlog::debug("config value {}.{} value: {}", section, item, final_value);
  }

  return (T)final_value;
}

NotificationSound get_notification_sound(toml::table& config, toml::table& new_config, std::string_view item,
                                         std::string_view default_value, bool write_log)
{
  const auto value = get_config_or_default<std::string>(config, new_config, "audio", item,
                                                         std::string(default_value), false);
  auto sound = notification_sound_from_name(StripAsciiWhitespace(value));
  if (!sound.has_value()) {
    spdlog::warn("invalid config value audio.{}: '{}'; using {}", item, value, default_value);
    sound = notification_sound_from_name(default_value);
  }

  const auto result = sound.value_or(NotificationSound::None);
  new_config["audio"].as_table()->insert_or_assign(item, notification_sound_name(result));
  if (write_log) {
    spdlog::debug("config value audio.{} value: {}", item, notification_sound_name(result));
  }
  return result;
}

template <typename T>
T get_config_or_default_with_alias(toml::table& config, toml::table& new_config, std::string_view section,
                                   std::string_view item, std::string_view alias, T default_value, bool write_log)
{
  if (!config[section][item] && config[section][alias]) {
    config[section].as_table()->insert_or_assign(item, config[section][alias]);
  }

  return get_config_or_default(config, new_config, section, item, default_value, write_log);
}

std::string_view to_string(MissionHudVisibility visibility)
{
  switch (visibility) {
    case MissionHudVisibility::Always:
      return "always";
    case MissionHudVisibility::Never:
      return "never";
    case MissionHudVisibility::Auto:
    default:
      return "auto";
  }
}

MissionHudVisibility parse_mission_hud_visibility(std::string_view item, std::string_view value)
{
  const auto normalized = AsciiStrToUpper(StripAsciiWhitespace(value));

  if (normalized == "ALWAYS") {
    return MissionHudVisibility::Always;
  }
  if (normalized == "NEVER") {
    return MissionHudVisibility::Never;
  }
  if (normalized == "AUTO" || normalized.empty()) {
    return MissionHudVisibility::Auto;
  }

  spdlog::warn("invalid config value ui.{}: '{}'; using auto", item, value);
  return MissionHudVisibility::Auto;
}

MissionHudVisibility get_mission_hud_visibility(toml::table& config, toml::table& new_config, std::string_view item,
                                                std::string_view default_value, bool write_log)
{
  const auto value = config["ui"][item].value<std::string>().value_or(std::string(default_value));
  const auto mode  = parse_mission_hud_visibility(item, value);

  new_config.emplace<toml::table>("ui", toml::table());
  new_config["ui"].as_table()->insert_or_assign(item, std::string(to_string(mode)));

  if (write_log) {
    spdlog::debug("config value ui.{} value: {}", item, to_string(mode));
  }

  return mode;
}

std::string_view to_string(InstantWarpConfirmation confirmation)
{
  switch (confirmation) {
    case InstantWarpConfirmation::Warp:
      return "warp";
    case InstantWarpConfirmation::Jump:
      return "jump";
    case InstantWarpConfirmation::None:
    default:
      return "none";
  }
}

InstantWarpConfirmation parse_auto_confirm_instant_warp(std::string_view value)
{
  const auto normalized = AsciiStrToUpper(StripAsciiWhitespace(value));

  if (normalized == "WARP" || normalized == "LEFT") {
    return InstantWarpConfirmation::Warp;
  }
  if (normalized == "JUMP" || normalized == "RIGHT") {
    return InstantWarpConfirmation::Jump;
  }
  if (normalized == "NONE" || normalized.empty()) {
    return InstantWarpConfirmation::None;
  }

  spdlog::warn("invalid config value ui.auto_confirm_instant_warp: '{}'; using none", value);
  return InstantWarpConfirmation::None;
}

InstantWarpConfirmation get_auto_confirm_instant_warp(toml::table& config, toml::table& new_config,
                                                      std::string_view default_value, bool write_log)
{
  const auto value = config["ui"]["auto_confirm_instant_warp"].value<std::string>().value_or(
      std::string(default_value));
  const auto confirmation = parse_auto_confirm_instant_warp(value);

  new_config.emplace<toml::table>("ui", toml::table());
  new_config["ui"].as_table()->insert_or_assign("auto_confirm_instant_warp", std::string(to_string(confirmation)));

  if (write_log) {
    spdlog::debug("config value ui.auto_confirm_instant_warp value: {}", to_string(confirmation));
  }

  return confirmation;
}

std::string_view to_string(FleetLabelDetail detail)
{
  switch (detail) {
    case FleetLabelDetail::Expanded:
      return "expanded";
    case FleetLabelDetail::Compact:
      return "compact";
    case FleetLabelDetail::Threshold:
      return "threshold";
    case FleetLabelDetail::Native:
    default:
      return "native";
  }
}

FleetLabelDetail parse_fleet_label_detail(std::string_view key, std::string_view value)
{
  const auto trimmed    = std::string(StripAsciiWhitespace(value));
  const auto normalized = AsciiStrToUpper(trimmed);

  if (normalized == "EXPANDED" || normalized == "ALWAYS") {
    return FleetLabelDetail::Expanded;
  }
  if (normalized == "COMPACT" || normalized == "NEVER") {
    return FleetLabelDetail::Compact;
  }
  if (normalized == "THRESHOLD" || normalized == "CUSTOM") {
    return FleetLabelDetail::Threshold;
  }
  if (normalized == "NATIVE" || normalized == "AUTO" || normalized == "NONE" || normalized == "OFF"
      || normalized.empty()) {
    return FleetLabelDetail::Native;
  }

  spdlog::warn("invalid config value graphics.{}: '{}'; using native", key, value);
  return FleetLabelDetail::Native;
}

FleetLabelDetail get_fleet_label_detail(toml::table& config, toml::table& new_config, std::string_view key,
                                        std::string_view default_value, bool write_log)
{
  const auto value  = config["graphics"][key].value<std::string>().value_or(std::string(default_value));
  const auto detail = parse_fleet_label_detail(key, value);

  new_config.emplace<toml::table>("graphics", toml::table());
  new_config["graphics"].as_table()->insert_or_assign(key, std::string(to_string(detail)));

  if (write_log) {
    spdlog::debug("config value graphics.{} value: {}", key, to_string(detail));
  }

  return detail;
}

float get_fleet_label_zoom_threshold(toml::table& config, toml::table& new_config, std::string_view key,
                                     float default_value, bool write_log)
{
  auto threshold = config["graphics"][key].value<float>().value_or(default_value);
  if (!std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f) {
    spdlog::warn("invalid config value graphics.{}: {}; using {}", key, threshold, default_value);
    threshold = default_value;
  }

  new_config.emplace<toml::table>("graphics", toml::table());
  new_config["graphics"].as_table()->insert_or_assign(key, threshold);

  if (write_log) {
    spdlog::debug("config value graphics.{} value: {}", key, threshold);
  }

  return threshold;
}

void parse_ship_filter(std::string_view value, std::vector<std::string>& names, bool& match_all)
{
  names.clear();
  match_all = false;
  if (AsciiStrToUpper(StripAsciiWhitespace(value)) == "*") {
    match_all = true;
    return;
  }
  for (const auto& token : StrSplit(std::string(value), ',')) {
    auto stripped = StripAsciiWhitespace(token);
    if (stripped.empty()) continue;
    if (auto normalized = ShipNameMatch::NormalizeKey(stripped); !normalized.empty()) {
      names.emplace_back(std::move(normalized));
    }
  }
}

void read_instant_warp_filter(toml::table& config, toml::table& new_config, std::string_view key,
                              std::vector<std::string>& names, bool& match_all,
                              std::string_view default_value, bool write_log)
{
  const auto value = config["ui"][key].value<std::string>().value_or(std::string(default_value));
  parse_ship_filter(value, names, match_all);

  new_config.emplace<toml::table>("ui", toml::table());
  new_config["ui"].as_table()->insert_or_assign(std::string(key), std::string(value));

  if (write_log) {
    spdlog::debug("config value ui.{}: {}", key, value);
  }
}

void parse_faction_filter(std::string_view value, std::vector<std::string>& factions)
{
  static constexpr std::array kKnownFactions{"federation", "klingon", "romulan"};

  factions.clear();
  bool none = false;
  for (const auto& token : StrSplit(std::string(value), ',')) {
    const auto stripped = StripAsciiWhitespace(token);
    if (stripped.empty())
      continue;

    auto lowered = AsciiStrToLower(std::string(stripped));

    if (lowered == "none") {
      none = true;
      continue;
    }
    if (std::ranges::find(kKnownFactions, lowered) == kKnownFactions.end()) {
      spdlog::warn("Unrecognised faction '{}' in daily_bulk_claim_factions; expected one of: federation, klingon, "
                   "romulan, or none by itself. Ignoring.",
                   stripped);
      continue;
    }

    if (std::ranges::find(factions, lowered) == factions.end()) {
      factions.emplace_back(std::move(lowered));
    }
  }
  if (none) {
    if (!factions.empty())
      spdlog::warn("daily_bulk_claim_factions: none overrides named factions; use none by itself.");
    factions = {"none"};
  }
}

void read_daily_bulk_claim_factions(toml::table& config, toml::table& new_config, std::vector<std::string>& factions,
                                    std::string_view default_value, bool write_log)
{
  auto value = config["ui"]["daily_bulk_claim_factions"].value<std::string>().value_or(std::string(default_value));
  // Blank has the same meaning as an omitted setting. Materialize the default
  // for both gameplay and generated config values; none is the explicit opt-out.
  if (StripAsciiWhitespace(value).empty())
    value = std::string(default_value);
  parse_faction_filter(value, factions);

  new_config.emplace<toml::table>("ui", toml::table());
  new_config["ui"].as_table()->insert_or_assign("daily_bulk_claim_factions", std::string(value));

  if (write_log) {
    spdlog::debug("config value ui.daily_bulk_claim_factions: {}", value);
  }
}

void read_sync_targets(toml::table& config, toml::table& new_config,
                       std::map<std::string, SyncTargetConfig>& sync_targets, const SyncConfig& defaults)
{
  if (!config.contains("sync")) {
    return;
  }

  const auto sync = config["sync"].as_table();
  if (!sync || !sync->contains("targets")) {
    return;
  }

  const auto targets = config["sync"]["targets"].as_table();
  if (!targets) {
    return;
  }

  for (const auto& [target_key, target_config] : *targets) {
    if (!target_config.is_table()) {
      continue;
    }

    const std::string target_section = "sync.targets." + std::string(target_key.str());

    SyncTargetConfig target;
    toml::table      parsed_target;

    const auto& values = *target_config.as_table();
    if (values.contains("url") && values.contains("token")) {
      auto url   = values["url"].value<std::string>();
      auto token = values["token"].value<std::string>();
      auto proxy = values["proxy"].value<std::string>();

      if (!url.has_value() || !token.has_value() || url->empty() || token->empty()) {
        continue;
      }

      target.url        = url.value();
      target.token      = token.value();
      target.proxy      = proxy.value_or(defaults.proxy);
      target.verify_ssl = values["verify_ssl"].value<bool>().value_or(defaults.verify_ssl);

      parsed_target.insert("url", target.url);
      parsed_target.insert("token", target.token);
      parsed_target.insert("proxy", target.proxy);
      parsed_target.insert("verify_ssl", target.verify_ssl);
    } else {
      spdlog::warn("Skipping invalid target [{}]. Missing url or token.", target_section);
      continue;
    }

    for (const auto& opt : SyncOptions) {
      if (opt.type == SyncConfig::Type::Officer && !values.contains(opt.option_str)) {
        target.*opt.option = values["officer"].value<bool>().value_or(defaults.*opt.option);
      } else {
        target.*opt.option = values[opt.option_str].value<bool>().value_or(defaults.*opt.option);
      }
      parsed_target.insert(opt.option_str, target.*opt.option);
    }

    if (sync_targets.emplace(target_key.str(), target).second) {
      new_config["sync"]["targets"].as_table()->insert_or_assign(target_key.str(), parsed_target);
      spdlog::debug("config value {} url: {}, token: {}", target_section, target.url, mask_token(target.token));
      spdlog::info("target [{}] proxy: '{}', verify_ssl: {}", target_section, target.proxy, target.verify_ssl);
    }
  }
}

struct ShortcutConfigValue {
  std::string value;
  std::string source_item;
  bool        from_config = false;
  bool        valid_type  = true;
};

std::string shortcut_source_label(const ShortcutConfigValue& shortcut_value, std::string_view item)
{
  if (!shortcut_value.valid_type) {
    return "invalid";
  }
  if (!shortcut_value.from_config) {
    return "default";
  }
  if (shortcut_value.source_item == item) {
    return "config";
  }

  std::string label = "alias:";
  label.append(shortcut_value.source_item);
  return label;
}

void set_shortcut_source(toml::node_view<toml::node> sourceTable, std::string_view item, std::string_view source)
{
  sourceTable.as_table()->insert_or_assign(item, std::string(source));
}

void set_shortcut_noop(toml::node_view<toml::node> sectionTable, toml::node_view<toml::node> sourceTable,
                       std::string_view item, std::string_view source)
{
  sectionTable.as_table()->insert_or_assign(item, "NONE");
  set_shortcut_source(sourceTable, item, source);
  spdlog::debug("shortcut value shortcuts.{} value: NONE", item);
}

void parse_config_shortcut_value(toml::table& new_config, std::string_view item, GameFunction gameFunction,
                                 std::string_view default_value, const ShortcutConfigValue& shortcut_value)
{
  auto section = "shortcuts";
  auto source  = "shortcuts_source";

  new_config.emplace<toml::table>(section, toml::table());
  new_config.emplace<toml::table>(source, toml::table());

  auto sectionTable = new_config[section];
  auto sourceTable  = new_config[source];
  auto sourceLabel  = shortcut_source_label(shortcut_value, item);

  if (!shortcut_value.valid_type) {
    spdlog::error("Invalid shortcut value [shortcuts].{} must be a string; using default for [shortcuts].{}.",
                  shortcut_value.source_item, item);
    return parse_config_shortcut_value(new_config, item, gameFunction, default_value,
                                       {std::string(default_value), std::string(item), false, true});
  }

  auto config_value = shortcut_value.value;
  auto valueTrimmed = StripTrailingAsciiWhitespace(config_value);
  auto valueLowered = AsciiStrToUpper(valueTrimmed);

  // An explicit empty string disables the binding, just like NONE. Missing or
  // invalid settings still fall back to their defaults.
  if (valueTrimmed.empty() || valueLowered == "NONE") {
    set_shortcut_noop(sectionTable, sourceTable, item, sourceLabel);
    return;
  }

  auto wantedKeys = StrSplit(valueLowered, '|');

  bool keyAdded = false;
  for (std::string_view wantedKey : wantedKeys) {
    MapKey mapKey = MapKey::Parse(wantedKey);

    if (mapKey.Key != KeyCode::None) {
      keyAdded = true;
      MapKey::AddMappedKey(gameFunction, std::move(mapKey));
    } else if (!wantedKey.empty()) {
      spdlog::warn("Invalid shortcut token [shortcuts].{} token='{}' value='{}'; ignoring token.",
                   shortcut_value.source_item, wantedKey, config_value);
    }
  }

  if (!keyAdded) {
    if (shortcut_value.from_config) {
      spdlog::error("No valid shortcut tokens for [shortcuts].{} value='{}'; using default for [shortcuts].{}.",
                    shortcut_value.source_item, config_value, item);
      return parse_config_shortcut_value(new_config, item, gameFunction, default_value,
                                         {std::string(default_value), std::string(item), false, true});
    } else {
      spdlog::error("Default shortcut [shortcuts].{} value='{}' has no valid tokens; action disabled.", item,
                    config_value);
    }
    set_shortcut_noop(sectionTable, sourceTable, item, "invalid");
    return;
  }

  auto shortcut = MapKey::GetShortcuts(gameFunction);
  sectionTable.as_table()->insert_or_assign(item, shortcut);
  set_shortcut_source(sourceTable, item, sourceLabel);

  spdlog::debug("shortcut value {}.{} value: {}", section, item, shortcut);
}

bool shortcut_key_exists(toml::table& config, std::string_view item)
{
  auto shortcuts = config["shortcuts"].as_table();
  return shortcuts && shortcuts->contains(item);
}

ShortcutConfigValue get_shortcut_value_or_default(toml::table& config, std::string_view item,
                                                  std::string_view default_value)
{
  if (!shortcut_key_exists(config, item)) {
    return {std::string(default_value), std::string(item), false, true};
  }

  auto value = config["shortcuts"][item].value<std::string>();
  if (!value) {
    return {"", std::string(item), true, false};
  }

  return {*value, std::string(item), true, true};
}

void parse_config_shortcut(toml::table& config, toml::table& new_config, std::string_view item,
                           GameFunction gameFunction, std::string_view default_value)
{
  auto section = "shortcuts";

  config.emplace<toml::table>(section, toml::table());

  parse_config_shortcut_value(new_config, item, gameFunction, default_value,
                              get_shortcut_value_or_default(config, item, default_value));
}

void parse_config_shortcut_aliases(toml::table& config, toml::table& new_config, std::string_view item,
                                   GameFunction gameFunction, std::string_view default_value,
                                   std::initializer_list<std::string_view> aliases)
{
  auto section = "shortcuts";

  config.emplace<toml::table>(section, toml::table());

  auto       config_value = get_shortcut_value_or_default(config, item, default_value);
  const auto has_item     = shortcut_key_exists(config, item);

  for (const auto alias : aliases) {
    if (!shortcut_key_exists(config, alias)) {
      continue;
    }

    if (has_item) {
      spdlog::warn("Ignoring deprecated shortcut alias [shortcuts].{} because [shortcuts].{} is set.", alias, item);
      continue;
    }

    config_value = get_shortcut_value_or_default(config, alias, default_value);
    spdlog::warn("Deprecated shortcut alias [shortcuts].{} is supported for compatibility; use [shortcuts].{}.",
                 alias, item);
    break;
  }

  parse_config_shortcut_value(new_config, item, gameFunction, default_value, config_value);
}

void migrate_mac_config_if_needed(const char* filename)
{
#if !_WIN32
  namespace fs = std::filesystem;

  fs::path file_path = File::MakePath(filename);
  auto     new_dir   = file_path.parent_path();
  if (fs::exists(file_path) || fs::exists(new_dir))
    return;

  spdlog::info("mac config migration: config dir does not exist, checking for migration...");

  fs::path old_path = File::MakePath(filename, false, true);
  if (!fs::exists(old_path)) {
    spdlog::info("mac config migration: old config does not exist. nothing to migrate.");
    return;
  }

  auto stat = fs::status(old_path);
  if (stat.type() == fs::file_type::regular) {
    spdlog::info("mac config migration: old config found. attempting to migrate...");

    try {
      // move
      auto old_dir = old_path.parent_path();
      fs::rename(old_dir, new_dir);

      // re-create old dir, create symlink
      fs::create_directories(old_dir);
      fs::create_symlink(file_path, old_path);

      // drop update-info.txt
      std::ofstream info;
      info.open(old_dir / "update-info.txt");
      info << "Your config has been moved!\n\n";
      info << "You can now find your config at " << file_path << "\n\n";
      info << "A symlink has been placed for your convenience, but it is generally recommended, that you use the new "
              "path and delete this directory going forward.";
      info.close();

      spdlog::info("mac config migration: config migration done.");
    } catch (std::exception& ex) {
      spdlog::warn("mac config migration: migration failed = {}", ex.what());
    }
  }
#endif
}

void delete_old_vars()
{
  namespace fs = std::filesystem;

  fs::path        old_vars = fs::path(File::MakePath(File::Vars())).parent_path() / FILE_DEF_VARS_OLD;
  std::error_code ignore;
  fs::remove(old_vars, ignore);
}

void Config::Load()
{
  auto filename = File::Config();

  migrate_mac_config_if_needed(filename);
  delete_old_vars();

  toml::table config;
  toml::table parsed;
  bool        write_config = false;
  bool        write_log    = true;
  try {
    config       = std::move(toml::parse_file(File::MakePath(filename)));
    write_config = true;
  } catch (const toml::parse_error& e) {
    spdlog::warn("Failed to load config file, falling back to default settings: {}", e.description());
    spdlog::debug("");
    write_config = false;
    write_log    = false;
  } catch (...) {
    spdlog::warn("Failed to load config file, falling back to default settings");
    spdlog::debug("");
    write_config = false;
    write_log    = false;
  }

  // Patch switches are honoured in release builds too (previously debug-only):
  // needed to selectively disable hook groups that collide with BepInEx plugins
  // hooking the same il2cpp methods (native + managed detour on one prologue crashes).
  this->installUiScaleHooks =
      get_config_or_default(config, parsed, "patches", "uiscalehooks", DCP::uiscalehooks, write_config);
  this->installZoomHooks = get_config_or_default(config, parsed, "patches", "zoomhooks", DCP::zoomhooks, write_config);
  this->installHavenZoomHooks =
      get_config_or_default(config, parsed, "patches", "havenzoomhooks", DCP::havenzoomhooks, write_config);
  this->installInteriorZoomHooks =
      get_config_or_default(config, parsed, "patches", "interiorzoomhooks", DCP::interiorzoomhooks, write_config);
  this->installBuffFixHooks =
      get_config_or_default(config, parsed, "patches", "bufffixhooks", DCP::bufffixhooks, write_config);
  this->installToastBannerHooks =
      get_config_or_default(config, parsed, "patches", "toastbannerhooks", DCP::toastbannerhooks, write_config);
  this->installPanHooks = get_config_or_default(config, parsed, "patches", "panhooks", DCP::panhooks, write_config);
  this->installHotkeyHooks =
      get_config_or_default(config, parsed, "patches", "hotkeyhooks", DCP::hotkeyhooks, write_config);
  this->installFreeResizeHooks =
      get_config_or_default(config, parsed, "patches", "freeresizehooks", DCP::freeresizehooks, write_config);
  this->installTempCrashFixes =
      get_config_or_default(config, parsed, "patches", "tempcrashfixes", DCP::tempcrashfixes, write_config);
  this->installTestPatches =
      get_config_or_default(config, parsed, "patches", "testpatches", DCP::testpatches, write_config);
  this->installMiscPatches =
      get_config_or_default(config, parsed, "patches", "miscpatches", DCP::miscpatches, write_config);
  this->installMissionHudTweaksHooks =
      get_config_or_default(config, parsed, "patches", "missionhudtweakshooks", DCP::missionhudtweakshooks, write_config);
  this->installChatPatches =
      get_config_or_default(config, parsed, "patches", "chatpatches", DCP::chatpatches, write_config);
  this->installPlayerTitleDisplayHooks = get_config_or_default(config, parsed, "patches", "chattitledisplayhooks",
                                                               DCP::chattitledisplayhooks, write_config);
  this->installSyncPatches =
      get_config_or_default(config, parsed, "patches", "syncpatches", DCP::syncpatches, write_config);
  this->installGameVersionHook =
      get_config_or_default(config, parsed, "patches", "game_version", DCP::game_version, write_config);
  this->installObjectTracker =
      get_config_or_default(config, parsed, "patches", "objecttracker", DCP::objecttracker, write_config);
  this->installLoadingScreenHooks =
      get_config_or_default(config, parsed, "patches", "loadingscreenhooks", DCP::loadingscreenhooks, write_config);
  this->installTransitionScreenHooks =
      get_config_or_default(config, parsed, "patches", "transitionscreenhooks", DCP::transitionscreenhooks, write_config);
  this->installGiftsBulkClaimHooks =
      get_config_or_default(config, parsed, "patches", "giftsbulkclaimhooks", DCP::giftsbulkclaimhooks, write_config);
  this->installDailyFactionBulkClaimHooks = get_config_or_default(
      config, parsed, "patches", "dailyfactionbulkclaimhooks", DCP::dailyfactionbulkclaimhooks, write_config);
  this->installGalacticAnomalyTimerHooks =
      get_config_or_default(config, parsed, "patches", "galacticanomalytimerhooks", DCP::galacticanomalytimerhooks, write_config);
  this->installLoadingTipHooks =
      get_config_or_default(config, parsed, "patches", "loadingtiphooks", DCP::loadingtiphooks, write_config);
  this->installDoubleClickAssignShipHooks =
      get_config_or_default(config, parsed, "patches", "doubleclickassignshiphooks", DCP::doubleclickassignshiphooks, write_config);
  this->installForbiddenTechConfirmationHooks =
      get_config_or_default(config, parsed, "patches", "forbiddentechconfirmhooks", DCP::forbiddentechconfirmhooks, write_config);
  this->installForbiddenTechShipNameHooks = get_config_or_default(
      config, parsed, "patches", "forbiddentechshipnamehooks", DCP::forbiddentechshipnamehooks, write_config);
  this->installRevealConfirmationHooks =
      get_config_or_default(config, parsed, "patches", "revealconfirmhooks", DCP::revealconfirmhooks, write_config);
  this->installQueueAddressGuardHooks = get_config_or_default(
      config, parsed, "patches", "queueaddressguardhooks", DCP::queueaddressguardhooks, write_config);
  this->installActionQueueRecoveryHooks =
      get_config_or_default(config, parsed, "patches", "actionqueuerecoveryhooks", DCP::actionqueuerecoveryhooks, write_config);
  this->installThinQueueProtectionHooks =
      get_config_or_default(config, parsed, "patches", "thinqueueprotectionhooks", DCP::thinqueueprotectionhooks, write_config);
  this->installArtifactExchangeHooks =
      get_config_or_default(config, parsed, "patches", "artifactexchangehooks", DCP::artifactexchangehooks, write_config);
  this->installAudioEventHooks =
      get_config_or_default(config, parsed, "patches", "audioeventhooks", DCP::audioeventhooks, write_config);
  this->installInstantCargoCounterHooks =
      get_config_or_default(config, parsed, "patches", "instantcargocounterhooks", DCP::instantcargocounterhooks, write_config);
  this->installCargoFormatHooks =
      get_config_or_default(config, parsed, "patches", "cargoformathooks", DCP::cargoformathooks, write_config);
  this->installOfficerSortHooks =
      get_config_or_default(config, parsed, "patches", "officersorthooks", DCP::officersorthooks, write_config);
  this->installPinnedShipSortHooks =
      get_config_or_default(config, parsed, "patches", "pinnedshiphooks", DCP::pinnedshiphooks, write_config);
  this->installPinnedShipClickToggle = get_config_or_default(config, parsed, "patches", "pinnedshipclicktoggle",
                                                             DCP::pinnedshipclicktoggle, write_config);
  this->installAllianceGamesStoreHooks = get_config_or_default(
      config, parsed, "patches", "alliancegamesstorehooks", DCP::alliancegamesstorehooks, write_config);
  this->installHavenHistoryHooks =
      get_config_or_default(config, parsed, "patches", "havenhistoryhooks", DCP::havenhistoryhooks, write_config);
  spdlog::debug("");
  this->kirshara_fixes = get_config_or_default(config, parsed, "control", "kirshara_fixes",
                                             DCC::kirshara_fixes, write_config);
  if (!config["control"]["kirshara_fixes"]
      && (config["control"]["faster_queue_recovery"] || config["control"]["thin_queue_protection"]
          || config["control"]["queue_address_guard"] || config["advanced"]["queue"]["thin_queue_protection"]))
    spdlog::warn("The separate queue feature settings have been replaced by [control] kirshara_fixes (default=true). "
                 "Set kirshara_fixes=false to disable the combined fixes; old feature settings are ignored.");
  this->queue_enabled =
      get_config_or_default(config, parsed, "control", "queue_enabled", DCC::queue_enabled, write_config);
  this->hotkeys_enabled =
      get_config_or_default(config, parsed, "control", "hotkeys_enabled", DCC::hotkeys_enabled, write_config);
  this->hotkeys_extended =
      get_config_or_default(config, parsed, "control", "hotkeys_extended", DCC::hotkeys_extended, write_config);
  this->primary_requires_visible_target = get_config_or_default(
      config, parsed, "control", "primary_requires_visible_target", DCC::primary_requires_visible_target, write_config);
  this->use_scopely_hotkeys =
      get_config_or_default(config, parsed, "control", "use_scopely_hotkeys", DCC::use_scopely_hotkeys, write_config);
  this->select_timer =
      get_config_or_default(config, parsed, "control", "select_timer", DCC::select_timer, write_config);
  this->enable_experimental =
      get_config_or_default(config, parsed, "control", "enable_experimental", DCC::enable_experimental, write_config);

  spdlog::debug("");

  this->ui_scale = get_config_or_default(config, parsed, "graphics", "ui_scale", DCG::ui_scale, write_config);
  this->ui_scale_step = get_config_or_default_with_alias(config, parsed, "graphics", "ui_scale_step", "ui_scale_adjust",
                                                         DCG::ui_scale_step, write_config);
  this->ui_scale_ship =
      get_config_or_default(config, parsed, "graphics", "ui_scale_ship", DCG::ui_scale_ship, write_config);
  this->ui_scale_viewer =
      get_config_or_default(config, parsed, "graphics", "ui_scale_viewer", DCG::ui_scale_viewer, write_config);
  this->zoom     = get_config_or_default(config, parsed, "graphics", "zoom", DCG::zoom, write_config);
  this->haven_zoom = get_config_or_default(config, parsed, "graphics", "haven_zoom", DCG::haven_zoom, write_config);
  if (!std::isfinite(this->haven_zoom) || this->haven_zoom < 0.0f) {
    spdlog::warn("Invalid haven_zoom {}; using {}", this->haven_zoom, DCG::haven_zoom);
    this->haven_zoom = DCG::haven_zoom;
    parsed["graphics"].as_table()->insert_or_assign("haven_zoom", this->haven_zoom);
  }
  this->interior_zoom_scale =
      get_config_or_default(config, parsed, "graphics", "interior_zoom_scale", DCG::interior_zoom_scale, write_config);
  if (!std::isfinite(this->interior_zoom_scale) || this->interior_zoom_scale < 1.0f) {
    spdlog::warn("Invalid interior_zoom_scale {}; using {}", this->interior_zoom_scale, DCG::interior_zoom_scale);
    this->interior_zoom_scale = DCG::interior_zoom_scale;
    parsed["graphics"].as_table()->insert_or_assign("interior_zoom_scale", this->interior_zoom_scale);
  }
  this->fr_scale = get_config_or_default(config, parsed, "graphics", "fr_scale", DCG::fr_scale, write_config);
  this->zoom_label_player.detail =
      get_fleet_label_detail(config, parsed, "zoom_label_player_detail", DCG::zoom_label_player_detail, write_config);
  this->zoom_label_player.zoom_threshold = get_fleet_label_zoom_threshold(
      config, parsed, "zoom_label_player_threshold", DCG::zoom_label_player_threshold, write_config);
  this->zoom_label_non_player.detail         = get_fleet_label_detail(config, parsed, "zoom_label_non_player_detail",
                                                                      DCG::zoom_label_non_player_detail, write_config);
  this->zoom_label_non_player.zoom_threshold = get_fleet_label_zoom_threshold(
      config, parsed, "zoom_label_non_player_threshold", DCG::zoom_label_non_player_threshold, write_config);
  this->free_resize = get_config_or_default(config, parsed, "graphics", "free_resize", DCG::free_resize, write_config);
  this->allow_cursor =
      get_config_or_default(config, parsed, "graphics", "allow_cursor", DCG::allow_cursor, write_config);
  this->keyboard_zoom_speed =
      get_config_or_default(config, parsed, "graphics", "keyboard_zoom_speed", DCG::keyboard_zoom_speed, write_config);

  if (this->enable_experimental) {
    this->system_pan_momentum = get_config_or_default(config, parsed, "graphics", "system_pan_momentum",
                                                      DCG::system_pan_momentum, write_config);
  }

  spdlog::debug("");

  this->system_pan_momentum_falloff = get_config_or_default(config, parsed, "graphics", "system_pan_momentum_falloff",
                                                            DCG::system_pan_momentum_falloff, write_log);
  this->disable_target_camera_lock = get_config_or_default(config, parsed, "graphics", "disable_target_camera_lock",
                                                         DCG::disable_target_camera_lock, write_log);
  this->borderless_fullscreen =
      get_config_or_default(config, parsed, "graphics", "borderless_fullscreen", DCG::borderless_fullscreen, write_log);
  this->transition_time =
      get_config_or_default(config, parsed, "graphics", "transition_time", DCG::transition_time, write_config);
  this->default_system_zoom =
      get_config_or_default(config, parsed, "graphics", "default_system_zoom", DCG::default_system_zoom, write_config);

  spdlog::debug("");

  this->system_zoom_preset_1   = get_config_or_default(config, parsed, "graphics", "system_zoom_preset_1",
                                                       DCG::system_zoom_preset_1, write_config);
  this->system_zoom_preset_2   = get_config_or_default(config, parsed, "graphics", "system_zoom_preset_2",
                                                       DCG::system_zoom_preset_2, write_config);
  this->system_zoom_preset_3   = get_config_or_default(config, parsed, "graphics", "system_zoom_preset_3",
                                                       DCG::system_zoom_preset_3, write_config);
  this->system_zoom_preset_4   = get_config_or_default(config, parsed, "graphics", "system_zoom_preset_4",
                                                       DCG::system_zoom_preset_4, write_config);
  this->system_zoom_preset_5   = get_config_or_default(config, parsed, "graphics", "system_zoom_preset_5",
                                                       DCG::system_zoom_preset_5, write_config);
  this->use_presets_as_default = get_config_or_default(config, parsed, "graphics", "use_presets_as_default",
                                                       DCG::use_presets_as_default, write_config);

  spdlog::debug("");

  this->use_out_of_dock_power = get_config_or_default(config, parsed, "buffs", "use_out_of_dock_power",
                                                      DCBS::use_out_of_dock_power, write_config);

  spdlog::debug("");

  this->disable_escape_exit =
      get_config_or_default(config, parsed, "ui", "disable_escape_exit", DCU::disable_escape_exit, write_config);
  this->disable_escape_exit_timer = get_config_or_default(config, parsed, "ui", "disable_escape_exit_timer",
                                                          DCU::disable_escape_exit_timer, write_config);
  this->disable_preview_locate =
      get_config_or_default(config, parsed, "ui", "disable_preview_locate", DCU::disable_preview_locate, write_config);
  this->disable_preview_recall =
      get_config_or_default(config, parsed, "ui", "disable_preview_recall", DCU::disable_preview_recall, write_config);
  this->only_show_first_popup = get_config_or_default(config, parsed, "ui", "only_show_first_popup",
                                                      DCU::only_show_first_popup, write_config);
  this->disable_move_keys =
      get_config_or_default(config, parsed, "ui", "disable_move_keys", DCU::disable_move_keys, write_config);
  this->disable_toast_banners =
      get_config_or_default(config, parsed, "ui", "disable_toast_banners", DCU::disable_toast_banners, write_config);
  this->trace_audio_events =
      get_config_or_default(config, parsed, "audio", "trace_events", DCA::trace_events, write_config);
  auto disabled_audio_events = get_config_or_default<std::string>(
      config, parsed, "audio", "disabled_events", DCA::disabled_events, write_config);
  this->disable_all_audio_events = false;
  this->disabled_audio_events.clear();
  for (const auto& event : StrSplit(disabled_audio_events, ',')) {
    auto stripped = StripAsciiWhitespace(event);
    if (is_all_audio_wildcard(stripped)) {
      this->disable_all_audio_events = true;
    } else if (!stripped.empty()) {
      this->disabled_audio_events.emplace_back(stripped);
    }
  }
  bool any_toast_audio_alert_configured = false;
  for (const auto& alert : kToastAudioAlerts) {
    const auto sound = get_notification_sound(config, parsed, alert.config_name, alert.default_sound, write_config);
    this->*(alert.config_member) = sound;
    any_toast_audio_alert_configured |= sound != NotificationSound::None;
  }
  if (!this->installToastBannerHooks && any_toast_audio_alert_configured) {
    spdlog::warn("audio alerts require patches.toastbannerhooks = true");
  }
  this->auto_open_bulk_claim_flyout = get_config_or_default(config, parsed, "ui", "auto_open_bulk_claim_flyout",
                                                            DCU::auto_open_bulk_claim_flyout, write_config);

  read_daily_bulk_claim_factions(config, parsed, this->daily_bulk_claim_factions, DCU::daily_bulk_claim_factions,
                                 write_config);
  this->daily_bulk_claim_toggle_default_on =
      get_config_or_default(config, parsed, "ui", "daily_bulk_claim_toggle_default_on",
                            DCU::daily_bulk_claim_toggle_default_on, write_config);
  this->auto_confirm_instant_warp =
      get_auto_confirm_instant_warp(config, parsed, DCU::auto_confirm_instant_warp, write_config);
  this->installInstantWarpConfirmationHooks = true;
  read_instant_warp_filter(config, parsed, "instant_warp_auto_jump", this->instant_warp_auto_jump,
                           this->instant_warp_auto_jump_all, DCU::instant_warp_auto_jump, write_config);
  read_instant_warp_filter(config, parsed, "instant_warp_auto_warp", this->instant_warp_auto_warp,
                           this->instant_warp_auto_warp_all, DCU::instant_warp_auto_warp, write_config);
  read_instant_warp_filter(config, parsed, "instant_warp_always_ask", this->instant_warp_always_ask,
                           this->instant_warp_always_ask_all, DCU::instant_warp_always_ask, write_config);

  {
    bool unused_match_all = false;
    read_instant_warp_filter(config, parsed, "pinned_ships", this->pinned_ships, unused_match_all, DCU::pinned_ships,
                             write_config);
  }

  this->double_click_to_assign_ship = get_config_or_default(config, parsed, "ui", "double_click_to_assign_ship",
                                                            DCU::double_click_to_assign_ship, write_config);
  this->focus_search = get_config_or_default(config, parsed, "ui", "focus_search", DCU::focus_search, write_config);
  this->format_cargo_values = get_config_or_default_with_alias(
      config, parsed, "ui", "format_cargo_values", "cargo_format", DCU::format_cargo_values, write_config);
  this->officer_sort = get_config_or_default(config, parsed, "ui", "officer_sort", DCU::officer_sort, write_config);
  this->reverse_haven_history = get_config_or_default(config, parsed, "ui", "reverse_haven_history",
                                                       DCU::reverse_haven_history, write_config);

  this->dock_syncmanagement =
      get_config_or_default(config, parsed, "ui", "dock_syncmanagement",
                            DCU::dock_syncmanagement, write_config);
  this->arrow_keys_to_select_ship = get_config_or_default(config, parsed, "ui", "arrow_keys_to_select_ship",
                                                          DCU::arrow_keys_to_select_ship, write_config);

  this->auto_confirm_ft_upgrade =
      get_config_or_default(config, parsed, "ui", "auto_confirm_ft_upgrade",
                                                        DCU::auto_confirm_ft_upgrade, write_config);
  this->forbidden_tech_ship_name = get_config_or_default(
      config, parsed, "ui", "forbidden_tech_ship_name", DCU::forbidden_tech_ship_name, write_config);
  this->auto_confirm_reveal_ability =
      get_config_or_default(config, parsed, "ui", "auto_confirm_reveal_ability",
                                                        DCU::auto_confirm_reveal_ability, write_config);
  this->auto_confirm_alliance_contribution =
      get_config_or_default(config, parsed, "ui", "auto_confirm_alliance_contribution",
                                                        DCU::auto_confirm_alliance_contribution, write_config);

  this->extend_chest_purchase_max = get_config_or_default(config, parsed, "ui", "extend_chest_purchase_max",
                                                          DCU::extend_chest_purchase_max, write_config);
  this->extend_donation_slider =
      get_config_or_default(config, parsed, "ui", "extend_donation_slider", DCU::extend_donation_slider, write_config);
  this->extend_donation_max =
      get_config_or_default(config, parsed, "ui", "extend_donation_max", DCU::extend_donation_max, write_config);

  this->disable_galaxy_chat =
      get_config_or_default(config, parsed, "ui", "disable_galaxy_chat", DCU::disable_galaxy_chat, write_config);
  this->disable_veil_chat =
      get_config_or_default(config, parsed, "ui", "disable_veil_chat", DCU::disable_veil_chat, write_config);
  this->show_player_title =
      get_config_or_default(config, parsed, "ui", "show_player_title", DCU::show_player_title, write_config);
  this->show_cargo_default =
      get_config_or_default(config, parsed, "ui", "show_cargo_default", DCU::show_cargo_default, write_config);
  this->show_player_cargo =
      get_config_or_default(config, parsed, "ui", "show_player_cargo", DCU::show_player_cargo, write_config);
  this->show_station_cargo =
      get_config_or_default(config, parsed, "ui", "show_station_cargo", DCU::show_station_cargo, write_config);
  this->show_hostile_cargo =
      get_config_or_default(config, parsed, "ui", "show_hostile_cargo", DCU::show_hostile_cargo, write_config);
  this->show_armada_cargo =
      get_config_or_default(config, parsed, "ui", "show_armada_cargo", DCU::show_armada_cargo, write_config);

  this->instant_cargo_counter =
      get_config_or_default(config, parsed, "ui", "instant_cargo_counter", DCU::instant_cargo_counter, write_config);
  this->cargo_significant_decimals =
      get_config_or_default(config, parsed, "ui", "cargo_significant_decimals", DCU::cargo_significant_decimals, write_config);

  this->always_skip_reveal_sequence = get_config_or_default(config, parsed, "ui", "always_skip_reveal_sequence",
                                                            DCU::always_skip_reveal_sequence, write_config);
  this->mission_hud_buttons.clear();
  this->mission_hud_buttons.emplace(
      "q_trials", get_mission_hud_visibility(config, parsed, "hud_q_trials", DCU::hud_q_trials, write_config));
  this->mission_hud_buttons.emplace(
      "field_training", get_mission_hud_visibility(config, parsed, "hud_field_training", DCU::hud_field_training,
                                                     write_config));
  this->mission_hud_buttons.emplace(
      "outposts", get_mission_hud_visibility(config, parsed, "hud_outposts", DCU::hud_outposts, write_config));
  this->mission_hud_buttons.emplace(
      "missions", get_mission_hud_visibility(config, parsed, "hud_missions", DCU::hud_missions, write_config));
  this->disable_exchange_all = get_config_or_default_with_alias(
      config, parsed, "ui", "disable_exchange_all", "hide_artifact_exchange_all", DCU::disable_exchange_all, write_config);

  spdlog::debug("");

  this->sync_debug   = get_config_or_default(config, parsed, "sync", "debug", DCS::debug, write_config);
  this->sync_logging = get_config_or_default(config, parsed, "sync", "logging", DCS::logging, write_config);
  this->sync_resolver_cache_ttl =
      get_config_or_default(config, parsed, "sync", "resolver_cache_ttl", DCS::resolver_cache_ttl, write_config);

  SyncConfig sync_defaults;
  sync_defaults.proxy      = get_config_or_default<std::string>(config, parsed, "sync", "proxy", DCS::proxy, write_log);
  sync_defaults.verify_ssl = get_config_or_default(config, parsed, "sync", "verify_ssl", DCS::verify_ssl, write_config);

  for (const auto& opt : SyncOptions) {
    if (opt.type == SyncConfig::Type::Officer) {
      sync_defaults.*opt.option = get_config_or_default_with_alias(
          config, parsed, "sync", opt.option_str, "officer", false, write_config);
    } else {
      sync_defaults.*opt.option = get_config_or_default(config, parsed, "sync", opt.option_str, false, write_config);
    }
  }

  spdlog::debug("");

  toml::table default_targets{
      {"stfcdata", toml::table{{"token", ""}, {"url", DCS::stfcdata_url}}},
      {"nextspocksclub", toml::table{{"token", ""}, {"url", DCS::nextspocksclub_url}}},
      {"spocksclub", toml::table{{"token", ""}, {"url", DCS::spocksclub_url}}},
  };
  parsed["sync"].as_table()->emplace<toml::table>("targets", std::move(default_targets));

  read_sync_targets(config, parsed, this->sync_targets, sync_defaults);

  if (auto sync_file = config["sync"]["file"].value<std::string>();
      sync_file.has_value() && !sync_file.value().empty()) {
    spdlog::error("Deprecation Notice: The 'sync_file' config option has been deprecated and removed. "
                  "For capturing sync output, please use a local HTTP server instead.");
  }

  // set global sync options to what's actually used in targets
  const auto targets_view = this->sync_targets | std::views::values;

  this->sync_options.proxy      = sync_defaults.proxy;
  this->sync_options.verify_ssl = sync_defaults.verify_ssl;

  for (const auto& opt : SyncOptions) {
    this->sync_options.*opt.option =
        std::ranges::any_of(targets_view, [opt](const auto& target) { return target.*opt.option; });
  }

  spdlog::debug("");

  // must explicitly include std::string typing here, or we get back char * which fails us!
  auto disabled_banner_types_str = get_config_or_default<std::string>(config, parsed, "ui", "disabled_banner_types",
                                                                      DCU::disabled_banner_types, write_log);

  this->config_settings_url =
      get_config_or_default<std::string>(config, parsed, "config", "settings_url", DCSC::settings_url, write_log);
  this->config_assets_url_override = get_config_or_default<std::string>(config, parsed, "config", "assets_url_override",
                                                                        DCSC::assets_url_override, write_log);

  // Loading Screen / Transition Screen settings
  this->loader_enabled =
      get_config_or_default(config, parsed, "graphics", "loader_enabled", DCG::loader_enabled, write_log);
  this->loader_transition =
      get_config_or_default(config, parsed, "graphics", "loader_transition", DCG::loader_transition, write_log);
  this->loader_transition_black =
      get_config_or_default(config, parsed, "graphics", "loader_transition_black", DCG::loader_transition_black, write_log);
  // If transition customization is disabled, force black mode (game's default background)
  if (!this->loader_transition)
    this->loader_transition_black = true;
#ifdef _USE_ORIGINAL_BG
  this->loader_transition_black = true;
#endif
  this->loader_image =
      get_config_or_default<std::string>(config, parsed, "graphics", "loader_image", DCG::loader_image, write_log);
  this->loader_logo_scale =
      get_config_or_default(config, parsed, "graphics", "loader_logo_scale", DCG::loader_logo_scale, write_log);
  this->galactic_anomaly_timer =
      get_config_or_default(config, parsed, "graphics", "galactic_anomaly_timer", DCG::galactic_anomaly_timer, write_log);
  this->loader_tip_enabled =
      get_config_or_default(config, parsed, "graphics", "loader_tip_enabled", DCG::loader_tip_enabled, write_log);

  std::vector<std::string> types = StrSplit(disabled_banner_types_str, ',');

  spdlog::debug("");

  std::string       bannerString;
  std::stringstream message;
  message << "Parsing banner strings";

  spdlog::debug(message.str());

  for (const auto& [key, value] : bannerTypes) {
    auto upper_key = AsciiStrToUpper(key);

    for (const std::string_view _type : types) {
      auto stripped_type = StripLeadingAsciiWhitespace(_type);
      auto upper_type    = AsciiStrToUpper(stripped_type);

      if ("ALL" == upper_type || upper_key == upper_type) {
        this->disabled_banner_types.emplace_back(value);
        if (!bannerString.empty()) {
          bannerString.append(", ");
        }
        bannerString.append(key);
      }
    }
  }

  message.str("");
  message << "Final disabledbanner types: " << bannerString;
  spdlog::debug(message.str());

  parsed["ui"].as_table()->insert_or_assign("disabled_banner_types", bannerString);

  // Parse notify_banner_types using the same bannerTypes lookup table
  auto notify_banner_types_str = get_config_or_default<std::string>(config, parsed, "ui", "notify_banner_types",
                                                                    DCU::notify_banner_types, write_log);

  std::vector<std::string> notify_types = StrSplit(notify_banner_types_str, ',');
  std::string              notifyString;

  for (const auto& [key, value] : bannerTypes) {
    auto upper_key = AsciiStrToUpper(key);
    for (const std::string_view _type : notify_types) {
      auto stripped_type = StripLeadingAsciiWhitespace(_type);
      auto upper_type    = AsciiStrToUpper(stripped_type);
      spdlog::warn("TEMP: Notify {} == {}", upper_type, upper_key);
      if ("ALL" == upper_type || upper_key == upper_type) {
        this->notify_banner_types.emplace_back(value);
        if (!notifyString.empty())
          notifyString.append(", ");
        notifyString.append(key);
      }
    }
  }

  spdlog::debug("Final notify banner types: {}", notifyString);
  parsed["ui"].as_table()->insert_or_assign("notify_banner_types", notifyString);

  spdlog::debug("");

  parse_config_shortcut(config, parsed, "move_up", GameFunction::MoveUp, DCSH::move_up);
  parse_config_shortcut(config, parsed, "move_down", GameFunction::MoveDown, DCSH::move_down);
  parse_config_shortcut(config, parsed, "move_left", GameFunction::MoveLeft, DCSH::move_left);
  parse_config_shortcut(config, parsed, "move_right", GameFunction::MoveRight, DCSH::move_right);

  parse_config_shortcut_aliases(config, parsed, "set_hotkeys_disabled", GameFunction::DisableHotKeys,
                                DCSH::set_hotkeys_disabled, {"set_hotkeys_disble", "set_hotkeys_disable"});
  parse_config_shortcut_aliases(config, parsed, "set_hotkeys_enabled", GameFunction::EnableHotKeys,
                                DCSH::set_hotkeys_enabled, {"set_hotkeys_enable"});

  parse_config_shortcut(config, parsed, "select_chatalliance", GameFunction::SelectChatAlliance,
                        DCSH::select_chatalliance);
  parse_config_shortcut(config, parsed, "select_chatglobal", GameFunction::SelectChatGlobal, DCSH::select_chatglobal);
  parse_config_shortcut(config, parsed, "select_chatprivate", GameFunction::SelectChatPrivate,
                        DCSH::select_chatprivate);
  parse_config_shortcut(config, parsed, "quit", GameFunction::Quit, DCSH::quit);

  parse_config_shortcut(config, parsed, "select_ship1", GameFunction::SelectShip1, DCSH::select_ship1);
  parse_config_shortcut(config, parsed, "select_ship2", GameFunction::SelectShip2, DCSH::select_ship2);
  parse_config_shortcut(config, parsed, "select_ship3", GameFunction::SelectShip3, DCSH::select_ship3);
  parse_config_shortcut(config, parsed, "select_ship4", GameFunction::SelectShip4, DCSH::select_ship4);
  parse_config_shortcut(config, parsed, "select_ship5", GameFunction::SelectShip5, DCSH::select_ship5);
  parse_config_shortcut(config, parsed, "select_ship6", GameFunction::SelectShip6, DCSH::select_ship6);
  parse_config_shortcut(config, parsed, "select_ship7", GameFunction::SelectShip7, DCSH::select_ship7);
  parse_config_shortcut(config, parsed, "select_ship8", GameFunction::SelectShip8, DCSH::select_ship8);
  parse_config_shortcut(config, parsed, "select_current", GameFunction::SelectCurrent, DCSH::select_current);
  parse_config_shortcut(config, parsed, "focus_search", GameFunction::FocusSearch, DCSH::focus_search);

  parse_config_shortcut(config, parsed, "action_primary", GameFunction::ActionPrimary, DCSH::action_primary);
  parse_config_shortcut(config, parsed, "action_secondary", GameFunction::ActionSecondary, DCSH::action_secondary);
  parse_config_shortcut(config, parsed, "action_queue", GameFunction::ActionQueue, DCSH::action_queue);
  parse_config_shortcut(config, parsed, "action_queue_clear", GameFunction::ActionQueueClear, DCSH::action_queue_clear);
  parse_config_shortcut(config, parsed, "action_view", GameFunction::ActionView, DCSH::action_view);
  parse_config_shortcut(config, parsed, "action_recall", GameFunction::ActionRecall, DCSH::action_recall);
  parse_config_shortcut(config, parsed, "action_recall_cancel", GameFunction::ActionRecallCancel,
                        DCSH::action_recall_cancel);
  parse_config_shortcut(config, parsed, "action_repair", GameFunction::ActionRepair, DCSH::action_repair);
  parse_config_shortcut(config, parsed, "show_chat", GameFunction::ShowChat, DCSH::show_chat);
  parse_config_shortcut(config, parsed, "show_chatside1", GameFunction::ShowChatSide1, DCSH::show_chatside1);
  parse_config_shortcut(config, parsed, "show_chatside2", GameFunction::ShowChatSide2, DCSH::show_chatside2);
  parse_config_shortcut(config, parsed, "show_galaxy", GameFunction::ShowGalaxy, DCSH::show_galaxy);
  parse_config_shortcut_aliases(config, parsed, "show_galaxy_native", GameFunction::NativeShortcutGalaxy,
                                DCSH::show_galaxy_native, {"native_shortcut_galaxy"});
  parse_config_shortcut(config, parsed, "show_system", GameFunction::ShowSystem, DCSH::show_system);
  parse_config_shortcut(config, parsed, "zoom_preset1", GameFunction::ZoomPreset1, DCSH::zoom_preset1);
  parse_config_shortcut(config, parsed, "zoom_preset2", GameFunction::ZoomPreset2, DCSH::zoom_preset2);
  parse_config_shortcut(config, parsed, "zoom_preset3", GameFunction::ZoomPreset3, DCSH::zoom_preset3);
  parse_config_shortcut(config, parsed, "zoom_preset4", GameFunction::ZoomPreset4, DCSH::zoom_preset4);
  parse_config_shortcut(config, parsed, "zoom_preset5", GameFunction::ZoomPreset5, DCSH::zoom_preset5);
  parse_config_shortcut(config, parsed, "zoom_in", GameFunction::ZoomIn, DCSH::zoom_in);
  parse_config_shortcut(config, parsed, "zoom_out", GameFunction::ZoomOut, DCSH::zoom_out);
  parse_config_shortcut(config, parsed, "zoom_max", GameFunction::ZoomMax, DCSH::zoom_max);
  parse_config_shortcut(config, parsed, "zoom_min", GameFunction::ZoomMin, DCSH::zoom_min);
  parse_config_shortcut(config, parsed, "zoom_reset", GameFunction::ZoomReset, DCSH::zoom_reset);
  parse_config_shortcut(config, parsed, "ui_scaleup", GameFunction::UiScaleUp, DCSH::ui_scaleup);
  parse_config_shortcut(config, parsed, "ui_scaledown", GameFunction::UiScaleDown, DCSH::ui_scaledown);
  parse_config_shortcut(config, parsed, "ui_scaleshipup", GameFunction::UiShipScaleUp, DCSH::ui_scaleshipup);
  parse_config_shortcut(config, parsed, "ui_scaleshipdown", GameFunction::UiShipScaleDown, DCSH::ui_scaleshipdown);
  parse_config_shortcut(config, parsed, "ui_scaleviewerup", GameFunction::UiViewerScaleUp, DCSH::ui_scaleviewerup);
  parse_config_shortcut(config, parsed, "ui_scaleviewerdown", GameFunction::UiViewerScaleDown,
                        DCSH::ui_scaleviewerdown);

  parse_config_shortcut(config, parsed, "log_debug", GameFunction::LogLevelDebug, DCSH::log_debug);
  parse_config_shortcut(config, parsed, "log_trace", GameFunction::LogLevelTrace, DCSH::log_trace);
  parse_config_shortcut(config, parsed, "log_info", GameFunction::LogLevelInfo, DCSH::log_info);
  parse_config_shortcut(config, parsed, "log_warn", GameFunction::LogLevelWarn, DCSH::log_warn);
  parse_config_shortcut(config, parsed, "log_error", GameFunction::LogLevelError, DCSH::log_error);
  parse_config_shortcut(config, parsed, "log_off", GameFunction::LogLevelOff, DCSH::log_off);
  parse_config_shortcut(config, parsed, "restart", GameFunction::Restart, DCSH::restart);

  parse_config_shortcut(config, parsed, "show_awayteam", GameFunction::ShowAwayTeam, DCSH::show_awayteam);
  parse_config_shortcut(config, parsed, "show_gifts", GameFunction::ShowGifts, DCSH::show_gifts);
  parse_config_shortcut(config, parsed, "show_artifacts", GameFunction::ShowArtifacts, DCSH::show_artifacts);
  parse_config_shortcut(config, parsed, "show_commander", GameFunction::ShowCommander, DCSH::show_commander);
  parse_config_shortcut(config, parsed, "show_daily", GameFunction::ShowDaily, DCSH::show_daily);
  parse_config_shortcut(config, parsed, "show_events", GameFunction::ShowEvents, DCSH::show_events);
  parse_config_shortcut_aliases(config, parsed, "show_events_native", GameFunction::NativeShortcutEvents,
                                DCSH::show_events_native, {"native_shortcut_events"});
  parse_config_shortcut(config, parsed, "show_exocomp", GameFunction::ShowExoComp, DCSH::show_exocomp);
  parse_config_shortcut(config, parsed, "show_factions", GameFunction::ShowFactions, DCSH::show_factions);
  parse_config_shortcut(config, parsed, "show_inventory", GameFunction::ShowInventory, DCSH::show_inventory);
  parse_config_shortcut(config, parsed, "show_missions", GameFunction::ShowMissions, DCSH::show_missions);
  parse_config_shortcut(config, parsed, "show_research", GameFunction::ShowResearch, DCSH::show_research);
  parse_config_shortcut(config, parsed, "show_scrapyard", GameFunction::ShowScrapYard, DCSH::show_scrapyard);
  parse_config_shortcut(config, parsed, "show_settings", GameFunction::ShowSettings, DCSH::show_settings);
  parse_config_shortcut(config, parsed, "toggle_shortcut_hints", GameFunction::ToggleShortcutHints,
                        DCSH::toggle_shortcut_hints);
  parse_config_shortcut(config, parsed, "show_officers", GameFunction::ShowOfficers, DCSH::show_officers);
  parse_config_shortcut(config, parsed, "show_officerpresets", GameFunction::ShowOfficerPresets,
                        DCSH::show_officerpresets);
  parse_config_shortcut(config, parsed, "show_officerswap", GameFunction::ShowOfficerSwap, DCSH::show_officerswap);
  parse_config_shortcut(config, parsed, "show_qtrials", GameFunction::ShowQTrials, DCSH::show_qtrials);
  parse_config_shortcut(config, parsed, "show_refinery", GameFunction::ShowRefinery, DCSH::show_refinery);
  parse_config_shortcut(config, parsed, "show_ships", GameFunction::ShowShips, DCSH::show_ships);
  parse_config_shortcut(config, parsed, "show_shipconstruction", GameFunction::ShowShipConstruction,
                        DCSH::show_shipconstruction);
  parse_config_shortcut(config, parsed, "show_shipswap", GameFunction::ShowShipSwap, DCSH::show_shipswap);
  parse_config_shortcut(config, parsed, "show_shields", GameFunction::ShowShields, DCSH::show_shields);
  parse_config_shortcut(config, parsed, "show_battlelogs", GameFunction::ShowBattlelogs, DCSH::show_battlelogs);
  parse_config_shortcut(config, parsed, "show_stationexterior", GameFunction::ShoWStationExterior,
                        DCSH::show_stationexterior);
  parse_config_shortcut(config, parsed, "show_stationinterior", GameFunction::ShowStationInterior,
                        DCSH::show_stationinterior);
  parse_config_shortcut(config, parsed, "show_haven", GameFunction::ShowHaven, DCSH::show_haven);
  parse_config_shortcut(config, parsed, "toggle_queue", GameFunction::ToggleQueue, DCSH::toggle_queue);
  parse_config_shortcut(config, parsed, "toggle_instant_warp", GameFunction::ToggleAutoConfirmInstantWarp,
                        DCSH::toggle_instant_warp);

  if (this->hotkeys_extended) {
    parse_config_shortcut(config, parsed, "show_alliance", GameFunction::ShowAlliance, DCSH::show_alliance);

    if (this->enable_experimental) {
      parse_config_shortcut(config, parsed, "show_alliance_help", GameFunction::ShowAllianceHelp,
                            DCSH::show_alliance_help);
      parse_config_shortcut(config, parsed, "show_alliance_armada", GameFunction::ShowAllianceArmada,
                            DCSH::show_alliance_armada);
    }

    parse_config_shortcut(config, parsed, "show_bookmarks", GameFunction::ShowBookmarks, DCSH::show_bookmarks);

    if (this->enable_experimental) {
      parse_config_shortcut(config, parsed, "show_lookup", GameFunction::ShowLookup, DCSH::show_lookup);
    }

    parse_config_shortcut(config, parsed, "set_zoom_preset1", GameFunction::SetZoomPreset1, DCSH::set_zoom_preset1);
    parse_config_shortcut(config, parsed, "set_zoom_preset2", GameFunction::SetZoomPreset2, DCSH::set_zoom_preset2);
    parse_config_shortcut(config, parsed, "set_zoom_preset3", GameFunction::SetZoomPreset3, DCSH::set_zoom_preset3);
    parse_config_shortcut(config, parsed, "set_zoom_preset4", GameFunction::SetZoomPreset4, DCSH::set_zoom_preset4);
    parse_config_shortcut(config, parsed, "set_zoom_preset5", GameFunction::SetZoomPreset5, DCSH::set_zoom_preset5);
    parse_config_shortcut(config, parsed, "set_zoom_default", GameFunction::SetZoomDefault, DCSH::set_zoom_default);
    parse_config_shortcut(config, parsed, "toggle_preview_locate", GameFunction::TogglePreviewLocate,
                          DCSH::toggle_preview_locate);
    parse_config_shortcut(config, parsed, "toggle_preview_recall", GameFunction::TogglePreviewRecall,
                          DCSH::toggle_preview_recall);
    parse_config_shortcut(config, parsed, "toggle_cargo_default", GameFunction::ToggleCargoDefault,
                          DCSH::toggle_cargo_default);
    parse_config_shortcut(config, parsed, "toggle_cargo_player", GameFunction::ToggleCargoPlayer,
                          DCSH::toggle_cargo_player);
    parse_config_shortcut(config, parsed, "toggle_cargo_station", GameFunction::ToggleCargoStation,
                          DCSH::toggle_cargo_station);
    parse_config_shortcut(config, parsed, "toggle_cargo_hostile", GameFunction::ToggleCargoHostile,
                          DCSH::toggle_cargo_hostile);
    parse_config_shortcut(config, parsed, "toggle_cargo_armada", GameFunction::ToggleCargoArmada,
                          DCSH::toggle_cargo_armada);
  }

  spdlog::debug("");

  if (!std::filesystem::exists(File::MakePath(File::Config()))) {
    message.str("");
    message << "Creating " << File::Config() << " (default config file)";
    spdlog::warn(message.str());

    Config::Save(parsed, File::Config(), false);
  }

  message.str("");
  message << "Creating " << File::Vars() << " (final config file)";
  spdlog::info(message.str());

  if (std::filesystem::exists(FILE_DEF_PARSED)) {
    message.str("");
    message << "Removing " << FILE_DEF_PARSED << " (old parsed file)";
    spdlog::info(message.str());

    std::filesystem::remove(FILE_DEF_PARSED);
  }

  Config::Save(parsed, File::Vars());

  std::cout << "\n\n-----------------------------\n\n"
            << parsed << "\n\n-----------------------------\nVersion "

#if VERSION_PATCH
            << "Loaded beta version " << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_REVISION << " (Patch "
            << VERSION_PATCH << ")\n\n"
            << "NOTE: Beta versions may have unexpected bugs and issues.\n\n"
#else
            << "Loaded beta version " << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_REVISION
            << " (Release)"
#endif

            << "\n\nPlease see https://github.com/STFC-Mod/mod for latest configuration help, examples and future "
               "releases\n"
            << "or visit the STFC Community Mod discord server at https://discord.gg/PrpHgs7Vjs\n\n";
}

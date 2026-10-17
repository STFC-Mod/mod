// Compile the actual catalog and shared warp/FT models. Only process/config,
// path, text-modal, logging, and save boundaries are controlled here.
#include "config.h"
#include "defaultconfig.h"
#include "file.h"
#include "patches/runtime_config.h"
#include "settings/mod_pages.h"
#include "settings/settings_search.h"
#include "settings/text_entry.h"
#include "settings/upstream_settings.h"
#include "str_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fixture
{
using Json  = nlohmann::json;
using Value = config_edit::Value;
struct Write {
  std::string key;
  Value       value;
  std::chrono::milliseconds delay{};
};
std::vector<Write>                            writes;
std::set<std::string>                         registered;
std::optional<mod_settings::TextEntryRequest> modal;
bool                                          labelsAvailable = true;
int                                           labelRefreshes = 0, hudRefreshes = 0, shipRefreshes = 0;
int                                           queueTrackingResets = 0;
float                                         previousShipScale = 0, nextShipScale = 0;

void Require(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}

template <typename T> Value V(const T& value)
{
  if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>)
    return value;
  else if constexpr (std::is_integral_v<T>)
    return static_cast<std::int64_t>(value);
  else
    return static_cast<double>(value);
}
Json J(const Value& value)
{
  return std::visit([](const auto& part) { return Json(part); }, value);
}

std::string Join(const std::vector<std::string>& names)
{
  std::string result;
  for (const auto& name : names) {
    if (!result.empty())
      result += ", ";
    result += name;
  }
  return result;
}
std::string Trim(std::string_view value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  return first == value.npos ? std::string{}
                             : std::string(value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1));
}
std::string Lower(std::string value)
{
  for (auto& c : value)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return value;
}

// Independent native-field mapping, taken from Config::Load rather than the
// catalog. Changing the wrong Config member must fail the all-model exercise.
std::map<std::string, std::function<Value()>> NativeReads()
{
  auto& cfg = Config::Get();
  return {
      {"audio.alert_armada_battle_lost",
       [&cfg] { return V(std::string(notification_sound_name(cfg.alert_armada_battle_lost))); }},
      {"audio.alert_armada_battle_won",
       [&cfg] { return V(std::string(notification_sound_name(cfg.alert_armada_battle_won))); }},
      {"audio.alert_armada_created",
       [&cfg] { return V(std::string(notification_sound_name(cfg.alert_armada_created))); }},
      {"audio.alert_defeat", [&cfg] { return V(std::string(notification_sound_name(cfg.alert_defeat))); }},
      {"audio.alert_victory", [&cfg] { return V(std::string(notification_sound_name(cfg.alert_victory))); }},
      {"buffs.use_out_of_dock_power", [&cfg] { return V(cfg.use_out_of_dock_power); }},
      {"control.hotkeys_enabled", [&cfg] { return V(cfg.hotkeys_enabled); }},
      {"control.hotkeys_extended", [&cfg] { return V(cfg.hotkeys_extended); }},
      {"control.kirshara_fixes", [&cfg] { return V(cfg.kirshara_fixes); }},
      {"control.primary_requires_visible_target", [&cfg] { return V(cfg.primary_requires_visible_target); }},
      {"control.queue_enabled", [&cfg] { return V(cfg.queue_enabled); }},
      {"control.select_timer", [&cfg] { return V(cfg.select_timer); }},
      {"control.use_scopely_hotkeys", [&cfg] { return V(cfg.use_scopely_hotkeys); }},
#if _WIN32
      {"graphics.allow_cursor", [&cfg] { return V(cfg.allow_cursor); }},
#endif
#if _WIN32
      {"graphics.borderless_fullscreen", [&cfg] { return V(cfg.borderless_fullscreen); }},
#endif
      {"graphics.default_system_zoom", [&cfg] { return V(cfg.default_system_zoom); }},
      {"graphics.fr_scale", [&cfg] { return V(cfg.fr_scale); }},
#if _WIN32
      {"graphics.free_resize", [&cfg] { return V(cfg.free_resize); }},
#endif
      {"graphics.galactic_anomaly_timer", [&cfg] { return V(cfg.galactic_anomaly_timer); }},
      {"graphics.haven_zoom", [&cfg] { return V(cfg.haven_zoom); }},
      {"graphics.keyboard_zoom_speed", [&cfg] { return V(cfg.keyboard_zoom_speed); }},
      {"graphics.loader_enabled", [&cfg] { return V(cfg.loader_enabled); }},
      {"graphics.loader_image", [&cfg] { return V(cfg.loader_image); }},
      {"graphics.loader_logo_scale", [&cfg] { return V(cfg.loader_logo_scale); }},
      {"graphics.loader_tip_enabled", [&cfg] { return V(cfg.loader_tip_enabled); }},
      {"graphics.loader_transition", [&cfg] { return V(cfg.loader_transition); }},
      {"graphics.loader_transition_black", [&cfg] { return V(cfg.loader_transition_black); }},
      {"graphics.system_pan_momentum_falloff", [&cfg] { return V(cfg.system_pan_momentum_falloff); }},
      {"graphics.system_zoom_preset_1", [&cfg] { return V(cfg.system_zoom_preset_1); }},
      {"graphics.system_zoom_preset_2", [&cfg] { return V(cfg.system_zoom_preset_2); }},
      {"graphics.system_zoom_preset_3", [&cfg] { return V(cfg.system_zoom_preset_3); }},
      {"graphics.system_zoom_preset_4", [&cfg] { return V(cfg.system_zoom_preset_4); }},
      {"graphics.system_zoom_preset_5", [&cfg] { return V(cfg.system_zoom_preset_5); }},
      {"graphics.ui_scale", [&cfg] { return V(cfg.ui_scale); }},
      {"graphics.ui_scale_ship", [&cfg] { return V(cfg.ui_scale_ship); }},
      {"graphics.ui_scale_step", [&cfg] { return V(cfg.ui_scale_step); }},
      {"graphics.ui_scale_viewer", [&cfg] { return V(cfg.ui_scale_viewer); }},
      {"graphics.use_presets_as_default", [&cfg] { return V(cfg.use_presets_as_default); }},
      {"graphics.zoom", [&cfg] { return V(cfg.zoom); }},
      {"graphics.zoom_label_non_player_detail",
       [&cfg] {
         return V(std::string(std::array{"native", "expanded", "compact", "threshold"}.at(
             static_cast<int>(cfg.zoom_label_non_player.detail))));
       }},
      {"graphics.zoom_label_non_player_threshold", [&cfg] { return V(cfg.zoom_label_non_player.zoom_threshold); }},
      {"graphics.zoom_label_player_detail",
       [&cfg] {
         return V(std::string(std::array{"native", "expanded", "compact", "threshold"}.at(
             static_cast<int>(cfg.zoom_label_player.detail))));
       }},
      {"graphics.zoom_label_player_threshold", [&cfg] { return V(cfg.zoom_label_player.zoom_threshold); }},
      {"ui.always_skip_reveal_sequence", [&cfg] { return V(cfg.always_skip_reveal_sequence); }},
      {"ui.arrow_keys_to_select_ship", [&cfg] { return V(cfg.arrow_keys_to_select_ship); }},
      {"ui.auto_confirm_ft_upgrade", [&cfg] { return V(cfg.auto_confirm_ft_upgrade); }},
      {"ui.auto_confirm_instant_warp",
       [&cfg] {
         return V(std::string(std::array{"none", "warp", "jump"}.at(static_cast<int>(cfg.auto_confirm_instant_warp))));
       }},
      {"ui.auto_confirm_reveal_ability", [&cfg] { return V(cfg.auto_confirm_reveal_ability); }},
      {"ui.auto_open_bulk_claim_flyout", [&cfg] { return V(cfg.auto_open_bulk_claim_flyout); }},
      {"ui.cargo_significant_decimals", [&cfg] { return V(cfg.cargo_significant_decimals); }},
      {"ui.daily_bulk_claim_factions", [&cfg] { return V(Join(cfg.daily_bulk_claim_factions)); }},
      {"ui.daily_bulk_claim_toggle_default_on", [&cfg] { return V(cfg.daily_bulk_claim_toggle_default_on); }},
      {"ui.disable_escape_exit", [&cfg] { return V(cfg.disable_escape_exit); }},
      {"ui.disable_escape_exit_timer", [&cfg] { return V(cfg.disable_escape_exit_timer); }},
      {"ui.disable_exchange_all", [&cfg] { return V(cfg.disable_exchange_all); }},
      {"ui.disable_galaxy_chat", [&cfg] { return V(cfg.disable_galaxy_chat); }},
      {"ui.disable_move_keys", [&cfg] { return V(cfg.disable_move_keys); }},
      {"ui.disable_preview_locate", [&cfg] { return V(cfg.disable_preview_locate); }},
      {"ui.disable_preview_recall", [&cfg] { return V(cfg.disable_preview_recall); }},
      {"ui.disable_veil_chat", [&cfg] { return V(cfg.disable_veil_chat); }},
      {"ui.disabled_banner_types", [&cfg] { return V(FormatBannerFilter(cfg.disabled_banner_types)); }},
      {"ui.dock_syncmanagement", [&cfg] { return V(cfg.dock_syncmanagement); }},
      {"ui.double_click_to_assign_ship", [&cfg] { return V(cfg.double_click_to_assign_ship); }},
      {"ui.extend_chest_purchase_max", [&cfg] { return V(cfg.extend_chest_purchase_max); }},
      {"ui.extend_donation_max", [&cfg] { return V(cfg.extend_donation_max); }},
      {"ui.extend_donation_slider", [&cfg] { return V(cfg.extend_donation_slider); }},
      {"ui.focus_search", [&cfg] { return V(cfg.focus_search); }},
      {"ui.format_cargo_values", [&cfg] { return V(cfg.format_cargo_values); }},
      {"ui.hud_field_training",
       [&cfg] {
         return V(std::string(std::array{"auto", "always", "never"}.at(
             static_cast<int>(cfg.MissionHudButtonVisibility("field_training")))));
       }},
      {"ui.hud_missions",
       [&cfg] {
         return V(std::string(
             std::array{"auto", "always", "never"}.at(static_cast<int>(cfg.MissionHudButtonVisibility("missions")))));
       }},
      {"ui.hud_outposts",
       [&cfg] {
         return V(std::string(
             std::array{"auto", "always", "never"}.at(static_cast<int>(cfg.MissionHudButtonVisibility("outposts")))));
       }},
      {"ui.hud_q_trials",
       [&cfg] {
         return V(std::string(
             std::array{"auto", "always", "never"}.at(static_cast<int>(cfg.MissionHudButtonVisibility("q_trials")))));
       }},
#if defined(_WIN32) && defined(_M_X64)
      {"ui.instant_cargo_counter", [&cfg] { return V(cfg.instant_cargo_counter); }},
#endif
      {"ui.instant_warp_always_ask",
       [&cfg] { return V(cfg.instant_warp_always_ask_all ? std::string("*") : Join(cfg.instant_warp_always_ask)); }},
      {"ui.instant_warp_auto_jump",
       [&cfg] { return V(cfg.instant_warp_auto_jump_all ? std::string("*") : Join(cfg.instant_warp_auto_jump)); }},
      {"ui.instant_warp_auto_warp",
       [&cfg] { return V(cfg.instant_warp_auto_warp_all ? std::string("*") : Join(cfg.instant_warp_auto_warp)); }},
#if _WIN32
      {"ui.notify_banner_types", [&cfg] { return V(FormatBannerFilter(cfg.notify_banner_types)); }},
#endif
      {"ui.officer_sort", [&cfg] { return V(cfg.officer_sort); }},
      {"ui.only_show_first_popup", [&cfg] { return V(cfg.only_show_first_popup); }},
      {"ui.pinned_ships", [&cfg] { return V(Join(cfg.pinned_ships)); }},
      {"ui.reverse_haven_history", [&cfg] { return V(cfg.reverse_haven_history); }},
      {"ui.show_armada_cargo", [&cfg] { return V(cfg.show_armada_cargo); }},
      {"ui.show_cargo_default", [&cfg] { return V(cfg.show_cargo_default); }},
      {"ui.show_hostile_cargo", [&cfg] { return V(cfg.show_hostile_cargo); }},
      {"ui.show_player_cargo", [&cfg] { return V(cfg.show_player_cargo); }},
      {"ui.show_player_title", [&cfg] { return V(cfg.show_player_title); }},
      {"ui.show_station_cargo", [&cfg] { return V(cfg.show_station_cargo); }},
  };
}

std::map<std::string, Value> ReadAll(const std::map<std::string, std::function<Value()>>& reads)
{
  std::map<std::string, Value> result;
  for (const auto& [key, read] : reads)
    result.emplace(key, read());
  return result;
}
struct Model {
  const mod_settings::SettingMetadata*                           metadata = nullptr;
  std::function<void()>                                          reset;
  std::function<bool()>                                          available;
  std::function<void(const Json&, const std::filesystem::path&)> change;
  mod_settings::ActionSetting*                                   exact = nullptr;
};

template <typename T> Model Typed(mod_settings::ValueSetting<T>& state, int choices = 0)
{
  Require(state.default_value().has_value(), "typed definition missing default: " + state.id());
  return {&state.metadata(), [&state] { state.Reset(state.Observe()); },
          [&state] { return state.Observe().state.known(); },
          [&state, choices](const Json&, const std::filesystem::path&) {
            const auto before = state.Observe();
            Require(before.state.known(), "unavailable model: " + state.id());
            T desired;
            if constexpr (std::is_same_v<T, bool>)
              desired = !*before.state.value;
            else if constexpr (std::is_same_v<T, int>)
              desired = (*before.state.value + 1) % choices;
            else
              desired = *before.state.value + static_cast<T>(.01f);
            const auto result = state.SetFromUser(desired, before).outcome;
            Require(result == mod_settings::Outcome::AppliedVerified, "typed edit rejected: " + state.id());
          }};
}

Model Exact(mod_settings::ActionSetting& action)
{
  Require(action.default_label && action.reset && action.retry && action.prepare_reset && action.save_status,
          "exact definition missing model operations: " + action.id());
  return {action.metadata, action.reset, [&action] { return action.Read(0).enabled; },
          [&action](const Json& row, const std::filesystem::path& image) {
            modal.reset();
            action.invoke(0);
            Require(modal.has_value(), "exact editor did not open: " + action.id());
            Require(!modal->help.empty(), "exact editor missing help: " + action.id());
            const std::string key     = row.at("key");
            const std::string control = row.at("control");
            std::string       value;
            if (control == "number") {
              std::ostringstream text;
              const auto         number  = std::stod(modal->value);
              const bool         integer = std::holds_alternative<std::int64_t>(NativeReads().at(key)());
              text.precision(12);
              text << number + (integer ? 1. : .01);
              value = text.str();
            } else if (control == "path")
              value = image.string();
            else if (key == "ui.daily_bulk_claim_factions")
              value = "federation";
            else if (key == "ui.disabled_banner_types" || key == "ui.notify_banner_types")
              value = "Victory";
            else
              value = "enterprise";
            Require(modal->validate(value).empty(), "valid input rejected: " + key + " = " + value);
            Require(modal->commit(value), "exact commit rejected: " + key);
            modal.reset();
          },
          &action};
}
} // namespace fixture

// Config's sole fixture instance has static storage, so all scalar members are
// zero-initialized before this constructor. Used fields then receive the same
// named DefaultConfig constants as the startup loader. No Load/file API runs.
Config::Config()
{
  installUiScaleHooks     = static_cast<decltype(installUiScaleHooks)>(DefaultConfig::Patches::uiscalehooks);
  installZoomHooks        = static_cast<decltype(installZoomHooks)>(DefaultConfig::Patches::zoomhooks);
  installHavenZoomHooks   = static_cast<decltype(installHavenZoomHooks)>(DefaultConfig::Patches::havenzoomhooks);
  installBuffFixHooks     = static_cast<decltype(installBuffFixHooks)>(DefaultConfig::Patches::bufffixhooks);
  installToastBannerHooks = static_cast<decltype(installToastBannerHooks)>(DefaultConfig::Patches::toastbannerhooks);
  installPanHooks         = static_cast<decltype(installPanHooks)>(DefaultConfig::Patches::panhooks);
  installHotkeyHooks      = static_cast<decltype(installHotkeyHooks)>(DefaultConfig::Patches::hotkeyhooks);
  installNativeSettings   = static_cast<decltype(installNativeSettings)>(DefaultConfig::Patches::nativesettingshooks);
  installRuntimeConfigHooks =
      static_cast<decltype(installRuntimeConfigHooks)>(DefaultConfig::Patches::runtimeconfighooks);
  installFreeResizeHooks = static_cast<decltype(installFreeResizeHooks)>(DefaultConfig::Patches::freeresizehooks);
  installTempCrashFixes  = static_cast<decltype(installTempCrashFixes)>(DefaultConfig::Patches::tempcrashfixes);
  installTestPatches     = static_cast<decltype(installTestPatches)>(DefaultConfig::Patches::testpatches);
  installMiscPatches     = static_cast<decltype(installMiscPatches)>(DefaultConfig::Patches::miscpatches);
  installMissionHudTweaksHooks =
      static_cast<decltype(installMissionHudTweaksHooks)>(DefaultConfig::Patches::missionhudtweakshooks);
  installChatPatches = static_cast<decltype(installChatPatches)>(DefaultConfig::Patches::chatpatches);
  installPlayerTitleDisplayHooks =
      static_cast<decltype(installPlayerTitleDisplayHooks)>(DefaultConfig::Patches::chattitledisplayhooks);
  installSyncPatches     = static_cast<decltype(installSyncPatches)>(DefaultConfig::Patches::syncpatches);
  installGameVersionHook = static_cast<decltype(installGameVersionHook)>(DefaultConfig::Patches::game_version);
  installObjectTracker   = static_cast<decltype(installObjectTracker)>(DefaultConfig::Patches::objecttracker);
  installLoadingScreenHooks =
      static_cast<decltype(installLoadingScreenHooks)>(DefaultConfig::Patches::loadingscreenhooks);
  installTransitionScreenHooks =
      static_cast<decltype(installTransitionScreenHooks)>(DefaultConfig::Patches::transitionscreenhooks);
  installGiftsBulkClaimHooks =
      static_cast<decltype(installGiftsBulkClaimHooks)>(DefaultConfig::Patches::giftsbulkclaimhooks);
  installDailyFactionBulkClaimHooks =
      static_cast<decltype(installDailyFactionBulkClaimHooks)>(DefaultConfig::Patches::dailyfactionbulkclaimhooks);
  installGalacticAnomalyTimerHooks =
      static_cast<decltype(installGalacticAnomalyTimerHooks)>(DefaultConfig::Patches::galacticanomalytimerhooks);
  installLoadingTipHooks = static_cast<decltype(installLoadingTipHooks)>(DefaultConfig::Patches::loadingtiphooks);
  installDoubleClickAssignShipHooks =
      static_cast<decltype(installDoubleClickAssignShipHooks)>(DefaultConfig::Patches::doubleclickassignshiphooks);
  installForbiddenTechConfirmationHooks =
      static_cast<decltype(installForbiddenTechConfirmationHooks)>(DefaultConfig::Patches::forbiddentechconfirmhooks);
  installRevealConfirmationHooks =
      static_cast<decltype(installRevealConfirmationHooks)>(DefaultConfig::Patches::revealconfirmhooks);
  installQueueAddressGuardHooks =
      static_cast<decltype(installQueueAddressGuardHooks)>(DefaultConfig::Patches::queueaddressguardhooks);
  installActionQueueRecoveryHooks =
      static_cast<decltype(installActionQueueRecoveryHooks)>(DefaultConfig::Patches::actionqueuerecoveryhooks);
  installThinQueueProtectionHooks =
      static_cast<decltype(installThinQueueProtectionHooks)>(DefaultConfig::Patches::thinqueueprotectionhooks);
  installArtifactExchangeHooks =
      static_cast<decltype(installArtifactExchangeHooks)>(DefaultConfig::Patches::artifactexchangehooks);
  installAudioEventHooks = static_cast<decltype(installAudioEventHooks)>(DefaultConfig::Patches::audioeventhooks);
  installInstantCargoCounterHooks =
      static_cast<decltype(installInstantCargoCounterHooks)>(DefaultConfig::Patches::instantcargocounterhooks);
  installCargoFormatHooks = static_cast<decltype(installCargoFormatHooks)>(DefaultConfig::Patches::cargoformathooks);
  installOfficerSortHooks = static_cast<decltype(installOfficerSortHooks)>(DefaultConfig::Patches::officersorthooks);
  installPinnedShipSortHooks =
      static_cast<decltype(installPinnedShipSortHooks)>(DefaultConfig::Patches::pinnedshiphooks);
  installPinnedShipClickToggle =
      static_cast<decltype(installPinnedShipClickToggle)>(DefaultConfig::Patches::pinnedshipclicktoggle);
  installHavenHistoryHooks = static_cast<decltype(installHavenHistoryHooks)>(DefaultConfig::Patches::havenhistoryhooks);
  kirshara_fixes           = static_cast<decltype(kirshara_fixes)>(DefaultConfig::Control::kirshara_fixes);
  queue_enabled            = static_cast<decltype(queue_enabled)>(DefaultConfig::Control::queue_enabled);
  hotkeys_enabled          = static_cast<decltype(hotkeys_enabled)>(DefaultConfig::Control::hotkeys_enabled);
  hotkeys_extended         = static_cast<decltype(hotkeys_extended)>(DefaultConfig::Control::hotkeys_extended);
  primary_requires_visible_target =
      static_cast<decltype(primary_requires_visible_target)>(DefaultConfig::Control::primary_requires_visible_target);
  use_scopely_hotkeys = static_cast<decltype(use_scopely_hotkeys)>(DefaultConfig::Control::use_scopely_hotkeys);
  select_timer        = static_cast<decltype(select_timer)>(DefaultConfig::Control::select_timer);
  enable_experimental = static_cast<decltype(enable_experimental)>(DefaultConfig::Control::enable_experimental);
  ui_scale            = static_cast<decltype(ui_scale)>(DefaultConfig::Graphics::ui_scale);
  ui_scale_step       = static_cast<decltype(ui_scale_step)>(DefaultConfig::Graphics::ui_scale_step);
  ui_scale_ship       = static_cast<decltype(ui_scale_ship)>(DefaultConfig::Graphics::ui_scale_ship);
  ui_scale_viewer     = static_cast<decltype(ui_scale_viewer)>(DefaultConfig::Graphics::ui_scale_viewer);
  zoom                = static_cast<decltype(zoom)>(DefaultConfig::Graphics::zoom);
  haven_zoom          = static_cast<decltype(haven_zoom)>(DefaultConfig::Graphics::haven_zoom);
  fr_scale            = static_cast<decltype(fr_scale)>(DefaultConfig::Graphics::fr_scale);
  free_resize         = static_cast<decltype(free_resize)>(DefaultConfig::Graphics::free_resize);
  allow_cursor        = static_cast<decltype(allow_cursor)>(DefaultConfig::Graphics::allow_cursor);
  keyboard_zoom_speed = static_cast<decltype(keyboard_zoom_speed)>(DefaultConfig::Graphics::keyboard_zoom_speed);
  system_pan_momentum = static_cast<decltype(system_pan_momentum)>(DefaultConfig::Graphics::system_pan_momentum);
  system_pan_momentum_falloff =
      static_cast<decltype(system_pan_momentum_falloff)>(DefaultConfig::Graphics::system_pan_momentum_falloff);
  borderless_fullscreen = static_cast<decltype(borderless_fullscreen)>(DefaultConfig::Graphics::borderless_fullscreen);
  transition_time       = static_cast<decltype(transition_time)>(DefaultConfig::Graphics::transition_time);
  default_system_zoom   = static_cast<decltype(default_system_zoom)>(DefaultConfig::Graphics::default_system_zoom);
  system_zoom_preset_1  = static_cast<decltype(system_zoom_preset_1)>(DefaultConfig::Graphics::system_zoom_preset_1);
  system_zoom_preset_2  = static_cast<decltype(system_zoom_preset_2)>(DefaultConfig::Graphics::system_zoom_preset_2);
  system_zoom_preset_3  = static_cast<decltype(system_zoom_preset_3)>(DefaultConfig::Graphics::system_zoom_preset_3);
  system_zoom_preset_4  = static_cast<decltype(system_zoom_preset_4)>(DefaultConfig::Graphics::system_zoom_preset_4);
  system_zoom_preset_5  = static_cast<decltype(system_zoom_preset_5)>(DefaultConfig::Graphics::system_zoom_preset_5);
  use_presets_as_default =
      static_cast<decltype(use_presets_as_default)>(DefaultConfig::Graphics::use_presets_as_default);
  use_out_of_dock_power = static_cast<decltype(use_out_of_dock_power)>(DefaultConfig::Buffs::use_out_of_dock_power);
  disable_escape_exit   = static_cast<decltype(disable_escape_exit)>(DefaultConfig::UI::disable_escape_exit);
  disable_escape_exit_timer =
      static_cast<decltype(disable_escape_exit_timer)>(DefaultConfig::UI::disable_escape_exit_timer);
  disable_preview_locate = static_cast<decltype(disable_preview_locate)>(DefaultConfig::UI::disable_preview_locate);
  disable_preview_recall = static_cast<decltype(disable_preview_recall)>(DefaultConfig::UI::disable_preview_recall);
  only_show_first_popup  = static_cast<decltype(only_show_first_popup)>(DefaultConfig::UI::only_show_first_popup);
  disable_move_keys      = static_cast<decltype(disable_move_keys)>(DefaultConfig::UI::disable_move_keys);
  disable_toast_banners  = static_cast<decltype(disable_toast_banners)>(DefaultConfig::UI::disable_toast_banners);
  trace_audio_events     = static_cast<decltype(trace_audio_events)>(DefaultConfig::Audio::trace_events);
  auto_open_bulk_claim_flyout =
      static_cast<decltype(auto_open_bulk_claim_flyout)>(DefaultConfig::UI::auto_open_bulk_claim_flyout);
  daily_bulk_claim_toggle_default_on =
      static_cast<decltype(daily_bulk_claim_toggle_default_on)>(DefaultConfig::UI::daily_bulk_claim_toggle_default_on);
  double_click_to_assign_ship =
      static_cast<decltype(double_click_to_assign_ship)>(DefaultConfig::UI::double_click_to_assign_ship);
  focus_search          = static_cast<decltype(focus_search)>(DefaultConfig::UI::focus_search);
  format_cargo_values   = static_cast<decltype(format_cargo_values)>(DefaultConfig::UI::format_cargo_values);
  officer_sort          = static_cast<decltype(officer_sort)>(DefaultConfig::UI::officer_sort);
  reverse_haven_history = static_cast<decltype(reverse_haven_history)>(DefaultConfig::UI::reverse_haven_history);
  dock_syncmanagement   = static_cast<decltype(dock_syncmanagement)>(DefaultConfig::UI::dock_syncmanagement);
  arrow_keys_to_select_ship =
      static_cast<decltype(arrow_keys_to_select_ship)>(DefaultConfig::UI::arrow_keys_to_select_ship);
  auto_confirm_ft_upgrade = static_cast<decltype(auto_confirm_ft_upgrade)>(DefaultConfig::UI::auto_confirm_ft_upgrade);
  auto_confirm_reveal_ability =
      static_cast<decltype(auto_confirm_reveal_ability)>(DefaultConfig::UI::auto_confirm_reveal_ability);
  extend_chest_purchase_max =
      static_cast<decltype(extend_chest_purchase_max)>(DefaultConfig::UI::extend_chest_purchase_max);
  extend_donation_slider = static_cast<decltype(extend_donation_slider)>(DefaultConfig::UI::extend_donation_slider);
  extend_donation_max    = static_cast<decltype(extend_donation_max)>(DefaultConfig::UI::extend_donation_max);
  disable_galaxy_chat    = static_cast<decltype(disable_galaxy_chat)>(DefaultConfig::UI::disable_galaxy_chat);
  disable_veil_chat      = static_cast<decltype(disable_veil_chat)>(DefaultConfig::UI::disable_veil_chat);
  show_player_title      = static_cast<decltype(show_player_title)>(DefaultConfig::UI::show_player_title);
  show_cargo_default     = static_cast<decltype(show_cargo_default)>(DefaultConfig::UI::show_cargo_default);
  show_player_cargo      = static_cast<decltype(show_player_cargo)>(DefaultConfig::UI::show_player_cargo);
  show_station_cargo     = static_cast<decltype(show_station_cargo)>(DefaultConfig::UI::show_station_cargo);
  show_hostile_cargo     = static_cast<decltype(show_hostile_cargo)>(DefaultConfig::UI::show_hostile_cargo);
  show_armada_cargo      = static_cast<decltype(show_armada_cargo)>(DefaultConfig::UI::show_armada_cargo);
  instant_cargo_counter  = static_cast<decltype(instant_cargo_counter)>(DefaultConfig::UI::instant_cargo_counter);
  cargo_significant_decimals =
      static_cast<decltype(cargo_significant_decimals)>(DefaultConfig::UI::cargo_significant_decimals);
  always_skip_reveal_sequence =
      static_cast<decltype(always_skip_reveal_sequence)>(DefaultConfig::UI::always_skip_reveal_sequence);
  disable_exchange_all    = static_cast<decltype(disable_exchange_all)>(DefaultConfig::UI::disable_exchange_all);
  sync_debug              = static_cast<decltype(sync_debug)>(DefaultConfig::Sync::debug);
  sync_logging            = static_cast<decltype(sync_logging)>(DefaultConfig::Sync::logging);
  sync_resolver_cache_ttl = static_cast<decltype(sync_resolver_cache_ttl)>(DefaultConfig::Sync::resolver_cache_ttl);
  config_settings_url     = static_cast<decltype(config_settings_url)>(DefaultConfig::SystemConfig::settings_url);
  config_assets_url_override =
      static_cast<decltype(config_assets_url_override)>(DefaultConfig::SystemConfig::assets_url_override);
  loader_enabled    = static_cast<decltype(loader_enabled)>(DefaultConfig::Graphics::loader_enabled);
  loader_transition = static_cast<decltype(loader_transition)>(DefaultConfig::Graphics::loader_transition);
  loader_transition_black =
      static_cast<decltype(loader_transition_black)>(DefaultConfig::Graphics::loader_transition_black);
  loader_image      = static_cast<decltype(loader_image)>(DefaultConfig::Graphics::loader_image);
  loader_logo_scale = static_cast<decltype(loader_logo_scale)>(DefaultConfig::Graphics::loader_logo_scale);
  galactic_anomaly_timer =
      static_cast<decltype(galactic_anomaly_timer)>(DefaultConfig::Graphics::galactic_anomaly_timer);
  loader_tip_enabled = static_cast<decltype(loader_tip_enabled)>(DefaultConfig::Graphics::loader_tip_enabled);
  installInstantWarpConfirmationHooks = true;
  auto_confirm_instant_warp           = InstantWarpConfirmation::None;
  zoom_label_player                   = {FleetLabelDetail::Native,
                                         static_cast<float>(DefaultConfig::Graphics::zoom_label_player_threshold)};
  zoom_label_non_player               = {FleetLabelDetail::Native,
                                         static_cast<float>(DefaultConfig::Graphics::zoom_label_non_player_threshold)};
  bool ignoredAll                     = false;
  ParseShipFilter(DefaultConfig::UI::pinned_ships, pinned_ships, ignoredAll);
  ParseShipFilter(DefaultConfig::UI::instant_warp_auto_jump, instant_warp_auto_jump, instant_warp_auto_jump_all);
  ParseShipFilter(DefaultConfig::UI::instant_warp_auto_warp, instant_warp_auto_warp, instant_warp_auto_warp_all);
  ParseShipFilter(DefaultConfig::UI::instant_warp_always_ask, instant_warp_always_ask, instant_warp_always_ask_all);
  ParseFactionFilter(DefaultConfig::UI::daily_bulk_claim_factions, daily_bulk_claim_factions);
  ParseBannerFilter(DefaultConfig::UI::disabled_banner_types, disabled_banner_types);
  ParseBannerFilter(DefaultConfig::UI::notify_banner_types, notify_banner_types);
  alert_victory = notification_sound_from_name(DefaultConfig::Audio::alert_victory).value_or(NotificationSound::None);
  alert_defeat  = notification_sound_from_name(DefaultConfig::Audio::alert_defeat).value_or(NotificationSound::None);
  alert_armada_created =
      notification_sound_from_name(DefaultConfig::Audio::alert_armada_created).value_or(NotificationSound::None);
  alert_armada_battle_won =
      notification_sound_from_name(DefaultConfig::Audio::alert_armada_battle_won).value_or(NotificationSound::None);
  alert_armada_battle_lost =
      notification_sound_from_name(DefaultConfig::Audio::alert_armada_battle_lost).value_or(NotificationSound::None);
}
Config& Config::Get()
{
  static Config config;
  return config;
}
MissionHudVisibility Config::MissionHudButtonVisibility(std::string_view name) const
{
  const auto found = mission_hud_buttons.find(std::string(name));
  return found == mission_hud_buttons.end() ? MissionHudVisibility::Auto : found->second;
}

const char* File::Config()
{ return "fixture-settings.toml"; }
#if _WIN32
std::string_view File::MakePath(std::string_view, bool, bool)
{ return "fixture://configuration/fixture-settings.toml"; }
#else
std::u8string File::MakePath(std::string_view, bool, bool)
{ return u8"fixture://configuration/fixture-settings.toml"; }
#endif

namespace runtime_config
{
bool RegisterSetting(const char* section, const char* key) noexcept
{
  fixture::registered.insert(std::string(section) + "." + key);
  return true;
}
void SaveSetting(const char* section, const char* key, config_edit::Value value,
                 std::chrono::milliseconds delay) noexcept
{ fixture::writes.push_back({std::string(section) + "." + key, std::move(value), delay}); }
mod_settings::SaveStatus GetSaveStatus(const char* section, const char* key, const config_edit::Value& current) noexcept
{
  const auto name = std::string(section) + "." + key;
  for (auto it = fixture::writes.rbegin(); it != fixture::writes.rend(); ++it)
    if (it->key == name)
      return {it->value == current ? mod_settings::SaveState::Saved : mod_settings::SaveState::SessionOnly,
              fixture::writes.size()};
  return {};
}
} // namespace runtime_config
namespace mod_settings
{
PageCatalog& ModPages()
{
  static PageCatalog catalog("community_mod.settings", "Mod Settings");
  return catalog;
}
bool OpenTextEntry(TextEntryRequest request)
{
  fixture::modal = std::move(request);
  return true;
}
void CloseTextEntry()
{ fixture::modal.reset(); }
bool TextEntryActive()
{ return fixture::modal.has_value(); }
} // namespace mod_settings
bool ForbiddenTechControlsAvailable()
{ return true; }
namespace mission_hud
{
bool Available()
{ return true; }
bool CanChange()
{ return true; }
void Refresh()
{ ++fixture::hudRefreshes; }
} // namespace mission_hud
bool FleetLabelControlsAvailable()
{ return fixture::labelsAvailable; }
void RefreshFleetLabelControls()
{ ++fixture::labelRefreshes; }
void ApplyUiShipScaleToLoadedShips(float before, float after)
{
  ++fixture::shipRefreshes;
  fixture::previousShipScale = before;
  fixture::nextShipScale     = after;
}
void ResetQueueRecoveryTracking()
{ ++fixture::queueTrackingResets; }

std::string_view notification_sound_name(NotificationSound sound)
{
  static constexpr std::string_view names[]{"none",  "default", "info", "success", "warning",
                                            "alarm", "arrival", "soft", "ping",    "repair"};
  return names[static_cast<int>(sound)];
}
std::optional<NotificationSound> notification_sound_from_name(std::string_view name)
{
  for (int i = 0; i < static_cast<int>(NotificationSound::Count); ++i)
    if (notification_sound_name(static_cast<NotificationSound>(i)) == name)
      return static_cast<NotificationSound>(i);
  return {};
}
void notification_audio_play(NotificationSound) {}

int main(int argc, char** argv)
{
  using namespace fixture;
  using namespace mod_settings;
  try {
    Require(argc == 3, "usage: upstream_settings_test <coverage.json> <new-fixture-directory>");
    std::ifstream coverageFile(argv[1]);
    Require(coverageFile.good(), "coverage inventory not readable");
    const Json                  inventory = Json::parse(coverageFile);
    std::map<std::string, Json> expected;
    for (const auto& row : inventory.at("settings")) {
      if (row.at("treatment") != "in_game")
        continue;
#if !_WIN32
      if (row.contains("platforms") && row.at("platforms") == Json::array({"windows"}))
        continue;
#endif
      Require(expected.emplace(row.at("key").get<std::string>(), row).second, "duplicate inventory key");
    }
#if _WIN32
    Require(expected.size() == 87, "Windows catalog count changed without a coverage decision");
#else
    Require(expected.size() == 82, "macOS catalog count changed without a coverage decision");
#endif
    const auto reads = NativeReads();
    Require(reads.size() == expected.size(), "native field map missing an inventory key");
    const auto defaults                 = ReadAll(reads);
    Config::Get().installNativeSettings = false;
    Require(PersistCurrentSetting("ui", "pinned_ships"), "native pin save failed with settings hooks disabled");
    Require(writes.size() == 1 && writes.back().key == "ui.pinned_ships", "lazy initialization wrote unrelated fields");
    writes.clear();
    auto& catalog = ModPages();
    RegisterUpstreamSettings(catalog);
    const auto     pages = catalog.Build();
    SettingsSearch search;
    search.Build(pages);
    Require(!search.Find("ui.show_player_title").empty(), "actual catalog key missing from search");
    std::map<std::string, Model> models;
    for (const auto& page : pages)
      for (const auto& item : page.items) {
        std::visit(
            [&](const auto& value) {
              using P = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<P, BooleanSetting*>) {
                value->Observe();
                models.try_emplace(value->metadata().TomlKey(), Typed(*value));
              } else if constexpr (std::is_same_v<P, ChoiceSetting*>) {
                value->state().Observe();
                models.try_emplace(value->state().metadata().TomlKey(),
                                   Typed(value->state(), static_cast<int>(value->labels().size())));
              } else if constexpr (std::is_same_v<P, SliderSetting*>) {
                value->state().Observe();
                models.try_emplace(value->state().metadata().TomlKey(), Typed(value->state()));
              } else if constexpr (std::is_same_v<P, ActionSetting*>) {
                for (std::size_t i = 0; i < value->Count(); ++i)
                  value->Read(i);
                if (value->metadata)
                  models.try_emplace(value->metadata->TomlKey(), Exact(*value));
              }
            },
            item);
      }
    Require(writes.empty(), "registration/search/browsing persisted configuration");
    Require(models.size() == expected.size() && registered.size() == expected.size(),
            "actual metadata count differs from inventory");
    for (const auto& [key, model] : models) {
      Require(expected.contains(key), "unexpected model key: " + key);
      Require(!model.metadata->description.empty(), "missing model help: " + key);
      Require(model.available(), "startup default unavailable: " + key);
      const char* timing = model.metadata->timing == ApplyTiming::Restart      ? "restart"
                           : model.metadata->timing == ApplyTiming::ReopenView ? "reopen_view"
                                                                               : "live";
      Require(expected.at(key).at("application") == timing, "application timing differs from inventory: " + key);
    }
    const std::filesystem::path directory(argv[2]);
    Require(!std::filesystem::exists(directory), "fixture directory must be new");
    std::filesystem::create_directories(directory);
    const auto image = directory / "test-image.png";
    {
      std::ofstream file(image);
      file << "catalog path boundary fixture";
    }
    for (const auto& [key, model] : models) {
      writes.clear();
      model.change(expected.at(key), image);
      Require(writes.size() == 1 && writes.back().key == key, "edit did not queue exactly its own field: " + key);
      const auto after = ReadAll(reads);
      for (const auto& [other, before] : defaults) {
        const auto target =
            other == key && model.metadata->timing != ApplyTiming::Restart ? writes.back().value : before;
        Require(after.at(other) == target, "edit changed wrong native field: " + key + " -> " + other);
      }
      model.reset();
      Require(writes.size() == 2 && writes.back().key == key, "reset did not queue exactly its own field: " + key);
      Require(writes.back().value == defaults.at(key), "reset/default serialization mismatch: " + key);
      if (key == "graphics.ui_scale" || key == "graphics.zoom_label_player_threshold")
        Require(writes.back().delay == std::chrono::milliseconds(250), "float edits lost save debounce: " + key);
      Require(ReadAll(reads) == defaults, "reset did not restore native baseline: " + key);
    }
    Require(shipRefreshes == 2 && previousShipScale != nextShipScale, "ship scale did not refresh loaded ships");
    Require(queueTrackingResets == 4, "queue protections did not invalidate tracking on both toggle transitions");
    models.at("control.queue_enabled").reset();
    models.at("control.kirshara_fixes").reset();
    Require(queueTrackingResets == 4, "unchanged queue defaults unnecessarily invalidated recovery tracking");
    Require(labelRefreshes > 0 && hudRefreshes > 0, "native refresh/parser boundaries were bypassed");

    // Exercise actual dependency combinations rather than assuming a catalog
    // control's owner from its name or its Config field.
    auto withFlags = [&](std::initializer_list<std::pair<bool Config::*, bool>> flags, auto check) {
      std::vector<std::pair<bool Config::*, bool>> originals;
      for (const auto& [member, value] : flags) {
        originals.emplace_back(member, Config::Get().*member);
        Config::Get().*member = value;
      }
      check();
      for (const auto& [member, value] : originals)
        Config::Get().*member = value;
    };
    withFlags({{&Config::installMiscPatches, false}, {&Config::installTempCrashFixes, true}}, [&] {
      Require(models.at("ui.only_show_first_popup").available()
                  && models.at("ui.always_skip_reveal_sequence").available(),
              "temporary hooks wrongly depend on Misc");
    });
    withFlags({{&Config::installMiscPatches, true}, {&Config::installTempCrashFixes, false}}, [&] {
      Require(!models.at("ui.only_show_first_popup").available()
                  && !models.at("ui.always_skip_reveal_sequence").available(),
              "disabled temporary hooks advertised");
    });
    withFlags({{&Config::installPinnedShipSortHooks, false}}, [&] {
      Require(!models.at("ui.pinned_ships").available(), "disabled sort editor advertised");
      writes.clear();
      Require(PersistCurrentSetting("ui", "pinned_ships") && writes.size() == 1
                  && writes.back().key == "ui.pinned_ships",
              "native pin save incorrectly depends on sort/UI");
    });
    withFlags(
        {{&Config::installHotkeyHooks, false}, {&Config::installZoomHooks, true}, {&Config::installPanHooks, true}},
        [&] {
          Require(models.at("control.hotkeys_enabled").available() && models.at("control.hotkeys_extended").available()
                      && models.at("control.use_scopely_hotkeys").available()
                      && models.at("ui.disable_move_keys").available(),
                  "independent camera owners ignored");
        });
    withFlags({{&Config::installPanHooks, false}},
              [&] { Require(!models.at("ui.disable_move_keys").available(), "disabled pan owner advertised"); });
    withFlags({{&Config::installLoadingScreenHooks, false}, {&Config::installTransitionScreenHooks, true}}, [&] {
      Require(models.at("graphics.loader_image").available() && models.at("graphics.loader_logo_scale").available(),
              "shared transition assets incorrectly depend on loading hooks");
    });
    withFlags({{&Config::installHotkeyHooks, false},
               {&Config::installQueueAddressGuardHooks, false},
               {&Config::installActionQueueRecoveryHooks, false},
               {&Config::installThinQueueProtectionHooks, false},
               {&Config::installTestPatches, true}},
              [&] {
                Require(models.at("control.queue_enabled").available()
                            && !models.at("control.kirshara_fixes").available(),
                        "queue/protection owners conflated");
              });

    // The same production parsers serve startup and the exact editor.
    const std::string bounded = "AbC suffix";
    Require(AsciiStrToUpper(std::string_view(bounded).substr(0, 3)) == "ABC"
                && AsciiStrToLower(std::string_view(bounded).substr(0, 3)) == "abc",
            "case helpers ignored view bounds");
    auto edit = [&](const std::string& key, const std::string& text) {
      models.at(key).exact->invoke(0);
      Require(modal && modal->validate(text).empty() && modal->commit(text), "production parser rejected " + text);
      modal.reset();
    };
    edit("ui.daily_bulk_claim_factions", " federation ,klingon ,romulan ");
    Require(Config::Get().daily_bulk_claim_factions == std::vector<std::string>({"federation", "klingon", "romulan"}),
            "faction whitespace normalization failed");
    models.at("ui.daily_bulk_claim_factions").reset();
    edit("ui.disabled_banner_types", "VICTORY ,DEFEAT ");
    Require(FormatBannerFilter(Config::Get().disabled_banner_types) == "Victory, Defeat", "banner parsing failed");
    models.at("ui.disabled_banner_types").reset();
    edit("ui.instant_warp_auto_jump", " * ");
    Require(Config::Get().instant_warp_auto_jump_all, "trimmed wildcard was not recognized");
    models.at("ui.instant_warp_auto_jump").reset();
    edit("ui.extend_chest_purchase_max", "0");
    Require(Config::Get().extend_chest_purchase_max == DefaultConfig::UI::extend_chest_purchase_max,
            "restart chest setting changed active hook behavior");
    Require(std::get<std::int64_t>(writes.back().value) == 0, "documented disabled value was not saved");
    models.at("ui.extend_chest_purchase_max").reset();
    edit("graphics.system_zoom_preset_1", "1000");
    Require(models.at("graphics.system_zoom_preset_1").exact->status() == "Applied and saved", "preset save missing");
    Config::Get().system_zoom_preset_1 = 2000; // Same mutation made by the explicit StoreZoom shortcut.
    Require(models.at("graphics.system_zoom_preset_1").exact->status() == "Applied for this session; not saved",
            "old saved badge described a different preset");
    Require(PersistCurrentSetting("graphics", "system_zoom_preset_1") && std::get<double>(writes.back().value) == 2000,
            "explicit preset save bypassed its canonical serializer");
    Require(models.at("graphics.system_zoom_preset_1").exact->status() == "Applied and saved", "preset status stale");
    models.at("graphics.system_zoom_preset_1").reset();
    edit("graphics.default_system_zoom", "1000");
    const auto beforeAutomatic        = writes.size();
    Config::Get().default_system_zoom = 2000; // Automatic use-presets-as-default remains session-only.
    Require(models.at("graphics.default_system_zoom").exact->status() == "Applied for this session; not saved"
                && writes.size() == beforeAutomatic,
            "automatic session default was saved or mislabeled");
    models.at("graphics.default_system_zoom").reset();
    Require(ReadAll(reads) == defaults, "parser/restart regression did not restore baseline");

    auto invalid = [&](const std::string& key, const std::string& text) {
      auto* action = models.at(key).exact;
      Require(action, "validation test requires exact entry");
      writes.clear();
      modal.reset();
      action->invoke(0);
      Require(modal.has_value(), "validation modal missing");
      Require(!modal->validate(text).empty() && !modal->commit(text), "invalid input accepted: " + key + " = " + text);
      Require(writes.empty() && ReadAll(reads) == defaults, "invalid input mutated state");
      modal.reset();
    };
    for (const auto* text : {"", "nan", "inf", "5garbage", "-1", "+5", "0x1p2", "1e1000", "1e-1000", "4 75"})
      invalid("graphics.keyboard_zoom_speed", text);
    invalid("graphics.system_pan_momentum_falloff", "1e-1000");
    invalid("control.select_timer", "1.5");
    invalid("control.select_timer", "2147483648");
    invalid("ui.cargo_significant_decimals", "7");
    invalid("ui.pinned_ships", "*");
    invalid("ui.daily_bulk_claim_factions", "not-a-faction");
    invalid("ui.disabled_banner_types", "not-a-banner");
    invalid("graphics.loader_image", (directory / "missing.png").string());

    // Exact numeric entry keeps dot/scientific syntax even when the user's
    // global C++ locale uses a comma decimal separator.
    struct CommaDecimal : std::numpunct<char> {
      char do_decimal_point() const override
      { return ','; }
    };
    const auto previousLocale = std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
    auto* numeric = models.at("graphics.keyboard_zoom_speed").exact;
    numeric->invoke(0);
    Require(modal && modal->validate(" 4.75e2 ").empty() && !modal->validate("4,75e2").empty(),
            "float entry depends on the process locale");
    Require(modal->commit(" 4.75e2 ") && Config::Get().keyboard_zoom_speed == 475,
            "dot/scientific float did not reach the native value");
    modal.reset();
    models.at("graphics.keyboard_zoom_speed").reset();
    std::locale::global(previousLocale);
    Require(ReadAll(reads) == defaults, "numeric locale test did not restore the baseline");

    // A stale exact editor and a stale prepared reset cannot overwrite a later
    // native shortcut mutation. No worker/config-file behavior is simulated.
    auto* speed = models.at("graphics.keyboard_zoom_speed").exact;
    speed->invoke(0);
    auto stale = std::move(*modal);
    modal.reset();
    auto reset = speed->prepare_reset();
    Config::Get().keyboard_zoom_speed += 25;
    writes.clear();
    Require(!stale.commit("475") && !reset.current() && !reset.apply(), "stale edit/reset overwrote native change");
    Require(writes.empty(), "stale action persisted state");
    Config::Get().keyboard_zoom_speed = DefaultConfig::Graphics::keyboard_zoom_speed;
    labelsAvailable                   = false;
    Require(!models.at("graphics.zoom_label_player_detail").available(),
            "missing fleet-label hook was advertised as supported");
    labelsAvailable = true;
    writes.clear();
    Require(!PersistCurrentSetting("unknown", "key") && writes.empty(), "unknown key was persisted");
    std::filesystem::remove(image);
    std::cout << "Actual upstream catalog passed: " << models.size()
              << " keys; defaults, field mapping, timing, serialization, validation and stale guards verified.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Catalog fixture failed: " << error.what() << '\n';
    return 1;
  }
}

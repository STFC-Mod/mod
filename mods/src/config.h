#pragma once

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.h>

#include "patches/notification_audio.h"

#if _WIN32
#include <Windows.h>
#endif

class SyncConfig
{
public:
  enum class Type {
    AwayAssignments,
    Battles,
    Buffs,
    Buildings,
    EmeraldChain,
    Fleets,
    Haven,
    Inventory,
    Jobs,
    Missions,
    Officer,
    Profile,
    Research,
    Resources,
    Ships,
    Slots,
    StoreOffers,
    Tech,
    Traits
  };

  struct Option {
    Type             type;
    std::string_view type_str;   // used in JSON body
    std::string_view option_str; // used in TOML file
    bool SyncConfig::* option;
  };

  std::string proxy;

  bool verify_ssl       = true;
  bool away_assignments = false;
  bool battlelogs       = false;
  bool buffs            = false;
  bool buildings        = true;
  bool fleets           = false;
  bool haven            = true;
  bool inventory        = false;
  bool jobs             = false;
  bool missions         = false;
  bool officers         = false;
  bool profile          = false;
  bool research         = true;
  bool resources        = false;
  bool ships            = false;
  bool slots            = false;
  bool store_offers     = false;
  bool tech             = false;
  bool traits           = false;

  [[nodiscard]] bool enabled(Type type) const;
};

constexpr std::array SyncOptions{
    SyncConfig::Option{SyncConfig::Type::AwayAssignments, "away_assignment", "away_assignments",
                       &SyncConfig::away_assignments},
    SyncConfig::Option{SyncConfig::Type::Battles, "battlelog", "battlelogs", &SyncConfig::battlelogs},
    SyncConfig::Option{SyncConfig::Type::Buffs, "buff", "buffs", &SyncConfig::buffs},
    SyncConfig::Option{SyncConfig::Type::Buildings, "module", "buildings", &SyncConfig::buildings},
    SyncConfig::Option{SyncConfig::Type::EmeraldChain, "emerald_chain", "buffs", &SyncConfig::buffs},
    SyncConfig::Option{SyncConfig::Type::Fleets, "fleet", "fleets", &SyncConfig::fleets},
    SyncConfig::Option{SyncConfig::Type::Haven, "haven", "haven", &SyncConfig::haven},
    SyncConfig::Option{SyncConfig::Type::Inventory, "inventory", "inventory", &SyncConfig::inventory},
    SyncConfig::Option{SyncConfig::Type::Jobs, "job", "jobs", &SyncConfig::jobs},
    SyncConfig::Option{SyncConfig::Type::Missions, "mission", "missions", &SyncConfig::missions},
    SyncConfig::Option{SyncConfig::Type::Officer, "officer", "officers", &SyncConfig::officers},
    SyncConfig::Option{SyncConfig::Type::Profile, "profile", "profile", &SyncConfig::profile},
    SyncConfig::Option{SyncConfig::Type::Research, "research", "research", &SyncConfig::research},
    SyncConfig::Option{SyncConfig::Type::Resources, "resource", "resources", &SyncConfig::resources},
    SyncConfig::Option{SyncConfig::Type::Ships, "ship", "ships", &SyncConfig::ships},
    SyncConfig::Option{SyncConfig::Type::Slots, "slot", "slots", &SyncConfig::slots},
    SyncConfig::Option{SyncConfig::Type::StoreOffers, "store_offer", "store_offers", &SyncConfig::store_offers},
    SyncConfig::Option{SyncConfig::Type::Tech, "ft", "tech", &SyncConfig::tech},
    SyncConfig::Option{SyncConfig::Type::Traits, "trait", "traits", &SyncConfig::traits},
};

constexpr std::string to_string(const SyncConfig::Type type)
{
  for (const auto& opt : SyncOptions) {
    if (opt.type == type) {
      return std::string(opt.type_str);
    }
  }

  return {};
}

constexpr std::string operator+(const std::string& prefix, const SyncConfig::Type type)
{
  return prefix + to_string(type);
}

constexpr std::string operator+(const SyncConfig::Type type, const std::string& suffix)
{
  return to_string(type) + suffix;
}

class SyncTargetConfig : public SyncConfig
{
public:
  std::string url;
  std::string token;
};

enum class MissionHudVisibility {
  Auto,
  Always,
  Never,
};

enum class InstantWarpConfirmation {
  None,
  Warp,
  Jump,
};

enum class FleetLabelDetail {
  Native,
  Expanded,
  Compact,
  Threshold,
};

struct FleetLabelProfile {
  FleetLabelDetail detail;
  float            zoom_threshold;
};

// Part of UI Scale
void ApplyUiShipScaleToLoadedShips(float old_multiplier, float new_multiplier);

class Config final
{
public:
  Config();

  [[nodiscard]] static Config& Get();
  [[nodiscard]] static float   GetDPI();
  static float                 RefreshDPI();

#ifdef _WIN32
  [[nodiscard]] static HWND WindowHandle();
#endif

  static void Save(const toml::table& config, std::string_view filename, bool apply_warning = true);
  void        Load();
  void        AdjustUiScale(bool scaleUp);
  void        AdjustUiShipScale(bool scaleUp);
  void        AdjustUiViewerScale(bool scaleUp);

  [[nodiscard]] MissionHudVisibility MissionHudButtonVisibility(std::string_view button_name) const;
  [[nodiscard]] NotificationSound    NotificationSoundForToast(int toast_state) const;

  // Disallow copying/moving to enforce singleton
  Config(const Config&)            = delete;
  Config& operator=(const Config&) = delete;
  Config(Config&&)                 = delete;
  Config& operator=(Config&&)      = delete;

  float             ui_scale;
  float             ui_scale_step;
  float             ui_scale_ship;
  float             ui_scale_viewer;
  float             zoom;
  float             haven_zoom;
  float             interior_zoom_scale;
  float             fr_scale;
  FleetLabelProfile zoom_label_player;
  FleetLabelProfile zoom_label_non_player;
  bool              allow_cursor;
  bool              free_resize;
  bool              adjust_scale_res;

  bool  use_out_of_dock_power;
  float system_pan_momentum;
  float system_pan_momentum_falloff;
  bool disable_target_camera_lock;

  float keyboard_zoom_speed;
  int   select_timer;

  bool  queue_enabled;
  bool  kirshara_fixes;
  bool  hotkeys_enabled;
  bool  hotkeys_extended;
  bool  primary_requires_visible_target;
  bool  use_scopely_hotkeys;
  bool  use_presets_as_default;
  bool  enable_experimental;
  float default_system_zoom;

  float system_zoom_preset_1;
  float system_zoom_preset_2;
  float system_zoom_preset_3;
  float system_zoom_preset_4;
  float system_zoom_preset_5;
  float transition_time;

  bool             borderless_fullscreen;
  std::vector<int> disabled_banner_types;
  std::vector<int> notify_banner_types;

  int                      extend_chest_purchase_max;
  int                      extend_donation_max;
  bool                     extend_donation_slider;
  bool                     disable_move_keys;
  bool                     disable_preview_locate;
  bool                     disable_preview_recall;
  bool                     disable_escape_exit;
  int                      disable_escape_exit_timer;
  bool                     disable_galaxy_chat;
  bool                     disable_veil_chat;
  bool                     show_player_title;
  bool                     disable_first_popup;
  bool                     disable_toast_banners;
  bool                     trace_audio_events;
  bool                     disable_all_audio_events;
  std::vector<std::string> disabled_audio_events;
  NotificationSound alert_victory            = NotificationSound::None;
  NotificationSound alert_defeat             = NotificationSound::None;
  NotificationSound alert_armada_created     = NotificationSound::None;
  NotificationSound alert_armada_battle_won  = NotificationSound::None;
  NotificationSound alert_armada_battle_lost = NotificationSound::None;
  bool                     auto_open_bulk_claim_flyout;
  bool                     auto_confirm_ft_upgrade;
  bool                     forbidden_tech_ship_name;
  bool                     auto_confirm_reveal_ability;
  bool                     auto_confirm_alliance_contribution;

  std::vector<std::string> daily_bulk_claim_factions;

  bool daily_bulk_claim_toggle_default_on;

  InstantWarpConfirmation auto_confirm_instant_warp;

  std::vector<std::string> instant_warp_auto_jump;
  std::vector<std::string> instant_warp_auto_warp;
  bool                     instant_warp_auto_jump_all = false;
  bool                     instant_warp_auto_warp_all = false;

  std::vector<std::string> instant_warp_always_ask;
  bool                     instant_warp_always_ask_all = false;

  std::vector<std::string> pinned_ships;

  bool double_click_to_assign_ship;
  bool focus_search;
  bool format_cargo_values;
  bool officer_sort;
  bool reverse_haven_history;
  bool arrow_keys_to_select_ship;
  bool dock_syncmanagement;

  bool only_show_first_popup;

  bool show_cargo_default;
  bool show_player_cargo;
  bool show_station_cargo;
  bool show_hostile_cargo;
  bool show_armada_cargo;
  bool instant_cargo_counter;

  bool                                        always_skip_reveal_sequence;
  std::map<std::string, MissionHudVisibility> mission_hud_buttons;

  bool       sync_logging;
  bool       sync_debug;
  int        sync_resolver_cache_ttl;
  SyncConfig sync_options;

  std::map<std::string, SyncTargetConfig> sync_targets;

  bool installUiScaleHooks;
  bool installZoomHooks;
  bool installHavenZoomHooks;
  bool installInteriorZoomHooks;
  bool installBuffFixHooks;
  bool installToastBannerHooks;
  bool installPanHooks;
  bool installHotkeyHooks;
  bool installFreeResizeHooks;
  bool installTempCrashFixes;
  bool installTestPatches;
  bool installMiscPatches;
  bool installMissionHudTweaksHooks;
  bool disable_exchange_all;
  bool installChatPatches;
  bool installPlayerTitleDisplayHooks;
  bool installSyncPatches;
  bool installGameVersionHook;
  bool installObjectTracker;
  bool installGiftsBulkClaimHooks;
  bool installDailyFactionBulkClaimHooks;
  bool installGalacticAnomalyTimerHooks;
  bool installLoadingTipHooks;
  bool installDoubleClickAssignShipHooks;
  bool installForbiddenTechConfirmationHooks;
  bool installForbiddenTechShipNameHooks;
  bool installRevealConfirmationHooks;
  bool installInstantWarpConfirmationHooks;
  bool installAudioEventHooks;
  bool installActionQueueRecoveryHooks;
  bool installQueueAddressGuardHooks;
  bool installThinQueueProtectionHooks;
  bool installArtifactExchangeHooks;

  std::string config_settings_url;
  std::string config_assets_url_override;

  // Loading Screen / Transition Screen
  bool        loader_enabled;
  bool        loader_transition;
  bool        loader_transition_black;
  std::string loader_image;
  float       loader_logo_scale;
  bool        loader_tip_enabled;
  bool        galactic_anomaly_timer;

  bool installLoadingScreenHooks;
  bool installTransitionScreenHooks;

  // Cargo formatting
  bool installCargoFormatHooks;
  bool installInstantCargoCounterHooks;
  int  cargo_significant_decimals;

  // Officer roster/assignment "Below Deck Ability" sort option restore
  bool installOfficerSortHooks;

  // Fleet management dock ship sort: pin configured ships to the front
  bool installPinnedShipSortHooks;

  // Fleet management dock: SHIFT+click a ship tile to pin/unpin it
  bool installPinnedShipClickToggle;

  // Haven help-history list ordering
  bool installHavenHistoryHooks;

  // Native Alliance Games store research, currency and purchase refresh
  bool installAllianceGamesStoreHooks;
};

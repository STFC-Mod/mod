#pragma once

// User settings from the settings TOML. Each struct is a TOML section and each member is named after its key,
// with its default as the initializer: Config::Get().graphics.ui_scale is [graphics] ui_scale. They are read at
// startup and again whenever the file changes; a few are also toggled at runtime by hotkeys. Everything runs on
// the game thread except sync, whose workers retain immutable snapshots.

#include "features/notification_audio.h"
#include "shortcuts.h"

#include <toml++/impl/forward_declarations.hpp>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Which categories of game data are uploaded, globally ([sync]) or for one target ([sync.targets.<name>]).
struct SyncConfig {
  struct Option {
    std::string_view option_str; // used in TOML file
    bool SyncConfig::* option;
    std::string_view   old_name = {};
  };

  std::string proxy;
  bool        verify_ssl       = true;
  bool        away_assignments = false;
  bool        battlelogs       = false;
  bool        buffs            = false;
  bool        buildings        = false;
  bool        fleets           = false;
  bool        haven            = false;
  bool        inventory        = false;
  bool        jobs             = false;
  bool        missions         = false;
  bool        officers         = false;
  bool        profile          = false;
  bool        research         = false;
  bool        resources        = false;
  bool        ships            = false;
  bool        slots            = false;
  bool        store_offers     = false;
  bool        tech             = false;
  bool        traits           = false;

  bool operator==(const SyncConfig&) const = default;
};

constexpr std::array SyncOptions{
    SyncConfig::Option{"away_assignments", &SyncConfig::away_assignments},
    SyncConfig::Option{"battlelogs", &SyncConfig::battlelogs},
    SyncConfig::Option{"buffs", &SyncConfig::buffs},
    SyncConfig::Option{"buildings", &SyncConfig::buildings},
    SyncConfig::Option{"fleets", &SyncConfig::fleets},
    SyncConfig::Option{"haven", &SyncConfig::haven},
    SyncConfig::Option{"inventory", &SyncConfig::inventory},
    SyncConfig::Option{"jobs", &SyncConfig::jobs},
    SyncConfig::Option{"missions", &SyncConfig::missions},
    SyncConfig::Option{"officers", &SyncConfig::officers, "officer"},
    SyncConfig::Option{"profile", &SyncConfig::profile},
    SyncConfig::Option{"research", &SyncConfig::research},
    SyncConfig::Option{"resources", &SyncConfig::resources},
    SyncConfig::Option{"ships", &SyncConfig::ships},
    SyncConfig::Option{"slots", &SyncConfig::slots},
    SyncConfig::Option{"store_offers", &SyncConfig::store_offers},
    SyncConfig::Option{"tech", &SyncConfig::tech},
    SyncConfig::Option{"traits", &SyncConfig::traits},
};

struct SyncTargetConfig : SyncConfig {
  std::string url;
  std::string token;
  bool        operator==(const SyncTargetConfig&) const = default;
};

enum class MissionHudVisibility {
  kAuto,
  kAlways,
  kNever,
};

enum class InstantWarpConfirmation {
  kNone,
  kWarp,
  kJump,
};

enum class FleetLabelDetail {
  kNative,
  kExpanded,
  kCompact,
  kThreshold,
};

struct FleetLabelProfile {
  FleetLabelDetail detail         = FleetLabelDetail::kNative;
  float            zoom_threshold = 0.5f;
};

// Ship names from a comma list, each split by ShipNameWords, or every ship for "*".
struct ShipFilter {
  std::vector<std::vector<std::string>> names;
  bool                                  all = false;
};

class Config final
{
public:
  // Game thread only. Call it again rather than keeping the reference: a reload replaces the settings.
  [[nodiscard]] static Config& Get();
  // Re-reads the settings file and makes it current, rebinding shortcuts. A file that does not parse keeps the
  // current settings. [patches] decides which hooks are installed, so its changes wait for a restart. Game thread.
  static bool Reload();
  // The frame tick's check: reloads once the settings file has changed and then stayed unchanged for a second.
  static void ReloadIfChanged();

  Config(const Config&)            = delete;
  Config& operator=(const Config&) = delete;

  // Hook groups. Users switch these off when another mod (such as a BepInEx plugin) hooks the same methods.
  struct Patches {
    bool pinnedshipclicktoggle      = true;
    bool thinqueueprotectionhooks   = true;
    bool queueaddressguardhooks     = true;
    bool actionqueuerecoveryhooks   = true;
    bool revealconfirmhooks         = true;
    bool chattitledisplayhooks      = true;
    bool interiorzoomhooks          = true;
    bool uiscalehooks               = true;
    bool zoomhooks                  = true;
    bool havenzoomhooks             = true;
    bool havenhistoryhooks          = true;
    bool alliancegamesstorehooks    = true;
    bool bufffixhooks               = true;
    bool toastbannerhooks           = true;
    bool panhooks                   = true;
    bool hotkeyhooks                = true;
    bool freeresizehooks            = true;
    bool tempcrashfixes             = true;
    bool testpatches                = true;
    bool miscpatches                = true;
    bool missionhudtweakshooks      = true;
    bool chatpatches                = true;
    bool syncpatches                = true;
    bool game_version               = true;
    bool objecttracker              = true;
    bool loadingscreenhooks         = true;
    bool transitionscreenhooks      = true;
    bool giftsbulkclaimhooks        = true;
    bool dailyfactionbulkclaimhooks = true;
    bool galacticanomalytimerhooks  = true;
    bool loadingtiphooks            = true;
    bool doubleclickassignshiphooks = true;
    bool forbiddentechconfirmhooks  = true;
    bool forbiddentechshipnamehooks = true;
    bool audioeventhooks            = true;
    bool instantcargocounterhooks   = true;
    bool cargoformathooks           = true;
    bool officersorthooks           = true;
    bool pinnedshiphooks            = true;
    bool artifactexchangehooks      = true;

    bool operator==(const Patches&) const = default;
  } patches;

  struct Control {
    bool primary_requires_visible_target = false;
    bool kirshara_fixes                  = true;
    bool queue_enabled                   = true;
    bool hotkeys_enabled                 = true;
    bool hotkeys_extended                = true;
    bool use_scopely_hotkeys             = false;
    // Milliseconds within which pressing a ship's number again flies the camera to it.
    int  select_timer        = 500;
    bool enable_experimental = false;
  } control;

  struct Graphics {
    float interior_zoom_scale = 2.0f;
    float ui_scale            = 0.6f;
    float ui_scale_step       = 0.05f;
    float ui_scale_ship       = 1.0f;
    float ui_scale_viewer     = 1.2f;
    // The farthest system zoom; presets and zoom shortcuts are in the same unit.
    float             zoom       = 5000.0f;
    float             haven_zoom = 4000.0f;
    float             fr_scale   = 2.0f;
    FleetLabelProfile zoom_label_player;
    FleetLabelProfile zoom_label_non_player;
    bool              free_resize         = true;
    bool              allow_cursor        = true;
    float             keyboard_zoom_speed = 350.0f;
    // TODO: read (with enable_experimental) but nothing applies it.
    float system_pan_momentum = 0.4f;
    // How much of the pan speed is kept each frame after a drag or key is released.
    float system_pan_momentum_falloff = 0.8f;
    bool  disable_target_camera_lock  = false;
    // TODO: the F11 borderless toggle needs a SetWindowLongW detour that was never installed, so this does nothing.
    bool borderless_fullscreen = true;
    // TODO: read but nothing applies it.
    float transition_time        = 0.01f;
    float default_system_zoom    = 1750.0f;
    float system_zoom_preset_1   = 50.0f;
    float system_zoom_preset_2   = 500.0f;
    float system_zoom_preset_3   = 1250.0f;
    float system_zoom_preset_4   = 2750.0f;
    float system_zoom_preset_5   = 5000.0f;
    bool  use_presets_as_default = true;
    // Replace the login background, and the transition (section change) backgrounds.
    bool loader_enabled    = true;
    bool loader_transition = true;
    // Transitions keep the game's black background and only get the logos.
    bool loader_transition_black = false;
    // A PNG to use instead of the embedded loading image.
    std::string loader_image;
    float       loader_logo_scale      = 1.0f;
    bool        loader_tip_enabled     = true;
    bool        galactic_anomaly_timer = false;
  } graphics;

  struct Buffs {
    bool use_out_of_dock_power = true;
  } buffs;

  struct Audio {
    bool                     trace_events = false;
    std::vector<std::string> disabled_events;
    bool                     disable_all_events       = false;
    NotificationSound        alert_victory            = NotificationSound::kNone;
    NotificationSound        alert_defeat             = NotificationSound::kNone;
    NotificationSound        alert_armada_created     = NotificationSound::kNone;
    NotificationSound        alert_armada_battle_won  = NotificationSound::kNone;
    NotificationSound        alert_armada_battle_lost = NotificationSound::kNone;
  } audio;

  struct Ui {
    bool dock_syncmanagement         = true;
    bool auto_confirm_reveal_ability = false;
    bool show_player_title           = true;
    bool disable_escape_exit         = true;
    // Maximum gap in milliseconds between two Escape presses that opens the exit prompt; 0 never opens it.
    int  disable_escape_exit_timer = 0;
    bool disable_preview_locate    = false;
    bool disable_preview_recall    = false;
    bool only_show_first_popup     = false;
    bool reverse_haven_history     = true;
    bool disable_toast_banners     = false;
    bool disable_move_keys         = false;
    bool disable_galaxy_chat       = false;
    bool disable_veil_chat         = false;

    // Side chat: its initial width in canvas units (0 keeps the game's), and opening it once per login.
    float default_chat_width = 0.0f;
    bool  auto_open_chat     = false;

    bool                     auto_open_bulk_claim_flyout = false;
    std::vector<std::string> daily_bulk_claim_factions{"federation", "klingon", "romulan"};
    bool                     daily_bulk_claim_toggle_default_on = false;

    InstantWarpConfirmation               auto_confirm_instant_warp = InstantWarpConfirmation::kNone;
    ShipFilter                            instant_warp_auto_jump;
    ShipFilter                            instant_warp_auto_warp;
    ShipFilter                            instant_warp_always_ask;
    std::vector<std::vector<std::string>> pinned_ships;

    bool double_click_to_assign_ship = false;
    bool focus_search                = true;
    // Abbreviated cargo values use cargo_significant_decimals.
    bool format_cargo_values = true;
    // Restores the Below Deck Ability officer sort option.
    bool officer_sort              = true;
    bool arrow_keys_to_select_ship = true;
    bool auto_confirm_ft_upgrade   = false;
    // Shows the equipped ship below the Forbidden Tech header, including ships without a dock.
    bool forbidden_tech_ship_name  = true;
    bool auto_confirm_alliance_contribution = false;

    int  extend_chest_purchase_max = 160;
    bool extend_donation_slider    = true;
    int  extend_donation_max       = 80;

    bool show_cargo_default    = true;
    bool show_player_cargo     = true;
    bool show_station_cargo    = true;
    bool show_hostile_cargo    = true;
    bool show_armada_cargo     = true;
    bool instant_cargo_counter = true;
    // Decimal places in abbreviated cargo values such as 1.25M.
    int cargo_significant_decimals = 2;

    bool always_skip_reveal_sequence = true;
    // Hides the artifact shards popup's Exchange All button; single exchanges stay. Takes effect on the next popup.
    bool                 disable_exchange_all = false;
    MissionHudVisibility hud_q_trials         = MissionHudVisibility::kAuto;
    MissionHudVisibility hud_field_training   = MissionHudVisibility::kAuto;
    MissionHudVisibility hud_outposts         = MissionHudVisibility::kAuto;
    MissionHudVisibility hud_missions         = MissionHudVisibility::kAuto;

    // ToastState values.
    std::vector<int> disabled_banner_types;
    std::vector<int> notify_banner_types;
  } ui;

  struct Sync {
    // Workers keep this snapshot alive while reading; reloads publish a separate immutable copy.
    [[nodiscard]] static std::shared_ptr<const Sync> Get();

    bool debug   = false;
    bool logging = false;
    // Seconds that resolved player and alliance names stay cached.
    int resolver_cache_ttl = 300;
    // The categories any target wants, so the game hooks can skip the rest.
    SyncConfig                              options;
    std::map<std::string, SyncTargetConfig> targets;
  };

  // [config]: overrides for the game's own server settings.
  struct Game {
    std::string settings_url;
    std::string assets_url_override;
  } game;

  // Settings from a parsed settings file; an empty table gives the defaults. `effective` receives every value
  // used, which becomes the runtime vars file. Logs what it rejects, and each value at debug level when
  // `log_values`. Does not change the live settings or shortcuts.
  static std::unique_ptr<Config> Parse(const toml::table& file, toml::table& effective, bool log_values);

private:
  Shortcut::Bindings          shortcuts_;
  std::shared_ptr<const Sync> sync_;
  Config() = default;
  void Read(const toml::table& file, toml::table& effective, bool log_values);
};

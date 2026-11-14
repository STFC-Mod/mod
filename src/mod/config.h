#pragma once

// User settings from the settings TOML. Each struct is a TOML section and each member is named after its key,
// with its default as the initializer: Config::Get().graphics.ui_scale is [graphics] ui_scale. They are read at
// startup and again whenever the file changes; a few are also toggled at runtime by hotkeys. Everything runs on
// the game thread except sync, whose workers retain immutable snapshots.

#include "shortcuts.h"

#include <toml++/impl/forward_declarations.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Which categories of game data are uploaded, globally ([sync]) or for one target ([sync.targets.<name>]).
struct SyncConfig {
  struct Option {
    std::string_view key;
    bool SyncConfig::* option;
    // The key's name in older releases, still read when the new one is missing.
    std::string_view old_name = {};
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

// Every category, as [sync] and [sync.targets.<name>] spell it.
constexpr auto SyncOptions = std::to_array<SyncConfig::Option>({
    {"away_assignments", &SyncConfig::away_assignments},
    {"battlelogs", &SyncConfig::battlelogs},
    {"buffs", &SyncConfig::buffs},
    {"buildings", &SyncConfig::buildings},
    {"fleets", &SyncConfig::fleets},
    {"haven", &SyncConfig::haven},
    {"inventory", &SyncConfig::inventory},
    {"jobs", &SyncConfig::jobs},
    {"missions", &SyncConfig::missions},
    {"officers", &SyncConfig::officers, "officer"},
    {"profile", &SyncConfig::profile},
    {"research", &SyncConfig::research},
    {"resources", &SyncConfig::resources},
    {"ships", &SyncConfig::ships},
    {"slots", &SyncConfig::slots},
    {"store_offers", &SyncConfig::store_offers},
    {"tech", &SyncConfig::tech},
    {"traits", &SyncConfig::traits},
});

struct SyncTargetConfig : SyncConfig {
  std::string url;
  std::string token;
  bool        operator==(const SyncTargetConfig&) const = default;
};

// The cues [audio] alert_* can play; notification_audio.cc generates them.
enum class NotificationSound : uint8_t {
  kNone,
  kDefault,
  kInfo,
  kSuccess,
  kWarning,
  kAlarm,
  kArrival,
  kSoft,
  kPing,
  kRepair,
  kCount,
};

// The cue's name as the settings file writes it: "success".
std::string_view NotificationSoundName(NotificationSound sound);

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

  // Hook groups, one per [patches] key in alphabetical order. Users switch one off when another mod (such as a
  // BepInEx plugin) hooks the same methods, or a game update breaks it. Read once: hooks install at startup.
  struct Patches {
    bool actionqueuerecoveryhooks   = true;
    bool alliancegamesstorehooks    = true;
    bool artifactexchangehooks      = true;
    bool audioeventhooks            = true;
    bool bufffixhooks               = true;
    bool cargoformathooks           = true;
    bool chatpatches                = true;
    bool chattitledisplayhooks      = true;
    bool dailyfactionbulkclaimhooks = true;
    bool doubleclickassignshiphooks = true;
    bool forbiddentechconfirmhooks  = true;
    bool forbiddentechshipnamehooks = true;
    bool freeresizehooks            = true;
    bool galacticanomalytimerhooks  = true;
    bool game_version               = true;
    bool giftsbulkclaimhooks        = true;
    bool havenhistoryhooks          = true;
    bool havenzoomhooks             = true;
    bool hotkeyhooks                = true;
    bool instantcargocounterhooks   = true;
    bool interiorzoomhooks          = true;
    bool loadingscreenhooks         = true;
    bool loadingtiphooks            = true;
    bool miscpatches                = true;
    bool missionhudtweakshooks      = true;
    bool objecttracker              = true;
    bool officersorthooks           = true;
    bool panhooks                   = true;
    bool pinnedshipclicktoggle      = true;
    bool pinnedshiphooks            = true;
    bool queueaddressguardhooks     = true;
    bool revealconfirmhooks         = true;
    bool syncpatches                = true;
    bool tempcrashfixes             = true;
    bool testpatches                = true;
    bool thinqueueprotectionhooks   = true;
    bool toastbannerhooks           = true;
    bool transitionscreenhooks      = true;
    bool uiscalehooks               = true;
    bool zoomhooks                  = true;

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
    float ui_scale        = 0.6f;
    float ui_scale_step   = 0.05f;
    float ui_scale_ship   = 1.0f;
    float ui_scale_viewer = 1.2f;
    // The farthest system zoom; presets and zoom shortcuts are in the same unit.
    float zoom       = 5000.0f;
    float haven_zoom = 4000.0f;
    // A multiple of the station interior's own zoom-out distance, at least 1.
    float             interior_zoom_scale = 2.0f;
    float             fr_scale            = 2.0f;
    FleetLabelProfile zoom_label_player;
    FleetLabelProfile zoom_label_non_player;
    bool              free_resize         = true;
    bool              allow_cursor        = true;
    float             keyboard_zoom_speed = 350.0f;
    float             system_pan_speed    = 1.0f;
    float             galaxy_pan_speed    = 1.0f;
    // Retained compatibility key; use system_pan_speed for panning speed.
    float system_pan_momentum = 0.4f;
    // How much of the pan speed is kept each frame after a drag or key is released.
    float system_pan_momentum_falloff = 0.8f;
    bool  disable_target_camera_lock  = false;
    // TODO(tashcan): the F11 borderless toggle needs a SetWindowLongW detour that was never installed, so this does
    // nothing.
    bool borderless_fullscreen = true;
    // TODO(tashcan): read but nothing applies it.
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
    bool disable_escape_exit = true;
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
    bool show_player_title         = true;

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
    // On the ship management screen, select the managed ship's dock slot as well.
    bool dock_syncmanagement = true;
    bool focus_search        = true;
    // Abbreviated cargo values use cargo_significant_decimals.
    bool format_cargo_values = true;
    // Restores the Below Deck Ability officer sort option.
    bool officer_sort                       = true;
    bool arrow_keys_to_select_ship          = true;
    bool auto_confirm_ft_upgrade            = false;
    // Shows the equipped ship below the Forbidden Tech header, including ships without a dock.
    bool forbidden_tech_ship_name           = true;
    bool auto_confirm_alliance_contribution = false;
    bool auto_confirm_reveal_ability        = false;

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
    // The categories any target wants. Only battle logs are skipped when no target wants them; the rest are stored
    // regardless, so a target switched on later gets them at once.
    SyncConfig                              options;
    std::map<std::string, SyncTargetConfig> targets;
  };

  // [config], named Game because Config is taken: overrides for the game's own server settings.
  struct Game {
    std::string settings_url;
    std::string assets_url_override;
  } game;

  // Settings from a parsed settings file; an empty table gives the defaults. `effective` receives every value
  // used, which becomes the runtime vars file. Logs what it rejects, and each value at debug level when
  // `log_values`. Does not change the live settings or shortcuts.
  static std::unique_ptr<Config> Parse(const toml::table& file, toml::table& effective, bool log_values);

private:
  // Held until Activate, so Parse can build a candidate without changing the live bindings or the workers' snapshot.
  Shortcut::Bindings          shortcuts_;
  std::shared_ptr<const Sync> sync_;

  Config() = default;
  void Read(const toml::table& file, toml::table& effective, bool log_values);
  // Makes `next` current: binds its shortcuts and publishes its sync snapshot to the workers.
  static void Activate(std::unique_ptr<Config> next);
};

#pragma once

namespace DefaultConfig
{
namespace Buffs
{
  constexpr bool use_out_of_dock_power = true;
} // namespace Buffs

namespace Audio
{
  constexpr const char* disabled_events = "";
  constexpr bool        trace_events    = false;
  constexpr const char* alert_victory = "none";
  constexpr const char* alert_defeat = "none";
  constexpr const char* alert_armada_created = "none";
  constexpr const char* alert_armada_battle_won = "none";
  constexpr const char* alert_armada_battle_lost = "none";
} // namespace Audio

namespace SystemConfig
{
  constexpr const char* assets_url_override = "";
  constexpr const char* settings_url        = "";
} // namespace SystemConfig

namespace Control
{
  constexpr bool enable_experimental             = false;
  constexpr bool hotkeys_enabled                 = true;
  constexpr bool hotkeys_extended                = true;
  constexpr bool primary_requires_visible_target = false;
  constexpr bool use_scopely_hotkeys             = false;
  constexpr bool queue_enabled                   = true;
  constexpr bool kirshara_fixes                  = true;
  constexpr auto select_timer                    = 500;
} // namespace Control

namespace Graphics
{
  constexpr bool        galactic_anomaly_timer          = false;
  constexpr bool        borderless_fullscreen           = true;
  constexpr bool        allow_cursor                    = true;
  constexpr const char* zoom_label_player_detail        = "native";
  constexpr auto        zoom_label_player_threshold     = 0.5;
  constexpr const char* zoom_label_non_player_detail    = "native";
  constexpr auto        zoom_label_non_player_threshold = 0.5;
  constexpr auto        default_system_zoom             = 1750;
  constexpr bool        free_resize                     = true;
  constexpr auto        keyboard_zoom_speed             = 350;
  constexpr auto        system_pan_momentum_falloff     = 0.8;
  constexpr bool        disable_target_camera_lock = false;
  constexpr auto        system_pan_momentum             = 0.4;
  constexpr auto        system_zoom_preset_1            = 50;
  constexpr auto        system_zoom_preset_2            = 500;
  constexpr auto        system_zoom_preset_3            = 1250;
  constexpr auto        system_zoom_preset_4            = 2750;
  constexpr auto        system_zoom_preset_5            = 5000;
  constexpr auto        transition_time                 = 0.01;
  constexpr auto        ui_scale                        = 0.6;
  constexpr auto        ui_scale_step                   = 0.05;
  constexpr auto        ui_scale_ship                   = 1.0;
  constexpr auto        ui_scale_viewer                 = 1.2;
  constexpr bool        use_presets_as_default          = true;
  constexpr auto        zoom                            = 5000;
  constexpr auto        haven_zoom                      = 4000.0;
  constexpr auto        interior_zoom_scale             = 2.0; // station-interior zoom-out multiplier; 1.0 = native
  constexpr auto        fr_scale                        = 2.0;
  constexpr bool        loader_enabled                  = true; // replace LoginSequence background
  constexpr bool        loader_transition               = true; // replace TVC/SlideShow backgrounds
#ifdef _USE_ORIGINAL_BG
  constexpr bool loader_transition_black = true; // original BG mode: use black transition
#else
  constexpr bool loader_transition_black = false; // transition: use black BG instead of custom image
#endif
  constexpr const char* loader_image       = "";   // Empty = use embedded fallback
  constexpr auto        loader_logo_scale  = 1.0;  // multiplier for logo size
  constexpr bool        loader_tip_enabled = true; // show custom tip on loading screen
} // namespace Graphics

namespace Patches
{
  constexpr bool bufffixhooks               = true;
  constexpr bool chatpatches                = true;
  constexpr bool chattitledisplayhooks      = true;
  constexpr bool freeresizehooks            = true;
  constexpr bool game_version               = true;
  constexpr bool hotkeyhooks                = true;
  constexpr bool loadingscreenhooks         = true;
  constexpr bool transitionscreenhooks      = true;
  constexpr bool objecttracker              = true;
  constexpr bool panhooks                   = true;
  constexpr bool syncpatches                = true;
  constexpr bool tempcrashfixes             = true;
  constexpr bool testpatches                = true;
  constexpr bool toastbannerhooks           = true;
  constexpr bool uiscalehooks               = true;
  constexpr bool zoomhooks                  = true;
  constexpr bool havenzoomhooks             = true;
  constexpr bool interiorzoomhooks          = true;
  constexpr bool miscpatches                = true;
  constexpr bool giftsbulkclaimhooks        = true;
  constexpr bool dailyfactionbulkclaimhooks = true;
  constexpr bool missionhudtweakshooks      = true;
  constexpr bool galacticanomalytimerhooks  = true;
  constexpr bool loadingtiphooks            = true;
  constexpr bool doubleclickassignshiphooks = true;
  constexpr bool forbiddentechconfirmhooks  = true;
  constexpr bool forbiddentechshipnamehooks = true;
  constexpr bool audioeventhooks            = true;
  constexpr bool actionqueuerecoveryhooks   = true;
  constexpr bool queueaddressguardhooks     = true;
  constexpr bool thinqueueprotectionhooks   = true;
  constexpr bool artifactexchangehooks      = true;
  constexpr bool instantcargocounterhooks   = true;
  constexpr bool cargoformathooks           = true; // on by default: cargo number precision override
  constexpr bool officersorthooks           = true; // restore Below Deck Ability sort option
  constexpr bool pinnedshiphooks            = true; // pin configured ships to front of fleet dock sort
  constexpr bool pinnedshipclicktoggle      = true; // SHIFT+click a fleet dock ship tile to pin/unpin it
  constexpr bool havenhistoryhooks          = true; // reverse Haven help history into newest-first order
  constexpr bool revealconfirmhooks         = true; // auto-confirm the Excelsior Decloaking Deflector popup when enabled in [ui]
  constexpr bool alliancegamesstorehooks   = true; // repair Alliance Games store research/currency/refresh
} // namespace Patches

namespace Shortcuts
{
  constexpr const char* toggle_queue         = "CTRL-Q";
  constexpr const char* action_queue         = "SPACE|MOUSE1";
  constexpr const char* action_queue_clear   = "CTRL-C";
  constexpr const char* action_primary       = "SPACE|MOUSE1";
  constexpr const char* action_recall        = "R|MOUSE3";
  constexpr const char* action_recall_cancel = "SPACE|MOUSE1";
  constexpr const char* action_repair        = "R|MOUSE3";
  constexpr const char* action_secondary     = "TAB|MOUSE4";
  constexpr const char* action_view          = "V|MOUSE2";
  constexpr const char* set_hotkeys_disabled = "CTRL-ALT-MINUS";
  constexpr const char* set_hotkeys_enabled  = "CTRL-ALT-=";
  constexpr const char* log_off              = "CTRL-SHIFT-F12";
  constexpr const char* log_error            = "CTRL-SHIFT-F11";
  constexpr const char* log_warn             = "CTRL-SHIFT-F10";
  constexpr const char* log_debug            = "CTRL-SHIFT-F9";
  constexpr const char* log_info             = "CTRL-SHIFT-F8";
  constexpr const char* log_trace            = "CTRL-SHIFT-F7";
  constexpr const char* restart              = "F9";
  constexpr const char* quit                 = "F10";
  constexpr const char* select_chatalliance  = "CTRL-2";
  constexpr const char* select_chatglobal    = "CTRL-1";
  constexpr const char* select_chatprivate   = "CTRL-3";
  constexpr const char* select_current       = "CTRL-SPACE";
  constexpr const char* select_ship1         = "1";
  constexpr const char* select_ship2         = "2";
  constexpr const char* select_ship3         = "3";
  constexpr const char* select_ship4         = "4";
  constexpr const char* select_ship5         = "5";
  constexpr const char* select_ship6         = "6";
  constexpr const char* select_ship7         = "7";
  constexpr const char* select_ship8         = "8";
  constexpr const char* focus_search         = "CTRL-F";
  constexpr const char* set_zoom_default     = "CTRL-=";
  constexpr const char* set_zoom_preset1     = "SHIFT-F1";
  constexpr const char* set_zoom_preset2     = "SHIFT-F2";
  constexpr const char* set_zoom_preset3     = "SHIFT-F3";
  constexpr const char* set_zoom_preset4     = "SHIFT-F4";
  constexpr const char* set_zoom_preset5     = "SHIFT-F5";

  constexpr const char* show_events_native = "CTRL-E";
  constexpr const char* show_galaxy_native = "CTRL-G";

  constexpr const char* show_alliance         = "ALT-'";
  constexpr const char* show_alliance_armada  = "CTRL-'";
  constexpr const char* show_alliance_help    = "SHIFT-'";
  constexpr const char* show_artifacts        = "SHIFT-I";
  constexpr const char* show_awayteam         = "T";
  constexpr const char* show_bookmarks        = "B";
  constexpr const char* show_chat             = "C";
  constexpr const char* show_chatside1        = "ALT-C";
  constexpr const char* show_chatside2        = "`";
  constexpr const char* show_commander        = "O";
  constexpr const char* show_daily            = "Z";
  constexpr const char* show_events           = "SHIFT-E";
  constexpr const char* show_exocomp          = "X";
  constexpr const char* show_factions         = "F";
  constexpr const char* show_galaxy           = "G";
  constexpr const char* show_gifts            = "/";
  constexpr const char* show_inventory        = "I";
  constexpr const char* show_lookup           = "L";
  constexpr const char* show_missions         = "M";
  constexpr const char* show_officers         = "SHIFT-O";
  constexpr const char* show_officerpresets   = "CTRL-O";
  constexpr const char* show_officerswap      = "ALT-O";
  constexpr const char* show_qtrials          = "SHIFT-Q";
  constexpr const char* show_refinery         = "SHIFT-F";
  constexpr const char* show_research         = "U";
  constexpr const char* show_scrapyard        = "Y";
  constexpr const char* show_settings         = "SHIFT-S";
  constexpr const char* show_ships            = "N";
  constexpr const char* show_shipconstruction = "SHIFT-N";
  constexpr const char* show_shipswap         = "ALT-N";
  constexpr const char* show_shields          = "CTRL-S";
  constexpr const char* show_battlelogs       = "SHIFT-B";
  constexpr const char* show_stationexterior  = "SHIFT-G";
  constexpr const char* show_stationinterior  = "SHIFT-H";
  constexpr const char* show_haven            = "ALT-H";
  constexpr const char* show_system           = "H";
  constexpr const char* toggle_shortcut_hints = "HOME";
  constexpr const char* toggle_cargo_armada   = "ALT-5";
  constexpr const char* toggle_cargo_default  = "ALT-1";
  constexpr const char* toggle_cargo_hostile  = "ALT-4";
  constexpr const char* toggle_cargo_player   = "ALT-2";
  constexpr const char* toggle_cargo_station  = "ALT-3";
  constexpr const char* toggle_instant_warp   = "ALT-I";
  constexpr const char* toggle_preview_locate = "CTRL-R";
  constexpr const char* toggle_preview_recall = "CTRL-T";
  constexpr const char* ui_scaledown          = "PGDOWN";
  constexpr const char* ui_scaleup            = "PGUP";
  constexpr const char* ui_scaleshipdown      = "CTRL-PGDOWN";
  constexpr const char* ui_scaleshipup        = "CTRL-PGUP";
  constexpr const char* ui_scaleviewerdown    = "SHIFT-PGDOWN";
  constexpr const char* ui_scaleviewerup      = "SHIFT-PGUP";
  constexpr const char* zoom_in               = "Q";
  constexpr const char* zoom_max              = "MINUS";
  constexpr const char* zoom_min              = "BACKSPACE";
  constexpr const char* zoom_out              = "E";
  constexpr const char* zoom_reset            = "=";
  constexpr const char* zoom_preset1          = "F1";
  constexpr const char* zoom_preset2          = "F2";
  constexpr const char* zoom_preset3          = "F3";
  constexpr const char* zoom_preset4          = "F4";
  constexpr const char* zoom_preset5          = "F5";
  constexpr const char* move_up               = "W|UP";
  constexpr const char* move_down             = "S|DOWN";
  constexpr const char* move_left             = "A|LEFT";
  constexpr const char* move_right            = "D|RIGHT";
} // namespace Shortcuts

namespace Sync
{
  constexpr const char* stfcdata_url       = "https://sync.stfcdata.de";
  constexpr const char* spocksclub_url     = "https://spocks.club/sync/ingress/";
  constexpr const char* nextspocksclub_url = "https://next.spocks.club/sync/ingress/";

  constexpr bool        away_assignments   = true;
  constexpr bool        battlelogs         = false;
  constexpr bool        buffs              = true;
  constexpr bool        buildings          = true;
  constexpr bool        fleets             = false; // opt-in: live fleet state from fleet_watch
  constexpr bool        haven              = true;
  constexpr bool        inventory          = true;
  constexpr bool        jobs               = true;
  constexpr bool        missions           = true;
  constexpr bool        officers           = true;
  constexpr bool        profile            = false;
  constexpr const char* proxy              = "";
  constexpr bool        research           = true;
  constexpr bool        resources          = true;
  constexpr bool        ships              = true;
  constexpr bool        slots              = true;
  constexpr bool        store_offers       = false;
  constexpr bool        tech               = true;
  constexpr bool        traits             = true;
  constexpr const char* token              = "";
  constexpr const char* url                = "";
  constexpr bool        debug              = false;
  constexpr bool        logging            = false;
  constexpr bool        verify_ssl         = true;
  constexpr auto        resolver_cache_ttl = 300;
} // namespace Sync

namespace UI
{
  constexpr bool        always_skip_reveal_sequence        = true;
  constexpr bool        arrow_keys_to_select_ship          = true;
  constexpr bool        dock_syncmanagement                = true;
  constexpr bool        auto_confirm_discovery             = true;
  constexpr bool        auto_confirm_ft_upgrade            = false;
  constexpr bool        forbidden_tech_ship_name           = true;
  constexpr bool        auto_confirm_reveal_ability        = false;
  constexpr bool        auto_confirm_alliance_contribution = false;
  constexpr bool        auto_open_bulk_claim_flyout        = false;
  constexpr const char* daily_bulk_claim_factions          = "federation,klingon,romulan";
  constexpr bool        daily_bulk_claim_toggle_default_on = false;
  constexpr bool        only_show_first_popup              = false;
  constexpr bool        disable_escape_exit                = true;
  // Maximum gap between Escape presses that opens the exit prompt.
  // 0 disables double-tap and preserves the existing blocked behavior.
  constexpr auto        disable_escape_exit_timer   = 0;
  constexpr bool        disable_first_popup         = false;
  constexpr bool        disable_galaxy_chat         = false;
  constexpr bool        disable_move_keys           = false;
  constexpr bool        disable_preview_locate      = false;
  constexpr bool        disable_preview_recall      = false;
  constexpr bool        disable_toast_banners       = false;
  constexpr bool        disable_veil_chat           = false;
  constexpr bool        show_player_title           = true;
  constexpr bool        disable_exchange_all        = false;
  constexpr bool        double_click_to_assign_ship = false;
  constexpr bool        focus_search                = true;
  constexpr bool        format_cargo_values         = true;
  constexpr bool        officer_sort                = true;
  constexpr bool        reverse_haven_history       = true;
  constexpr const char* disabled_banner_types       = "";
  constexpr const char* hud_field_training          = "auto";
  constexpr const char* hud_missions                = "auto";
  constexpr const char* hud_outposts                = "auto";
  constexpr const char* hud_q_trials                = "auto";
  constexpr const char* auto_confirm_instant_warp   = "none";
  constexpr const char* instant_warp_auto_jump      = "";
  constexpr const char* instant_warp_auto_warp      = "";
  constexpr const char* instant_warp_always_ask     = "";
  constexpr const char* pinned_ships                = "";
  constexpr const char* notify_banner_types         = "";
  constexpr auto        extend_chest_purchase_max   = 160;
  constexpr auto        extend_donation_max         = 80;
  constexpr bool        extend_donation_slider      = true;
  constexpr bool        show_armada_cargo           = true;
  constexpr bool        show_cargo_default          = true;
  constexpr bool        show_hostile_cargo          = true;
  constexpr bool        show_player_cargo           = true;
  constexpr bool        show_station_cargo          = true;
  constexpr bool        instant_cargo_counter       = true;
  constexpr int         cargo_significant_decimals  = 2; // decimal places for abbreviated cargo values (e.g. 1.25M)
} // namespace UI

} // namespace DefaultConfig

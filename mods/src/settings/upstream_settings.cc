#include "upstream_settings.h"

#include "config.h"
#include "defaultconfig.h"
#include "file.h"
#include "forbidden_tech.h"
#include "mod_pages.h"
#include "patches/mission_hud.h"
#include "patches/notification_audio.h"
#include "patches/parts/action_queue.h"
#include "persisted_setting.h"
#include "text_entry.h"
#include "warp_mode.h"

#include <charconv>
#include <cmath>
#include <filesystem>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <type_traits>

void RefreshFleetLabelControls();
bool FleetLabelControlsAvailable();

namespace mod_settings
{
namespace
{
  constexpr auto root = "community_mod.settings";
  using Timing        = ApplyTiming;
  using Available     = std::function<bool()>;
  std::vector<std::shared_ptr<void>>                        adapters;
  std::map<std::string, std::function<bool()>, std::less<>> currentSavers;

  template <typename T, typename... Args> std::shared_ptr<T> Keep(Args&&... args)
  {
    auto value = std::make_shared<T>(std::forward<Args>(args)...);
    adapters.push_back(value);
    return value;
  }

  Available Hook(bool Config::* member)
  {
    return [member] { return Config::Get().*member; };
  }

  std::string Id(const char* section, const char* key)
  { return std::string("community_mod.") + section + "." + key; }

  template <typename T> std::string Format(const T& value)
  {
    if constexpr (std::is_same_v<T, std::string>)
      return value;
    else {
      char       text[128];
      const auto result = std::to_chars(text, text + sizeof(text), value);
      return result.ec == std::errc{} ? std::string(text, result.ptr) : std::string{};
    }
  }

  std::string_view Trim(std::string_view text)
  {
    constexpr auto whitespace = " \t\r\n";
    const auto     first      = text.find_first_not_of(whitespace);
    return first == text.npos ? std::string_view{} : text.substr(first, text.find_last_not_of(whitespace) - first + 1);
  }

  template <typename T> std::optional<T> ParseNumber(std::string_view text, T minimum, T maximum)
  {
    text = Trim(text);
    if (text.empty())
      return {};
    T value{};
    if constexpr (std::is_floating_point_v<T>) {
      // Apple's floating from_chars requires macOS 26. Use the classic locale
      // on every platform while retaining full-token and range validation.
      if (text.front() == '+' || text.find_first_not_of("0123456789.eE+-") != text.npos)
        return {};
      std::istringstream input{std::string(text)};
      input.imbue(std::locale::classic());
      input >> std::noskipws >> value;
      if (!input || input.peek() != std::char_traits<char>::eof())
        return {};
      if (!std::isfinite(value))
        return {};
      // Some stream libraries silently round underflow to zero. An explicit
      // zero significand is valid; a nonzero value lost to underflow is not.
      if (value == 0 && text.substr(0, text.find_first_of("eE")).find_first_not_of("-0.") != text.npos)
        return {};
    } else {
      const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return {};
    }
    if (value < minimum || value > maximum)
      return {};
    return value;
  }

  template <typename T, typename Read, typename Write, typename Serializer>
  ValueDefinition<T> MakeDefinition(const char* section, const char* key, const char* label, T defaultValue, Read read,
                                    Write write, Available available, Timing timing, const char* help, const char* unit,
                                    Serializer serializer)
  {
    auto               requested = std::make_shared<T>(read());
    ValueDefinition<T> definition{Id(section, key), label,
                                  [read, requested, available, timing] {
                                    return available() ? ValueReadResult<T>::Known(
                                                             timing == Timing::Restart ? *requested : read(), 1)
                                                       : ValueReadResult<T>{};
                                  },
                                  [write, requested, available, timing](T value, std::uint64_t generation) {
                                    if (generation != 1 || !available())
                                      return ApplyResult::Rejected;
                                    if (timing == Timing::Restart)
                                      *requested = std::move(value);
                                    else if (!write(value))
                                      return ApplyResult::Rejected;
                                    return ApplyResult::Applied;
                                  }};
    if (timing == Timing::Restart)
      definition.active_value = [read]() -> std::optional<T> { return read(); };
    auto result = PersistedDefinition(
        std::move(definition), {section, key, help, unit, timing}, std::move(defaultValue), std::move(serializer),
        std::is_floating_point_v<T> ? std::chrono::milliseconds(250) : std::chrono::milliseconds{});
    // A native action already admitted the mutation. Its save does not depend
    // on whether the corresponding editor/sorting feature is available.
    currentSavers.insert_or_assign(result.metadata.TomlKey(), [read, persist = result.persist] {
      if (!persist)
        return false;
      persist(read());
      return true;
    });
    return result;
  }

  template <typename T, typename Read, typename Write>
  ValueDefinition<T> MakeDefinition(const char* section, const char* key, const char* label, T defaultValue, Read read,
                                    Write write, Available available, Timing timing, const char* help,
                                    const char* unit = "")
  {
    return MakeDefinition<T>(section, key, label, defaultValue, read, write, std::move(available), timing, help, unit,
                             [](const T& value) -> config_edit::Value {
                               if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
                                 return static_cast<std::int64_t>(value);
                               else if constexpr (std::is_floating_point_v<T>)
                                 return static_cast<double>(value);
                               else
                                 return value;
                             });
  }

  void Boolean(PageCatalog& catalog, const char* page, const char* section, const char* key, const char* label,
               bool Config::* member, bool defaultValue, Available available, Timing timing, const char* help,
               std::function<void()> onChanged = {})
  {
    auto setting = Keep<BooleanSetting>(MakeDefinition<bool>(
        section, key, label, defaultValue, [member] { return Config::Get().*member; },
        [member, onChanged = std::move(onChanged)](bool value) {
          const bool changed    = Config::Get().*member != value;
          Config::Get().*member = value;
          if (changed && onChanged)
            onChanged();
          return true;
        },
        std::move(available), timing, help));
    catalog.AddBoolean(page, *setting);
  }

  // An exact editor uses its own canonical snapshot rather than the convenience
  // slider's range-limited reader. Existing custom values remain readable and can
  // be edited without first moving a slider or normalizing the TOML file.
  template <typename T, typename Parse>
  void ExactEntry(PageCatalog& catalog, const char* page, const std::shared_ptr<ValueSetting<T>>& setting, Parse parse)
  {
    auto action           = Keep<ActionSetting>();
    action->identity      = setting->id() + ".exact";
    action->label         = setting->label();
    action->metadata      = &setting->metadata();
    action->reset         = [setting] { setting->Reset(setting->Observe()); };
    action->retry         = [setting] { setting->RetrySave(setting->Observe()); };
    action->prepare_reset = [setting] { return PrepareReset(*setting); };
    action->status        = [setting] { return setting->status_text(); };
    action->save_status   = [setting] { return setting->save_status(); };
    action->default_label = [setting] {
      const auto value = setting->default_value();
      return value ? Format(*value) : std::string{};
    };
    action->read = [setting](std::size_t) {
      const auto state = setting->Observe().state;
      auto       value = state.known() ? Format(*state.value) : std::string("Unavailable");
      if (value.empty())
        value = "(empty)";
      const auto status = setting->status_text();
      if (!status.empty())
        value += "\n" + status;
      return ActionSetting::Presentation{setting->label(), "Edit", std::move(value), state.known()};
    };
    action->invoke = [setting, parse](std::size_t) {
      const auto observed = setting->Observe();
      if (!observed.state.known())
        return;
      auto help = setting->metadata().description;
      if (!setting->metadata().unit.empty())
        help += "\nUnits: " + setting->metadata().unit;
      if (const auto value = setting->default_value()) {
        const auto text = Format(*value);
        help += "\nDefault: " + (text.empty() ? std::string("(empty)") : text);
      }
      OpenTextEntry({.title = setting->label(),
                     .value = Format(*observed.state.value),
                     .help  = std::move(help),
                     .validate =
                         [parse](std::string_view text) {
                           return parse(text) ? std::string{} : std::string("Enter a valid value.");
                         },
                     .commit =
                         [setting, observed, parse](std::string_view text) {
                           const auto value = parse(text);
                           if (!value)
                             return false;
                           const auto result = setting->SetFromUser(*value, observed).outcome;
                           return result == Outcome::AppliedVerified || result == Outcome::Unchanged;
                         }});
    };
    catalog.AddAction(page, *action);
  }

  template <typename T>
  void Number(PageCatalog& catalog, const char* page, const char* section, const char* key, const char* label,
              T Config::* member, T defaultValue, Available available, Timing timing, const char* help,
              const char* unit, T minimum, T maximum, float sliderMaximum = 0, float step = 1)
  {
    auto definition = MakeDefinition<T>(
        section, key, label, defaultValue, [member] { return Config::Get().*member; },
        [member, minimum, maximum](T value) {
          if (value < minimum || value > maximum)
            return false;
          if constexpr (std::is_floating_point_v<T>)
            if (!std::isfinite(value))
              return false;
          const auto before     = Config::Get().*member;
          Config::Get().*member = value;
          if constexpr (std::is_same_v<T, float>)
            if (member == &Config::ui_scale_ship)
              ApplyUiShipScaleToLoadedShips(before, value);
          return true;
        },
        available, timing, help, unit);
    if constexpr (std::is_floating_point_v<T>) {
      if (minimum == std::numeric_limits<T>::min())
        definition.metadata.description += "\nMust be greater than zero.";
      else
        definition.metadata.description += "\nMinimum: " + Format(minimum) + ".";
    } else {
      definition.metadata.description += "\nWhole numbers only. Minimum: " + Format(minimum) + ".";
    }
    if (maximum != std::numeric_limits<T>::max())
      definition.metadata.description += " Maximum: " + Format(maximum) + ".";
    if (sliderMaximum > static_cast<float>(minimum))
      definition.metadata.description += "\nThe slider is a convenience range; Edit also accepts valid custom values.";
    auto exact = Keep<ValueSetting<T>>(definition);
    ExactEntry(catalog, page, exact,
               [minimum, maximum](std::string_view text) { return ParseNumber<T>(text, minimum, maximum); });
    if constexpr (std::is_same_v<T, float>) {
      if (sliderMaximum > static_cast<float>(minimum)) {
        auto slider = Keep<SliderSetting>(std::move(definition), static_cast<float>(minimum), sliderMaximum, step,
                                          std::move(available), SliderLabel::Value, 2);
        catalog.AddSlider(page, *slider);
      }
    }
  }

  template <typename Read, typename Write>
  void Choice(PageCatalog& catalog, const char* page, const char* section, const char* key, const char* label,
              std::vector<std::string> labels, std::vector<std::string> values, int defaultValue, Read read,
              Write write, Available available, Timing timing, const char* help)
  {
    auto setting =
        Keep<ChoiceSetting>(MakeDefinition<int>(
                                section, key, label, defaultValue, read,
                                [write, count = values.size()](int value) {
                                  return value >= 0 && static_cast<std::size_t>(value) < count && write(value);
                                },
                                std::move(available), timing, help, "",
                                [values](const int& value) -> config_edit::Value { return values.at(value); }),
                            std::move(labels));
    catalog.AddChoice(page, *setting);
  }

  void Information(PageCatalog& catalog, const char* page, const char* id, const char* label,
                   std::function<std::string()> text)
  {
    auto action      = Keep<ActionSetting>();
    action->identity = id;
    action->label    = label;
    action->read     = [label, text = std::move(text)](std::size_t) {
      return ActionSetting::Presentation{label, "", text(), false};
    };
    action->invoke = [](std::size_t) {};
    catalog.AddAction(page, *action);
  }

  std::string Join(const std::vector<std::string>& names)
  {
    std::string text;
    for (const auto& name : names) {
      if (!text.empty())
        text += ", ";
      text += name;
    }
    return text;
  }

  int ChoiceDefault(std::string_view name, std::initializer_list<std::string_view> values)
  {
    int index = 0;
    for (const auto value : values) {
      if (value == name)
        return index;
      ++index;
    }
    throw std::invalid_argument("unknown default choice");
  }

  void Lists(PageCatalog& catalog)
  {
    constexpr auto page      = "community_mod.travel.filters";
    const auto     available = Hook(&Config::installInstantWarpConfirmationHooks);
    struct Filter {
      const char*              key;
      const char*              label;
      std::vector<std::string> Config::* names;
      bool Config::* all;
      const char*    defaultValue;
    };
    constexpr Filter filters[]{
        {"instant_warp_auto_jump", "Always jump with these ships", &Config::instant_warp_auto_jump,
         &Config::instant_warp_auto_jump_all, DefaultConfig::UI::instant_warp_auto_jump},
        {"instant_warp_auto_warp", "Always warp with these ships", &Config::instant_warp_auto_warp,
         &Config::instant_warp_auto_warp_all, DefaultConfig::UI::instant_warp_auto_warp},
        {"instant_warp_always_ask", "Always ask for these ships", &Config::instant_warp_always_ask,
         &Config::instant_warp_always_ask_all, DefaultConfig::UI::instant_warp_always_ask},
        {"pinned_ships", "Pinned fleet dock ships", &Config::pinned_ships, nullptr, DefaultConfig::UI::pinned_ships}};
    for (const auto& filter : filters) {
      auto read = [filter] {
        return filter.all && Config::Get().*filter.all ? std::string("*") : Join(Config::Get().*filter.names);
      };
      auto parse = [filter](std::string_view text) -> std::optional<std::string> {
        std::vector<std::string> names;
        bool                     all = false;
        if (!ParseShipFilter(text, names, all) || (!filter.all && all))
          return {};
        return all ? std::string("*") : Join(names);
      };
      auto setting = Keep<ValueSetting<std::string>>(MakeDefinition<std::string>(
          "ui", filter.key, filter.label, filter.defaultValue, read,
          [filter](const std::string& text) {
            std::vector<std::string> names;
            bool                     all = false;
            if (!ParseShipFilter(text, names, all) || (!filter.all && all))
              return false;
            Config::Get().*filter.names = std::move(names);
            if (filter.all)
              Config::Get().*filter.all = all;
            return true;
          },
          filter.all ? available : Hook(&Config::installPinnedShipSortHooks), Timing::ReopenView,
          "Comma-separated ship names. Warp filters accept * for every ship. Pins match idle ships by name; reopen "
          "fleet management after changing them."));
      ExactEntry(catalog, page, setting, parse);
    }
    auto factions = Keep<ValueSetting<std::string>>(MakeDefinition<std::string>(
        "ui", "daily_bulk_claim_factions", "Daily bulk-claim factions", DefaultConfig::UI::daily_bulk_claim_factions,
        [] { return Join(Config::Get().daily_bulk_claim_factions); },
        [](const std::string& text) {
          std::vector<std::string> values;
          if (!ParseFactionFilter(text, values))
            return false;
          Config::Get().daily_bulk_claim_factions = std::move(values);
          return true;
        },
        Hook(&Config::installDailyFactionBulkClaimHooks), Timing::ReopenView,
        "Comma-separated faction names; reopen the daily faction claim screen after changing the list."));
    ExactEntry(catalog, "community_mod.claims", factions, [](std::string_view text) -> std::optional<std::string> {
      std::vector<std::string> values;
      return ParseFactionFilter(text, values) ? std::optional{Join(values)} : std::nullopt;
    });
    for (const bool notify : {false, true}) {
#if !_WIN32
      if (notify)
        continue; // Native notification filtering consumes this list only on Windows.
#endif
      auto setting = Keep<ValueSetting<std::string>>(MakeDefinition<std::string>(
          "ui", notify ? "notify_banner_types" : "disabled_banner_types",
          notify ? "Banners allowed to notify" : "Hidden banner types",
          notify ? DefaultConfig::UI::notify_banner_types : DefaultConfig::UI::disabled_banner_types,
          [notify] {
            return FormatBannerFilter(notify ? Config::Get().notify_banner_types : Config::Get().disabled_banner_types);
          },
          [notify](const std::string& text) {
            std::vector<int> values;
            if (!ParseBannerFilter(text, values))
              return false;
            (notify ? Config::Get().notify_banner_types : Config::Get().disabled_banner_types) = std::move(values);
            return true;
          },
          Hook(&Config::installToastBannerHooks), Timing::Live,
          "Comma-separated banner names from the English example configuration. Unknown names are rejected."));
      ExactEntry(catalog, "community_mod.notifications", setting,
                 [](std::string_view text) -> std::optional<std::string> {
                   std::vector<int> values;
                   return ParseBannerFilter(text, values) ? std::optional{FormatBannerFilter(values)} : std::nullopt;
                 });
    }
  }
} // namespace

bool PersistCurrentSetting(std::string_view section, std::string_view key)
{
  // Existing native pin/shortcut actions still persist when the settings UI's
  // emergency hook switch is disabled. Call only from the game UI thread.
  try {
    if (currentSavers.empty())
      RegisterUpstreamSettings(ModPages());
    const auto found = currentSavers.find(std::string(section) + "." + std::string(key));
    return found != currentSavers.end() && found->second();
  } catch (...) {
    return false;
  }
}

void RegisterUpstreamSettings(PageCatalog& catalog)
{
  if (!adapters.empty())
    return;
  constexpr float positive    = std::numeric_limits<float>::min();
  constexpr float largest     = std::numeric_limits<float>::max();
  constexpr int   integer     = std::numeric_limits<int>::max();
  namespace C                 = DefaultConfig::Control;
  namespace G                 = DefaultConfig::Graphics;
  namespace U                 = DefaultConfig::UI;
  namespace B                 = DefaultConfig::Buffs;
  const auto      hotkeys     = Hook(&Config::installHotkeyHooks);
  const auto      zoom        = Hook(&Config::installZoomHooks);
  const auto      pan         = Hook(&Config::installPanHooks);
  const Available controls    = [hotkeys, zoom, pan] { return hotkeys() || zoom() || pan(); };
  const Available extended    = [hotkeys, zoom] { return hotkeys() || zoom(); };
  const Available protections = [] {
    const auto& config  = Config::Get();
    bool        enabled = config.installQueueAddressGuardHooks;
#if defined(_WIN32) && defined(_M_X64)
    enabled = enabled || config.installActionQueueRecoveryHooks || config.installThinQueueProtectionHooks;
#endif
    return enabled;
  };
  const Available queue = [hotkeys, protections] {
    return hotkeys() || protections() || Config::Get().installTestPatches;
  };
  const Available fleetLabels = [zoom] { return zoom() && FleetLabelControlsAvailable(); };
  const auto      scale       = Hook(&Config::installUiScaleHooks);
  const auto      misc        = Hook(&Config::installMiscPatches);
  const auto      toast       = Hook(&Config::installToastBannerHooks);
  for (const auto& page :
       std::initializer_list<std::pair<const char*, const char*>>{{"controls", "Controls"},
                                                                  {"camera", "Camera"},
                                                                  {"interface", "Interface & Scale"},
                                                                  {"travel", "Map & Travel"},
                                                                  {"previews", "Previews & Cargo"},
                                                                  {"claims", "Claims & Confirmations"},
                                                                  {"officers", "Officers & Ships"},
                                                                  {"chat", "Chat"},
                                                                  {"notifications", "Notifications"},
                                                                  {"hud", "HUD Buttons"},
                                                                  {"labels", "Fleet Labels"},
                                                                  {"loading", "Loading Screens"},
                                                                  {"advanced", "Advanced & Configuration"}})
    catalog.AddPage(std::string("community_mod.") + page.first, page.second, root);
  catalog.AddPage("community_mod.travel.filters", "Ship Filters & Pins", "community_mod.travel");

#define BOOL(page, section, key, label, member, def, hook, timing, help)                                               \
  Boolean(catalog, "community_mod." page, section, key, label, &Config::member, def, hook, Timing::timing, help)
#define NUM(page, section, key, label, member, def, hook, timing, help, unit, low, high, slider, step)                 \
  Number(catalog, "community_mod." page, section, key, label, &Config::member, decltype(Config::member)(def), hook,    \
         Timing::timing, help, unit, decltype(Config::member)(low), decltype(Config::member)(high), slider, step)

  BOOL("controls", "control", "hotkeys_enabled", "Enable mod shortcuts", hotkeys_enabled, C::hotkeys_enabled, controls,
       Live, "Use the mod's configured keyboard and mouse shortcuts.");
  BOOL("controls", "control", "hotkeys_extended", "Extended shortcut actions", hotkeys_extended, C::hotkeys_extended,
       extended, Restart,
       "Enable the extended set of mod shortcut actions. Restart to change their startup availability.");
  BOOL("controls", "control", "use_scopely_hotkeys", "Use native game shortcuts", use_scopely_hotkeys,
       C::use_scopely_hotkeys, controls, Restart,
       "The game uses its own shortcuts instead of the mod's shortcut bindings.");
  Boolean(catalog, "community_mod.controls", "control", "queue_enabled", "Enable action queue", &Config::queue_enabled,
          C::queue_enabled, queue, Timing::Live, "Allow queued actions from the mod's queue shortcut.",
          ResetQueueRecoveryTracking);
  Boolean(catalog, "community_mod.controls", "control", "kirshara_fixes", "Kirshara queue protections",
          &Config::kirshara_fixes, C::kirshara_fixes, protections, Timing::Live,
          "Protect and recover queued actions when targets or addresses change.", ResetQueueRecoveryTracking);
  BOOL("controls", "control", "primary_requires_visible_target", "Require visible primary target",
       primary_requires_visible_target, C::primary_requires_visible_target, hotkeys, Live,
       "Require a visible selected target before issuing the primary-action shortcut.");
  NUM("controls", "control", "select_timer", "Ship selection interval", select_timer, C::select_timer, hotkeys, Live,
      "Maximum interval used by repeated ship-selection shortcuts.", "ms", 0, integer, 0, 1);

  NUM("camera", "graphics", "keyboard_zoom_speed", "Keyboard zoom speed", keyboard_zoom_speed, G::keyboard_zoom_speed,
      zoom, Live, "Speed of keyboard zoom. Use Edit for a precise or custom value.", "distance/s", 0, largest, 1000,
      25);
  NUM("camera", "graphics", "zoom", "System zoom-out distance", zoom, G::zoom, zoom, Live,
      "Maximum system-camera zoom-out distance.", "distance", positive, largest, 10000, 250);
  NUM("camera", "graphics", "haven_zoom", "Haven zoom-out distance", haven_zoom, G::haven_zoom,
      Hook(&Config::installHavenZoomHooks), Live, "Maximum Haven camera distance. Zero uses the game's native limit.",
      "distance", 0, largest, 6000, 250);
  NUM("camera", "graphics", "default_system_zoom", "Default system zoom", default_system_zoom, G::default_system_zoom,
      zoom, Live, "Camera distance used by reset/default zoom.", "distance", 0, largest, 6000, 50);
  NUM("camera", "graphics", "system_pan_momentum_falloff", "Pan glide", system_pan_momentum_falloff,
      G::system_pan_momentum_falloff, Hook(&Config::installPanHooks), Live,
      "Retained motion per pan update. Higher values glide longer; values below 1 eventually stop.", "ratio", 0,
      .999999f, .99f, .01f);
  BOOL("camera", "graphics", "use_presets_as_default", "Use last preset as default zoom", use_presets_as_default,
       G::use_presets_as_default, zoom, Live, "Selecting a zoom preset also updates the session's default distance.");
  NUM("camera", "graphics", "system_zoom_preset_1", "Zoom preset 1", system_zoom_preset_1, G::system_zoom_preset_1,
      zoom, Live, "Distance for zoom preset 1.", "distance", 0, largest, 6000, 50);
  NUM("camera", "graphics", "system_zoom_preset_2", "Zoom preset 2", system_zoom_preset_2, G::system_zoom_preset_2,
      zoom, Live, "Distance for zoom preset 2.", "distance", 0, largest, 6000, 50);
  NUM("camera", "graphics", "system_zoom_preset_3", "Zoom preset 3", system_zoom_preset_3, G::system_zoom_preset_3,
      zoom, Live, "Distance for zoom preset 3.", "distance", 0, largest, 6000, 50);
  NUM("camera", "graphics", "system_zoom_preset_4", "Zoom preset 4", system_zoom_preset_4, G::system_zoom_preset_4,
      zoom, Live, "Distance for zoom preset 4.", "distance", 0, largest, 6000, 50);
  NUM("camera", "graphics", "system_zoom_preset_5", "Zoom preset 5", system_zoom_preset_5, G::system_zoom_preset_5,
      zoom, Live, "Distance for zoom preset 5.", "distance", 0, largest, 6000, 50);

  NUM("interface", "graphics", "ui_scale", "Interface scale", ui_scale, G::ui_scale, scale, Live,
      "Scale multiplier for the game interface. Zero uses native scaling.", "multiplier", 0, largest, 2, .05f);
  NUM("interface", "graphics", "ui_scale_step", "Scale shortcut step", ui_scale_step, G::ui_scale_step, scale, Live,
      "Amount added or removed by interface scale shortcuts.", "multiplier", positive, largest, .5f, .01f);
  NUM("interface", "graphics", "ui_scale_ship", "Ship scale", ui_scale_ship, G::ui_scale_ship, scale, Live,
      "Multiplier for ship models, including ships already loaded in the current view.", "multiplier", positive,
      largest, 3, .05f);
  NUM("interface", "graphics", "ui_scale_viewer", "Viewer scale", ui_scale_viewer, G::ui_scale_viewer, scale,
      ReopenView, "Scale multiplier for ship and officer viewers. Reopen the viewer to refresh it.", "multiplier",
      positive, largest, 3, .05f);
  NUM("interface", "graphics", "fr_scale", "Planet backdrop scale", fr_scale, G::fr_scale, zoom, Restart,
      "Multiplier for the system planet backdrop. Restart avoids rescaling an already modified transform.",
      "multiplier", positive, largest, 4, .1f);
#if _WIN32
  BOOL("interface", "graphics", "allow_cursor", "Allow cursor changes", allow_cursor, G::allow_cursor,
       ([] { return Config::Get().installUiScaleHooks || Config::Get().installTestPatches; }), Live,
       "Allow the game to update the mouse cursor.");
  BOOL("interface", "graphics", "free_resize", "Free window resizing", free_resize, G::free_resize,
       Hook(&Config::installFreeResizeHooks), Restart, "Allow arbitrary window sizes on Windows.");
  BOOL("interface", "graphics", "borderless_fullscreen", "Borderless fullscreen", borderless_fullscreen,
       G::borderless_fullscreen, Hook(&Config::installFreeResizeHooks), Restart,
       "Use a borderless fullscreen window on Windows.");
#endif
  BOOL("interface", "ui", "disable_escape_exit", "Block Escape exit prompt", disable_escape_exit,
       U::disable_escape_exit, hotkeys, Live,
       "Prevent Escape from opening the game's exit prompt except for an enabled double-tap.");
  NUM("interface", "ui", "disable_escape_exit_timer", "Escape double-tap interval", disable_escape_exit_timer,
      U::disable_escape_exit_timer, hotkeys, Live,
      "Zero disables double-tap. Otherwise two Escape presses within this interval may open the exit prompt.", "ms", 0,
      integer, 0, 1);
  BOOL("interface", "ui", "disable_move_keys", "Disable keyboard movement", disable_move_keys, U::disable_move_keys,
       Hook(&Config::installPanHooks), Live, "Disable map movement using configured movement shortcuts.");
  BOOL("interface", "ui", "only_show_first_popup", "Show only the first startup popup", only_show_first_popup,
       U::only_show_first_popup, Hook(&Config::installTempCrashFixes), Live,
       "Suppress additional startup interstitial popups in this session.");

  BOOL("travel", "graphics", "galactic_anomaly_timer", "Galactic Anomaly timer", galactic_anomaly_timer,
       G::galactic_anomaly_timer, Hook(&Config::installGalacticAnomalyTimerHooks), Live,
       "Show the remaining lifetime on supported Galactic Anomalies.");
  catalog.AddChoice("community_mod.travel", WarpModeSetting());

  BOOL("previews", "ui", "disable_preview_locate", "Block Locate with an open preview", disable_preview_locate,
       U::disable_preview_locate, hotkeys, Live, "Block the Locate shortcut while a preview panel is open.");
  BOOL("previews", "ui", "disable_preview_recall", "Block Recall with an open preview", disable_preview_recall,
       U::disable_preview_recall, hotkeys, Live, "Block the Recall shortcut while a preview panel is open.");
  BOOL("previews", "ui", "show_cargo_default", "Automatically open cargo", show_cargo_default, U::show_cargo_default,
       hotkeys, Live, "Automatically expand cargo when opening a supported target preview.");
  BOOL("previews", "ui", "show_player_cargo", "Player cargo", show_player_cargo, U::show_player_cargo, hotkeys, Live,
       "Automatically show cargo for player fleets.");
  BOOL("previews", "ui", "show_station_cargo", "Station cargo", show_station_cargo, U::show_station_cargo, hotkeys,
       Live, "Automatically show cargo for stations.");
  BOOL("previews", "ui", "show_hostile_cargo", "Hostile cargo", show_hostile_cargo, U::show_hostile_cargo, hotkeys,
       Live, "Automatically show cargo for hostiles.");
  BOOL("previews", "ui", "show_armada_cargo", "Armada cargo", show_armada_cargo, U::show_armada_cargo, hotkeys, Live,
       "Automatically show cargo for Armada targets.");
#if defined(_WIN32) && defined(_M_X64)
  BOOL("previews", "ui", "instant_cargo_counter", "Instant ship cargo counter", instant_cargo_counter,
       U::instant_cargo_counter, Hook(&Config::installInstantCargoCounterHooks), Live,
       "Remove the animation delay from supported ship cargo counters.");
#endif
  BOOL("previews", "ui", "format_cargo_values", "Format cargo values", format_cargo_values, U::format_cargo_values,
       Hook(&Config::installCargoFormatHooks), Restart,
       "Use the configured decimal precision for abbreviated cargo numbers.");
  NUM("previews", "ui", "cargo_significant_decimals", "Cargo decimal places", cargo_significant_decimals,
      U::cargo_significant_decimals, Hook(&Config::installCargoFormatHooks), ReopenView,
      "Decimal places used for abbreviated cargo amounts, from 0 through 6.", "places", 0, 6, 0, 1);

  BOOL("claims", "ui", "auto_open_bulk_claim_flyout", "Automatically open bulk claims", auto_open_bulk_claim_flyout,
       U::auto_open_bulk_claim_flyout, Hook(&Config::installGiftsBulkClaimHooks), Live,
       "Open the bulk-claim drawer when entering supported claim screens.");
  BOOL("claims", "ui", "daily_bulk_claim_toggle_default_on", "Daily bulk claims on by default",
       daily_bulk_claim_toggle_default_on, U::daily_bulk_claim_toggle_default_on,
       Hook(&Config::installDailyFactionBulkClaimHooks), Restart,
       "Enable the daily faction Claim All toggle when first initialized in a session.");
  BOOL("claims", "ui", "always_skip_reveal_sequence", "Skip reward reveal sequences", always_skip_reveal_sequence,
       U::always_skip_reveal_sequence, Hook(&Config::installTempCrashFixes), Live,
       "Skip the animated reward reveal and show the summary directly.");
  catalog.AddBoolean("community_mod.claims", ForbiddenTechConfirmationSetting());
  BOOL("claims", "ui", "auto_confirm_reveal_ability", "Automatically confirm Excelsior reveal",
       auto_confirm_reveal_ability, U::auto_confirm_reveal_ability, Hook(&Config::installRevealConfirmationHooks), Live,
       "Skip confirmation for the Excelsior's Decloaking Deflector ability.");
  BOOL("claims", "ui", "disable_exchange_all", "Hide artifact Exchange All", disable_exchange_all,
       U::disable_exchange_all, Hook(&Config::installArtifactExchangeHooks), ReopenView,
       "Hide Exchange All in the artifact popup. Reopen the popup after changing this.");
  NUM("claims", "ui", "extend_chest_purchase_max", "Maximum chest quantity", extend_chest_purchase_max,
      U::extend_chest_purchase_max, misc, Restart,
      "Zero disables the extension. The effective extension is capped at 160; higher native limits are preserved. "
      "Restart after changing this value.",
      "chests", 0, integer, 0, 1);
  BOOL("claims", "ui", "extend_donation_slider", "Extend donation slider", extend_donation_slider,
       U::extend_donation_slider, misc, ReopenView,
       "Use the extended donation quantity limit in supported donation dialogs.");
  NUM("claims", "ui", "extend_donation_max", "Maximum donation quantity", extend_donation_max, U::extend_donation_max,
      misc, ReopenView, "Extended donation quantity limit. Reopen the donation dialog to refresh it.", "donations", 1,
      integer, 0, 1);

  BOOL("officers", "ui", "double_click_to_assign_ship", "Double-click to assign ship", double_click_to_assign_ship,
       U::double_click_to_assign_ship, Hook(&Config::installDoubleClickAssignShipHooks), Live,
       "Assign a ship with a double-click in supported selection screens.");
  BOOL("officers", "ui", "officer_sort", "Below Deck Ability sorting", officer_sort, U::officer_sort,
       Hook(&Config::installOfficerSortHooks), Restart,
       "Add Below Deck Ability sorting to the officer roster and assignment screens.");
  BOOL("officers", "ui", "arrow_keys_to_select_ship", "Arrow keys select ships", arrow_keys_to_select_ship,
       U::arrow_keys_to_select_ship, hotkeys, Live,
       "Use configured left/right controls to change ship selection in supported screens.");
  BOOL("officers", "ui", "dock_syncmanagement", "Keep dock selection in sync", dock_syncmanagement,
       U::dock_syncmanagement, hotkeys, Live, "Keep dock and Manage Ships selections aligned.");
  BOOL("officers", "buffs", "use_out_of_dock_power", "Use out-of-dock power", use_out_of_dock_power,
       B::use_out_of_dock_power, Hook(&Config::installBuffFixHooks), Restart,
       "Use deployed-ship power calculations for supported power displays. Restart after changing this value.");
  BOOL("officers", "ui", "reverse_haven_history", "Newest Haven help first", reverse_haven_history,
       U::reverse_haven_history, Hook(&Config::installHavenHistoryHooks), ReopenView,
       "Reverse Haven help history into newest-first order when the list refreshes.");
  BOOL("interface", "ui", "focus_search", "Enable search-focus shortcut", focus_search, U::focus_search, hotkeys, Live,
       "Allow the search-focus shortcut in supported game screens.");

  BOOL("chat", "ui", "show_player_title", "Show player titles", show_player_title, U::show_player_title,
       Hook(&Config::installPlayerTitleDisplayHooks), ReopenView,
       "Show title rows on chat messages. Reopen chat to rebuild existing rows.");
  BOOL("chat", "ui", "disable_galaxy_chat", "Disable galaxy chat", disable_galaxy_chat, U::disable_galaxy_chat,
       Hook(&Config::installChatPatches), Restart, "Disable galaxy chat tabs and preview messages.");
  BOOL("chat", "ui", "disable_veil_chat", "Disable Veil chat", disable_veil_chat, U::disable_veil_chat,
       Hook(&Config::installChatPatches), Restart, "Disable Veil chat tabs and preview messages where available.");
#undef BOOL
#undef NUM

  struct Hud {
    const char* name;
    const char* label;
    const char* defaultValue;
  };
  for (const auto& hud : std::initializer_list<Hud>{{"q_trials", "Q's Trials", U::hud_q_trials},
                                                    {"field_training", "Field Training", U::hud_field_training},
                                                    {"outposts", "Outposts", U::hud_outposts},
                                                    {"missions", "Missions", U::hud_missions}}) {
    const std::string key  = std::string("hud_") + hud.name;
    const std::string name = hud.name;
    Choice(
        catalog, "community_mod.hud", "ui", key.c_str(), hud.label, {"Auto", "Always", "Never"},
        {"auto", "always", "never"}, ChoiceDefault(hud.defaultValue, {"auto", "always", "never"}),
        [name] { return static_cast<int>(Config::Get().MissionHudButtonVisibility(name)); },
        [name](int value) {
          if (!mission_hud::CanChange())
            return false;
          Config::Get().mission_hud_buttons[name] = static_cast<MissionHudVisibility>(value);
          mission_hud::Refresh();
          return true;
        },
        [] { return mission_hud::Available(); }, Timing::Live, "Choose when this mission HUD button is visible.");
  }
  for (const bool player : {true, false}) {
    auto profile = [player]() -> FleetLabelProfile& {
      return player ? Config::Get().zoom_label_player : Config::Get().zoom_label_non_player;
    };
    Choice(
        catalog, "community_mod.labels", "graphics",
        player ? "zoom_label_player_detail" : "zoom_label_non_player_detail",
        player ? "Player label detail" : "Non-player label detail", {"Native", "Expanded", "Compact", "Threshold"},
        {"native", "expanded", "compact", "threshold"},
        ChoiceDefault(player ? G::zoom_label_player_detail : G::zoom_label_non_player_detail,
                      {"native", "expanded", "compact", "threshold"}),
        [profile] { return static_cast<int>(profile().detail); },
        [profile](int value) {
          profile().detail = static_cast<FleetLabelDetail>(value);
          RefreshFleetLabelControls();
          return true;
        },
        fleetLabels, Timing::Live, "Choose the amount of information shown on system fleet labels.");
    auto definition = MakeDefinition<float>(
        "graphics", player ? "zoom_label_player_threshold" : "zoom_label_non_player_threshold",
        player ? "Player detail threshold" : "Non-player detail threshold",
        static_cast<float>(player ? G::zoom_label_player_threshold : G::zoom_label_non_player_threshold),
        [profile] { return profile().zoom_threshold; },
        [profile](float value) {
          if (!std::isfinite(value) || value < 0 || value > 1)
            return false;
          profile().zoom_threshold = value;
          RefreshFleetLabelControls();
          return true;
        },
        fleetLabels, Timing::Live,
        "Zoom threshold used by Threshold label detail. Enter a ratio from 0 through 1: zero stays compact; one stays "
        "expanded.",
        "ratio");
    auto exact = Keep<ValueSetting<float>>(definition);
    ExactEntry(catalog, "community_mod.labels", exact,
               [](std::string_view text) { return ParseNumber<float>(text, 0, 1); });
    auto slider = Keep<SliderSetting>(std::move(definition), 0.f, 1.f, .01f, [profile, fleetLabels] {
      return fleetLabels() && profile().detail == FleetLabelDetail::Threshold;
    });
    catalog.AddSlider("community_mod.labels", *slider);
  }
  const std::pair<const char*, NotificationSound Config::*> alerts[]{
      {"alert_victory", &Config::alert_victory},
      {"alert_defeat", &Config::alert_defeat},
      {"alert_armada_created", &Config::alert_armada_created},
      {"alert_armada_battle_won", &Config::alert_armada_battle_won},
      {"alert_armada_battle_lost", &Config::alert_armada_battle_lost}};
  const char* alertLabels[]{"Victory sound", "Defeat sound", "Armada created sound", "Armada victory sound",
                            "Armada defeat sound"};
  const char* alertDefaults[]{DefaultConfig::Audio::alert_victory, DefaultConfig::Audio::alert_defeat,
                              DefaultConfig::Audio::alert_armada_created, DefaultConfig::Audio::alert_armada_battle_won,
                              DefaultConfig::Audio::alert_armada_battle_lost};
  for (std::size_t i = 0; i < std::size(alerts); ++i) {
    const auto               member = alerts[i].second;
    std::vector<std::string> labels, values;
    for (int value = 0; value < static_cast<int>(NotificationSound::Count); ++value) {
      auto text = std::string(notification_sound_name(static_cast<NotificationSound>(value)));
      values.push_back(text);
      labels.push_back(value == 0 ? "Off" : text);
    }
    Choice(
        catalog, "community_mod.notifications", "audio", alerts[i].first, alertLabels[i], std::move(labels),
        std::move(values),
        static_cast<int>(notification_sound_from_name(alertDefaults[i]).value_or(NotificationSound::None)),
        [member] { return static_cast<int>(Config::Get().*member); },
        [member](int value) {
          Config::Get().*member = static_cast<NotificationSound>(value);
          return true;
        },
        toast, Timing::Live, "Choose the built-in sound for this notification.");
    auto preview      = Keep<ActionSetting>();
    preview->identity = Id("audio", alerts[i].first) + ".preview";
    preview->label    = alertLabels[i];
    preview->read     = [member, toast, label = alertLabels[i]](std::size_t) {
      return ActionSetting::Presentation{label, "Play", "",
                                         toast() && Config::Get().*member != NotificationSound::None};
    };
    preview->invoke = [member](std::size_t) { notification_audio_play(Config::Get().*member); };
    catalog.AddAction("community_mod.notifications", *preview);
  }

  const auto loading    = Hook(&Config::installLoadingScreenHooks);
  const auto transition = Hook(&Config::installTransitionScreenHooks);
  Boolean(catalog, "community_mod.loading", "graphics", "loader_enabled", "Custom loading background",
          &Config::loader_enabled, G::loader_enabled, loading, Timing::Restart,
          "Replace the login background with the configured image.");
  Boolean(catalog, "community_mod.loading", "graphics", "loader_transition", "Custom transition background",
          &Config::loader_transition, G::loader_transition, transition, Timing::Restart,
          "Use the configured background during supported screen transitions.");
  Boolean(catalog, "community_mod.loading", "graphics", "loader_transition_black", "Black transition background",
          &Config::loader_transition_black, G::loader_transition_black, transition, Timing::Restart,
          "Use black instead of the configured image during transitions.");
  Boolean(catalog, "community_mod.loading", "graphics", "loader_tip_enabled", "Loading tips",
          &Config::loader_tip_enabled, G::loader_tip_enabled, Hook(&Config::installLoadingTipHooks), Timing::Restart,
          "Show custom tips on supported loading screens.");
  Number(
      catalog, "community_mod.loading", "graphics", "loader_logo_scale", "Loading logo scale",
      &Config::loader_logo_scale, static_cast<float>(G::loader_logo_scale),
      [loading, transition] { return loading() || transition(); }, Timing::Restart,
      "Multiplier for the loading-screen logo.", "multiplier", positive, largest, 3, .1f);
  auto image = Keep<ValueSetting<std::string>>(MakeDefinition<std::string>(
      "graphics", "loader_image", "Loading image path", G::loader_image, [] { return Config::Get().loader_image; },
      [](const std::string& value) {
        Config::Get().loader_image = value;
        return true;
      },
      [loading, transition] { return loading() || transition(); }, Timing::Restart,
      "Path to an image readable by the game. Empty uses the embedded fallback. This does not move or copy the "
      "image."));
  ExactEntry(catalog, "community_mod.loading", image, [](std::string_view value) -> std::optional<std::string> {
    value = Trim(value);
    if (value.empty())
      return std::string{};
    std::error_code error;
    return std::filesystem::is_regular_file(std::filesystem::u8path(value), error) && !error
               ? std::optional{std::string(value)}
               : std::nullopt;
  });
  Lists(catalog);
  Information(catalog, "community_mod.advanced", "community_mod.configuration.path", "Configuration file", [] {
    const auto path = File::MakePath(File::Config());
    return std::string(reinterpret_cast<const char*>(path.data()), path.size());
  });
  Information(catalog, "community_mod.advanced", "community_mod.configuration.saving", "Saving settings", [] {
    return "Changes apply immediately unless marked for reopening or restart. Saves preserve unrelated TOML values; a "
           "conflict or failed save remains visible until resolved.";
  });
  Information(catalog, "community_mod.advanced", "community_mod.configuration.sync", "Data sync", [] {
    return "Sync targets, credentials, connection settings and sync diagnostics remain in TOML. Restart after editing "
           "them. Credentials are never displayed here.";
  });
  Information(catalog, "community_mod.advanced", "community_mod.configuration.audio", "Audio diagnostics", [] {
    return "Advanced audio event tracing and the arbitrary event-name mute list remain in TOML: audio.trace_events and "
           "audio.disabled_events.";
  });
  Information(catalog, "community_mod.advanced", "community_mod.configuration.patches", "Compatibility switches", [] {
    return "Emergency hook-install switches under [patches] require restart. Keep them enabled unless diagnosing "
           "compatibility with another mod.";
  });
  Information(catalog, "community_mod.advanced", "community_mod.configuration.endpoints", "Configuration sources", [] {
    return "Advanced configuration and asset endpoint overrides remain in TOML under [config]. Restart after editing "
           "them.";
  });
  Information(catalog, "community_mod.advanced", "community_mod.configuration.experimental",
              "Experimental shortcut options", [] {
                return "control.enable_experimental controls startup availability of experimental shortcuts. "
                       "Enable it in TOML and restart. graphics.system_pan_momentum, graphics.transition_time and "
                       "ui.disable_toast_banners are legacy inputs with no current gameplay consumer. "
                       "Pan glide uses system_pan_momentum_falloff; banner suppression uses disabled_banner_types.";
              });
#if !defined(_WIN32)
  Information(catalog, "community_mod.advanced", "community_mod.configuration.windows", "Windows window options", [] {
    return "graphics.free_resize, graphics.borderless_fullscreen, graphics.allow_cursor, ui.instant_cargo_counter "
           "and ui.notify_banner_types have Windows-only consumers and are not offered on this client.";
  });
#endif
}
} // namespace mod_settings

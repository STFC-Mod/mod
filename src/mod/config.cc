// Owns reading the settings file into Config: defaults, validation, renamed and retired keys, the runtime vars
// file, and reloading when the file changes.

#include "config.h"

#include "file.h"
#include "shortcuts.h"
#include "text.h"

#include "game/ui.h"

#include <spdlog/spdlog.h>
#include <toml++/toml.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <utility>

namespace
{
// --- Catalogs ---

template <typename E> struct Choice {
  std::string_view name;
  E                value;
};

// Names of ToastState values as written in disabled_banner_types and notify_banner_types.
constexpr Choice<ToastState> kBannerTypes[] = {
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

// For the enum settings, the first name listed for a value is the one written back; later ones are accepted
// synonyms. The empty name keeps a blank setting at the default without a warning.
constexpr Choice<MissionHudVisibility> kHudVisibility[] = {
    {"auto", MissionHudVisibility::kAuto},
    {"always", MissionHudVisibility::kAlways},
    {"never", MissionHudVisibility::kNever},
    {"", MissionHudVisibility::kAuto},
};

constexpr Choice<InstantWarpConfirmation> kInstantWarp[] = {
    {"none", InstantWarpConfirmation::kNone},  {"warp", InstantWarpConfirmation::kWarp},
    {"jump", InstantWarpConfirmation::kJump},  {"left", InstantWarpConfirmation::kWarp},
    {"right", InstantWarpConfirmation::kJump}, {"", InstantWarpConfirmation::kNone},
};

constexpr Choice<FleetLabelDetail> kFleetLabelDetail[] = {
    {"native", FleetLabelDetail::kNative},    {"expanded", FleetLabelDetail::kExpanded},
    {"compact", FleetLabelDetail::kCompact},  {"threshold", FleetLabelDetail::kThreshold},
    {"always", FleetLabelDetail::kExpanded},  {"never", FleetLabelDetail::kCompact},
    {"custom", FleetLabelDetail::kThreshold}, {"auto", FleetLabelDetail::kNative},
    {"none", FleetLabelDetail::kNative},      {"off", FleetLabelDetail::kNative},
    {"", FleetLabelDetail::kNative},
};

// In NotificationSound order, so the canonical names come first.
constexpr Choice<NotificationSound> kNotificationSounds[] = {
    {"none", NotificationSound::kNone},       {"default", NotificationSound::kDefault},
    {"info", NotificationSound::kInfo},       {"success", NotificationSound::kSuccess},
    {"warning", NotificationSound::kWarning}, {"alarm", NotificationSound::kAlarm},
    {"arrival", NotificationSound::kArrival}, {"soft", NotificationSound::kSoft},
    {"ping", NotificationSound::kPing},       {"repair", NotificationSound::kRepair},
    {"off", NotificationSound::kNone},        {"silent", NotificationSound::kNone},
    {"victory", NotificationSound::kSuccess}, {"warn", NotificationSound::kWarning},
    {"attack", NotificationSound::kAlarm},    {"arrive", NotificationSound::kArrival},
    {"quiet", NotificationSound::kSoft},      {"repaired", NotificationSound::kRepair},
};

constexpr std::string_view kKnownFactions[] = {"federation", "klingon", "romulan"};

// --- Reading ---

// The text of a setting as written, for messages about values that were rejected.
std::string Raw(toml::node_view<const toml::node> node)
{
  if (auto text = node.value<std::string>())
    return *text;
  std::ostringstream out;
  out << node;
  return out.str();
}

// Tokens are shown with their first 9 and last 12 characters, enough to tell two apart in a log.
std::string MaskToken(std::string token)
{
  if (token.size() > 21)
    std::ranges::replace_if(token.begin() + 9, token.end() - 12, [](char c) { return c != '-'; }, '*');
  return token;
}

// Reads settings from the user's file and records every value used in `effective`, which is written out as the
// runtime vars file so users can see what the mod actually used. A missing or rejected value falls back to the
// default, which is the member's current value; every rejection logs one warning.
class Reader
{
public:
  Reader(const toml::table& file, toml::table& effective, bool log)
      : file_(file)
      , effective_(effective)
      , log_(log)
  {
  }

  // The value as written, empty when the file does not set it.
  toml::node_view<const toml::node> Node(std::string_view section, std::string_view key) const
  { return file_[section][key]; }

  // The section of the vars file, created on first use.
  toml::table& EffectiveSection(std::string_view section)
  { return *effective_.emplace<toml::table>(section).first->second.as_table(); }

  // The configured value converted to T, or `fallback` when missing or of the wrong type. TOML integers read fine
  // into float settings; fractions into int settings do not.
  template <typename T> T Get(std::string_view section, std::string_view key, T fallback)
  {
    auto value = Convert(section, key, fallback);
    Record(section, key, value);
    return value;
  }

  template <typename T> void Read(std::string_view section, std::string_view key, T& value)
  { value = Get(section, key, value); }

  // Read, keeping the default when `valid` rejects the configured value.
  template <typename T, typename Valid>
  void ReadValid(std::string_view section, std::string_view key, T& value, Valid valid)
  {
    const auto fallback = value;
    Read(section, key, value);
    if (valid(value))
      return;
    Invalid(section, key, value, fallback);
    value = fallback;
    Record(section, key, value);
  }

  // A renamed setting: `key`, or `old_key` when only the old name is set. The vars file records `key`.
  template <typename T>
  void ReadRenamed(std::string_view section, std::string_view key, std::string_view old_key, T& value)
  {
    if (Node(section, key) || !Node(section, old_key))
      return Read(section, key, value);
    spdlog::warn("deprecated config value {}.{}; use {}.{}", section, old_key, section, key);
    value = Convert(section, old_key, value);
    Record(section, key, value);
  }

  // A case-insensitive choice from `choices`. The vars file records the canonical name.
  template <typename E, size_t N>
  void Choose(std::string_view section, std::string_view key, E& value, const Choice<E> (&choices)[N])
  {
    const auto fallback = CanonicalName(value, choices);
    const auto raw      = Get<std::string>(section, key, std::string(fallback));
    const auto found    = std::ranges::find(choices, AsciiStrToLower(StripAsciiWhitespace(raw)), &Choice<E>::name);
    if (found == std::end(choices))
      Invalid(section, key, raw, fallback);
    else
      value = found->value;
    Record(section, key, std::string(CanonicalName(value, choices)));
  }

  // A setting that no longer exists, at a dotted path. Says what replaced it when the file still sets it.
  void Retired(std::string_view path, std::string_view replacement)
  {
    if (file_.at_path(path))
      spdlog::warn("retired config value {}; {}", path, replacement);
  }

  // Sets the value the vars file shows. A later Record of the same key replaces it.
  template <typename T> void Record(std::string_view section, std::string_view key, const T& value)
  {
    EffectiveSection(section).insert_or_assign(key, value);
    if (log_)
      spdlog::debug("config {}.{} = {}", section, key, value);
  }

private:
  // The value converted to T, or `fallback` when missing or, after a warning, of the wrong type.
  template <typename T> T Convert(std::string_view section, std::string_view key, const T& fallback) const
  {
    const auto node = Node(section, key);
    if (auto read = node.template value<T>())
      return *read;
    if (node)
      Invalid(section, key, Raw(node), fallback);
    return fallback;
  }

  template <typename R, typename F>
  static void Invalid(std::string_view section, std::string_view key, const R& raw, const F& fallback)
  { spdlog::warn("invalid config value {}.{}: '{}'; using {}", section, key, raw, fallback); }

  template <typename E, size_t N> static std::string_view CanonicalName(E value, const Choice<E> (&choices)[N])
  { return std::ranges::find(choices, value, &Choice<E>::value)->name; }

  const toml::table& file_;
  toml::table&       effective_;
  bool               log_;
};

// "*" matches every ship; otherwise a comma list of ship names, each split into words by ShipNameWords.
ShipFilter ReadShipFilter(Reader& reader, std::string_view key)
{
  const auto value = reader.Get<std::string>("ui", key, "");
  if (StripAsciiWhitespace(value) == "*")
    return {.all = true};
  ShipFilter filter;
  for (const auto& token : StrSplit(value, ','))
    if (auto words = ShipNameWords(token); !words.empty())
      filter.names.push_back(std::move(words));
  return filter;
}

// A comma list of kKnownFactions, or "none", which wins over the others. Blank means all of them.
std::vector<std::string> ReadFactions(Reader& reader)
{
  const auto all   = kKnownFactions | std::views::join_with(',') | std::ranges::to<std::string>();
  auto       value = reader.Get<std::string>("ui", "daily_bulk_claim_factions", all);
  if (StripAsciiWhitespace(value).empty()) {
    value = all;
    // Replaces the blank that Get recorded, without a second debug line.
    reader.EffectiveSection("ui").insert_or_assign("daily_bulk_claim_factions", value);
  }

  std::vector<std::string> factions;
  bool                     none = false;
  for (const auto& token : StrSplit(value, ',')) {
    auto faction = AsciiStrToLower(token);
    if (faction == "none")
      none = true;
    else if (!std::ranges::contains(kKnownFactions, faction))
      spdlog::warn("invalid config value ui.daily_bulk_claim_factions: '{}'; ignoring it (expected {} or none)", token,
                   all);
    else if (!std::ranges::contains(factions, faction))
      factions.push_back(std::move(faction));
  }
  if (!none)
    return factions;
  if (!factions.empty())
    spdlog::warn("invalid config value ui.daily_bulk_claim_factions: '{}'; using none, which overrides the others",
                 value);
  return {"none"};
}

// A case-insensitive comma list of kBannerTypes names, or "All". The vars file gets the names that matched, in
// catalog order.
std::vector<int> ReadBannerTypes(Reader& reader, std::string_view key)
{
  auto wanted = StrSplit(reader.Get<std::string>("ui", key, ""), ',') | std::views::transform(AsciiStrToUpper)
                | std::ranges::to<std::vector>();
  // With "All", names after it are still reported as unknown.
  const bool all = std::erase(wanted, "ALL") > 0;

  std::vector<int> types;
  std::string      names;
  for (const auto& [name, state] : kBannerTypes) {
    if (!all && std::erase(wanted, AsciiStrToUpper(name)) == 0)
      continue;
    types.push_back(std::to_underlying(state));
    names += names.empty() ? "" : ", ";
    names += name;
  }
  for (const auto& unknown : wanted)
    spdlog::warn("invalid config value ui.{}: '{}'; ignoring it", key, unknown);

  reader.Record("ui", key, names);
  return types;
}

// [sync.targets.<name>] tables. Each target takes its proxy, verify_ssl and categories from [sync] unless it sets
// its own. Unlike [sync], a target's renamed category keys are read without a deprecation warning.
void ReadSyncTargets(Reader& reader, std::map<std::string, SyncTargetConfig>& sync_targets, const SyncConfig& defaults)
{
  // The built-in targets are listed in the vars file as examples; they only sync once given a token.
  auto& effective_targets =
      *reader.EffectiveSection("sync")
           .insert_or_assign(
               "targets",
               toml::table{
                   {"stfcdata", toml::table{{"token", ""}, {"url", "https://sync.stfcdata.de"}}},
                   {"nextspocksclub", toml::table{{"token", ""}, {"url", "https://next.spocks.club/sync/ingress/"}}},
                   {"spocksclub", toml::table{{"token", ""}, {"url", "https://spocks.club/sync/ingress/"}}},
               })
           .first->second.as_table();

  const auto* targets = reader.Node("sync", "targets").as_table();
  if (!targets)
    return;

  for (const auto& [name, node] : *targets) {
    const auto* values = node.as_table();
    if (!values)
      continue;

    const auto section = "sync.targets." + std::string(name.str());
    const auto url     = (*values)["url"].value<std::string>();
    const auto token   = (*values)["token"].value<std::string>();
    if (!url || !token) {
      spdlog::warn("invalid config value {}: url and token are required; skipping the target", section);
      continue;
    }
    // Blank values are the example file's placeholders for a target the user has not set up.
    if (url->empty() || token->empty())
      continue;

    SyncTargetConfig target{defaults, *url, *token};
    target.proxy      = (*values)["proxy"].value<std::string>().value_or(target.proxy);
    target.verify_ssl = (*values)["verify_ssl"].value<bool>().value_or(target.verify_ssl);

    toml::table effective{
        {"url", target.url}, {"token", target.token}, {"proxy", target.proxy}, {"verify_ssl", target.verify_ssl}};
    for (const auto& [key, member, old_name] : SyncOptions) {
      const auto read_as = values->contains(key) || old_name.empty() ? key : old_name;
      target.*member     = (*values)[read_as].value<bool>().value_or(target.*member);
      effective.insert(key, target.*member);
    }

    // TOML table keys are unique, so every target is new here.
    effective_targets.insert_or_assign(name.str(), std::move(effective));
    spdlog::debug("config {} = url {}, token {}, proxy '{}', verify_ssl {}", section, target.url,
                  MaskToken(target.token), target.proxy, target.verify_ssl);
    sync_targets.emplace(name.str(), std::move(target));
  }
}

// --- Files and activation ---

std::unique_ptr<Config> current_config;
// Detached sync workers can still read during static teardown, so the publication slot lives until process exit.
auto& sync_config = *new std::shared_ptr<const Config::Sync>{std::make_shared<const Config::Sync>()};

// The settings file, or nullopt after a warning that says why it could not be read and what happens `instead`.
std::optional<toml::table> ReadSettingsFile(std::string_view instead)
{
  try {
    // The toml++ library is built without its char8_t overloads, so the UTF-8 path goes in as chars.
    const auto path = File::Path(File::Config()).u8string();
    return toml::parse_file(std::string_view(reinterpret_cast<const char*>(path.data()), path.size()));
  } catch (const toml::parse_error& e) {
    spdlog::warn("could not parse {}: {}; {}", File::Config(), e.description(), instead);
  } catch (const std::exception& e) {
    spdlog::warn("could not read {}: {}; {}", File::Config(), e.what(), instead);
  }
  return std::nullopt;
}

// Heads the runtime vars file; the placeholders are File::DefaultConfig() and File::Config().
constexpr std::string_view kVarsHeader = R"(#######################################################################
#######################################################################
####                                                               ####
#### NOTE: This file is not the configuration file that is used    ####
####       by the STFC Community Mod.  It is provided to help      ####
####       see what configuration is being used by the runtime     ####
####       and any desired settings should be copied to the same   ####
####       section in: {}
####                                                               ####
####        Config in: {}
####                                                               ####
#######################################################################
#######################################################################

)";

void Save(const toml::table& table, const std::string& name, bool warning_header)
{
  std::ofstream file(File::Path(name, true));
  if (warning_header)
    file << std::format(kVarsHeader, File::DefaultConfig(), File::Config());
  file << table;
}

// Writes `effective` as the runtime vars file. At startup it also becomes the settings file when there is none, so
// a new user starts from a complete list of settings.
void SaveEffective(const toml::table& effective, bool startup)
{
  if (startup && !std::filesystem::exists(File::Path(File::Config()))) {
    spdlog::info("creating {} with the default settings", File::Config());
    Save(effective, File::Config(), false);
  }
  spdlog::info("writing the settings in effect to {}", File::Vars());
  Save(effective, File::Vars(), true);
}
} // namespace

std::string_view NotificationSoundName(NotificationSound sound)
{
  const auto found = std::ranges::find(kNotificationSounds, sound, &Choice<NotificationSound>::value);
  return found == std::end(kNotificationSounds) ? "none" : found->name;
}

std::unique_ptr<Config> Config::Parse(const toml::table& file, toml::table& effective, bool log_values)
{
  std::unique_ptr<Config> config(new Config);
  config->Read(file, effective, log_values);
  return config;
}

std::shared_ptr<const Config::Sync> Config::Sync::Get()
{ return std::atomic_load_explicit(&sync_config, std::memory_order_acquire); }

void Config::Activate(std::unique_ptr<Config> next)
{
  Shortcut::Set(std::move(next->shortcuts_));
  std::atomic_store_explicit(&sync_config, next->sync_, std::memory_order_release);
  current_config = std::move(next);
}

Config& Config::Get()
{
  // First use is inside il2cpp_init on the game thread. A file that cannot be read gives the defaults; values are
  // only logged when they came from the file.
  if (!current_config) {
    File::MigrateLegacyFiles();
    auto        file   = ReadSettingsFile("using the default settings");
    const bool  loaded = file.has_value();
    toml::table effective;
    auto        config = Parse(std::move(file).value_or(toml::table{}), effective, loaded);
    SaveEffective(effective, true);
    Activate(std::move(config));
  }
  return *current_config;
}

bool Config::Reload()
{
  const auto file = ReadSettingsFile("keeping the current settings");
  if (!file)
    return false;

  spdlog::info("reloading settings from {}", File::Config());
  std::unique_ptr<Config> next;
  toml::table             effective;
  try {
    next = Parse(*file, effective, true);
    SaveEffective(effective, false);
  } catch (const std::exception& e) {
    spdlog::error("could not apply {}: {}; keeping the current settings", File::Config(), e.what());
    return false;
  }
  // Hooks are installed once, so the startup hook groups stay in force.
  if (next->patches != Get().patches)
    spdlog::warn("changes to [patches] take effect after a restart");
  next->patches = Get().patches;
  Activate(std::move(next));
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
  // The first check only learns the time of the file Get already read.
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

void Config::Read(const toml::table& file, toml::table& effective, bool log_values)
{
  Reader r(file, effective, log_values);

  // [patches]. Reload keeps the startup values, since hooks are only installed once.
  constexpr std::pair<std::string_view, bool Patches::*> kPatches[] = {
      {"actionqueuerecoveryhooks", &Patches::actionqueuerecoveryhooks},
      {"alliancegamesstorehooks", &Patches::alliancegamesstorehooks},
      {"artifactexchangehooks", &Patches::artifactexchangehooks},
      {"audioeventhooks", &Patches::audioeventhooks},
      {"bufffixhooks", &Patches::bufffixhooks},
      {"cargoformathooks", &Patches::cargoformathooks},
      {"chatpatches", &Patches::chatpatches},
      {"chattitledisplayhooks", &Patches::chattitledisplayhooks},
      {"dailyfactionbulkclaimhooks", &Patches::dailyfactionbulkclaimhooks},
      {"doubleclickassignshiphooks", &Patches::doubleclickassignshiphooks},
      {"forbiddentechconfirmhooks", &Patches::forbiddentechconfirmhooks},
      {"forbiddentechshipnamehooks", &Patches::forbiddentechshipnamehooks},
      {"freeresizehooks", &Patches::freeresizehooks},
      {"galacticanomalytimerhooks", &Patches::galacticanomalytimerhooks},
      {"game_version", &Patches::game_version},
      {"giftsbulkclaimhooks", &Patches::giftsbulkclaimhooks},
      {"havenhistoryhooks", &Patches::havenhistoryhooks},
      {"havenzoomhooks", &Patches::havenzoomhooks},
      {"hotkeyhooks", &Patches::hotkeyhooks},
      {"instantcargocounterhooks", &Patches::instantcargocounterhooks},
      {"interiorzoomhooks", &Patches::interiorzoomhooks},
      {"loadingscreenhooks", &Patches::loadingscreenhooks},
      {"loadingtiphooks", &Patches::loadingtiphooks},
      {"miscpatches", &Patches::miscpatches},
      {"missionhudtweakshooks", &Patches::missionhudtweakshooks},
      {"objecttracker", &Patches::objecttracker},
      {"officersorthooks", &Patches::officersorthooks},
      {"panhooks", &Patches::panhooks},
      {"pinnedshipclicktoggle", &Patches::pinnedshipclicktoggle},
      {"pinnedshiphooks", &Patches::pinnedshiphooks},
      {"queueaddressguardhooks", &Patches::queueaddressguardhooks},
      {"revealconfirmhooks", &Patches::revealconfirmhooks},
      {"syncpatches", &Patches::syncpatches},
      {"tempcrashfixes", &Patches::tempcrashfixes},
      {"testpatches", &Patches::testpatches},
      {"thinqueueprotectionhooks", &Patches::thinqueueprotectionhooks},
      {"toastbannerhooks", &Patches::toastbannerhooks},
      {"transitionscreenhooks", &Patches::transitionscreenhooks},
      {"uiscalehooks", &Patches::uiscalehooks},
      {"zoomhooks", &Patches::zoomhooks},
  };
  for (const auto& [key, member] : kPatches)
    r.Read("patches", key, patches.*member);

  // [control] comes next: enable_experimental and hotkeys_extended decide which other settings are read.
  r.Read("control", "kirshara_fixes", control.kirshara_fixes);
  // Once kirshara_fixes is set, leftover queue keys are not worth a warning.
  if (!r.Node("control", "kirshara_fixes"))
    for (const auto path : {"control.faster_queue_recovery", "control.thin_queue_protection",
                            "control.queue_address_guard", "advanced.queue.thin_queue_protection"})
      r.Retired(path, "control.kirshara_fixes switches all the queue fixes");
  r.Read("control", "primary_requires_visible_target", control.primary_requires_visible_target);
  r.Read("control", "queue_enabled", control.queue_enabled);
  r.Read("control", "hotkeys_enabled", control.hotkeys_enabled);
  r.Read("control", "hotkeys_extended", control.hotkeys_extended);
  r.Read("control", "use_scopely_hotkeys", control.use_scopely_hotkeys);
  r.Read("control", "select_timer", control.select_timer);
  r.Read("control", "enable_experimental", control.enable_experimental);

  // [graphics]
  auto& g = graphics;
  r.Read("graphics", "ui_scale", g.ui_scale);
  r.ReadRenamed("graphics", "ui_scale_step", "ui_scale_adjust", g.ui_scale_step);
  r.Read("graphics", "ui_scale_ship", g.ui_scale_ship);
  r.Read("graphics", "ui_scale_viewer", g.ui_scale_viewer);
  r.Read("graphics", "zoom", g.zoom);
  r.ReadValid("graphics", "haven_zoom", g.haven_zoom, [](float v) { return std::isfinite(v) && v >= 0.0f; });
  r.ReadValid("graphics", "interior_zoom_scale", g.interior_zoom_scale,
              [](float v) { return std::isfinite(v) && v >= 1.0f; });
  r.Read("graphics", "fr_scale", g.fr_scale);
  const auto read_label = [&r](const std::string& prefix, FleetLabelProfile& profile) {
    r.Choose("graphics", prefix + "_detail", profile.detail, kFleetLabelDetail);
    r.ReadValid("graphics", prefix + "_threshold", profile.zoom_threshold,
                [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; });
  };
  read_label("zoom_label_player", g.zoom_label_player);
  read_label("zoom_label_non_player", g.zoom_label_non_player);
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
  // Without transition customization (or in a build that keeps the original background) the game's own black
  // background stays. The vars file records what is actually used.
#ifdef _USE_ORIGINAL_BG
  constexpr bool kOriginalBackground = true;
#else
  constexpr bool kOriginalBackground = false;
#endif
  if (kOriginalBackground || !g.loader_transition) {
    g.loader_transition_black = true;
    r.Record("graphics", "loader_transition_black", true);
  }
  r.Read("graphics", "loader_image", g.loader_image);
  r.Read("graphics", "loader_logo_scale", g.loader_logo_scale);
  r.Read("graphics", "loader_tip_enabled", g.loader_tip_enabled);
  r.Read("graphics", "galactic_anomaly_timer", g.galactic_anomaly_timer);

  // [buffs]
  r.Read("buffs", "use_out_of_dock_power", buffs.use_out_of_dock_power);

  // [audio]
  r.Read("audio", "trace_events", audio.trace_events);
  for (auto& event : StrSplit(r.Get<std::string>("audio", "disabled_events", ""), ',')) {
    if (AsciiStrToLower(event) == "all")
      audio.disable_all_events = true;
    else
      audio.disabled_events.push_back(std::move(event));
  }
  constexpr std::pair<std::string_view, NotificationSound Audio::*> kAlerts[] = {
      {"alert_victory", &Audio::alert_victory},
      {"alert_defeat", &Audio::alert_defeat},
      {"alert_armada_created", &Audio::alert_armada_created},
      {"alert_armada_battle_won", &Audio::alert_armada_battle_won},
      {"alert_armada_battle_lost", &Audio::alert_armada_battle_lost},
  };
  bool has_audio_alert = false;
  for (const auto& [key, alert] : kAlerts) {
    r.Choose("audio", key, audio.*alert, kNotificationSounds);
    has_audio_alert |= audio.*alert != NotificationSound::kNone;
  }
  // The alerts play from the toast hooks.
  if (has_audio_alert && !patches.toastbannerhooks)
    spdlog::warn("audio alerts need patches.toastbannerhooks = true; they stay silent");

  // [ui]
  auto& u = ui;
  r.Read("ui", "disable_escape_exit", u.disable_escape_exit);
  r.Read("ui", "disable_escape_exit_timer", u.disable_escape_exit_timer);
  r.Read("ui", "disable_preview_locate", u.disable_preview_locate);
  r.Read("ui", "disable_preview_recall", u.disable_preview_recall);
  r.Read("ui", "only_show_first_popup", u.only_show_first_popup);
  r.Read("ui", "reverse_haven_history", u.reverse_haven_history);
  r.Read("ui", "disable_move_keys", u.disable_move_keys);
  r.Read("ui", "disable_toast_banners", u.disable_toast_banners);
  r.ReadValid("ui", "default_chat_width", u.default_chat_width, [](float v) { return std::isfinite(v) && v >= 0.0f; });
  r.Read("ui", "auto_open_chat", u.auto_open_chat);
  r.Read("ui", "disable_galaxy_chat", u.disable_galaxy_chat);
  r.Read("ui", "disable_veil_chat", u.disable_veil_chat);
  r.Read("ui", "show_player_title", u.show_player_title);
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
  r.Read("ui", "dock_syncmanagement", u.dock_syncmanagement);
  r.Read("ui", "focus_search", u.focus_search);
  r.ReadRenamed("ui", "format_cargo_values", "cargo_format", u.format_cargo_values);
  r.Read("ui", "officer_sort", u.officer_sort);
  r.Read("ui", "arrow_keys_to_select_ship", u.arrow_keys_to_select_ship);
  r.Read("ui", "auto_confirm_ft_upgrade", u.auto_confirm_ft_upgrade);
  r.Read("ui", "forbidden_tech_ship_name", u.forbidden_tech_ship_name);
  r.Read("ui", "auto_confirm_alliance_contribution", u.auto_confirm_alliance_contribution);
  r.Read("ui", "auto_confirm_reveal_ability", u.auto_confirm_reveal_ability);
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
  r.Choose("ui", "hud_q_trials", u.hud_q_trials, kHudVisibility);
  r.Choose("ui", "hud_field_training", u.hud_field_training, kHudVisibility);
  r.Choose("ui", "hud_outposts", u.hud_outposts, kHudVisibility);
  r.Choose("ui", "hud_missions", u.hud_missions, kHudVisibility);
  u.disabled_banner_types = ReadBannerTypes(r, "disabled_banner_types");
  u.notify_banner_types   = ReadBannerTypes(r, "notify_banner_types");

  // [sync]
  auto sync = std::make_shared<Sync>();
  r.Read("sync", "debug", sync->debug);
  r.Read("sync", "logging", sync->logging);
  r.Read("sync", "resolver_cache_ttl", sync->resolver_cache_ttl);
  SyncConfig sync_defaults;
  r.Read("sync", "proxy", sync_defaults.proxy);
  r.Read("sync", "verify_ssl", sync_defaults.verify_ssl);
  for (const auto& [key, member, old_name] : SyncOptions)
    if (old_name.empty())
      r.Read("sync", key, sync_defaults.*member);
    else
      r.ReadRenamed("sync", key, old_name, sync_defaults.*member);
  ReadSyncTargets(r, sync->targets, sync_defaults);

  r.Retired("sync.file", "capture sync output with a local HTTP server instead");

  // The global options carry [sync]'s proxy and verify_ssl, and for each category whether any target wants it.
  sync->options = {.proxy = sync_defaults.proxy, .verify_ssl = sync_defaults.verify_ssl};
  for (const auto& [key, member, old_name] : SyncOptions)
    sync->options.*member =
        std::ranges::any_of(sync->targets | std::views::values, [&](const auto& target) { return target.*member; });

  // [config]
  r.Read("config", "settings_url", game.settings_url);
  r.Read("config", "assets_url_override", game.assets_url_override);

  shortcuts_ = Shortcut::Parse(file, effective, control.hotkeys_extended, control.enable_experimental);
  sync_      = std::move(sync);
}

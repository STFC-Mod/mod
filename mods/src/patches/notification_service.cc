#include "patches/notification_service.h"
#include "patches/battle_notify_parser.h"

#include "config.h"
#include "str_utils.h"

#include <il2cpp/il2cpp.h>
#include <prime/LanguageManager.h>
#include <prime/LocaleTextContext.h>
#include <prime/Toast.h>

#include <spdlog/spdlog.h>

#include <string>

#if _WIN32
#include <windows.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#endif

namespace {

// EventCategories is a struct wrapping one int32 (_flagValue).
struct EventCategories {
  int32_t flagValue;
};

struct EventModel : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimePlatform.Models", "EventModel">;

  EventCategories category()
  { return Field<"category_", EventCategories>(); }
};

} // namespace

// ---------------------------------------------------------------------------
// Toast state → human-readable title
// ---------------------------------------------------------------------------
static const char* toast_state_title(int state)
{
  switch (state) {
    case Standard:                  return "Notification";
    case FactionWarning:            return "Faction Warning";
    case FactionLevelUp:            return "Faction Level Up";
    case FactionLevelDown:          return "Faction Level Down";
    case FactionDiscovered:         return "Faction Discovered";
    case IncomingAttack:            return "Incoming Attack!";
    case IncomingAttackFaction:     return "Incoming Faction Attack!";
    case FleetBattle:               return "Fleet Battle";
    case StationBattle:             return "Station Under Attack!";
    case StationVictory:            return "Station Victory!";
    case Victory:                   return "Victory!";
    case Defeat:                    return "Defeat";
    case StationDefeat:             return "Station Defeat";
    case Tournament:                return "Event Progress";
    case ArmadaCreated:             return "Armada Created";
    case ArmadaCanceled:            return "Armada Canceled";
    case ArmadaIncomingAttack:      return "Armada Under Attack!";
    case ArmadaBattleWon:           return "Armada Victory!";
    case ArmadaBattleLost:          return "Armada Defeated";
    case DiplomacyUpdated:          return "Diplomacy Updated";
    case JoinedTakeover:            return "Territory Capture Joined";
    case CompetitorJoinedTakeover:  return "Competitor Joined Territory";
    case AbandonedTerritory:        return "Territory Abandoned";
    case TakeoverVictory:           return "Takeover Victory!";
    case TakeoverDefeat:            return "Takeover Defeat";
    case TreasuryProgress:          return "Treasury Progress";
    case TreasuryFull:              return "Treasury Full";
    case Achievement:               return "Achievement";
    case AssaultVictory:            return "Assault Victory!";
    case AssaultDefeat:             return "Assault Defeat";
    case ChallengeComplete:         return "Challenge Complete";
    case ChallengeFailed:           return "Challenge Failed";
    case StrikeHit:                 return "Strike Hit";
    case StrikeDefeat:              return "Strike Defeat";
    case WarchestProgress:          return "Warchest Progress";
    case WarchestFull:              return "Warchest Full";
    case PartialVictory:            return "Partial Victory";
    case ArenaTimeLeft:             return "Arena Time Warning";
    case ChainedEventScored:        return "Event Progress";
    case FleetPresetApplied:        return "Fleet Preset Applied";
    case SurgeWarmUpEnded:          return "Surge Started";
    case SurgeHostileGroupDefeated: return "Surge Hostiles Defeated";
    case SurgeTimeLeft:             return "Surge Time Warning";
    case QueueForLeaseActivated:    return "Queue Activated";
    case QueueForLeaseExpired:      return "Queue Expired";
    case PermanentQueuePurchased:   return "Permanent Queue Purchased";
    case OutpostStartedOrEnded:     return "Outpost Update";
    case CrossAllianceArmadaVictory:      return "Cross-Armada Victory!";
    case CrossAllianceArmadaDefeat:       return "Cross-Armada Defeated";
    case CrossAllianceArmadaPartialVictory: return "Cross-Armada Partial Victory";
    case FactionWeeklyEventsProgress:     return "Faction Weekly Event Progress";
    case FactionWeeklyEventsComplete:     return "Faction Weekly Event Complete";
    case ArmadaPlayerBlocked:       return "Armada Player Blocked";
    case ArmadaPlayerUnblocked:     return "Armada Player Unblocked";
    case DynamicCrisisUpdate:       return "Dynamic Crisis Update";
    case DynamicCrisisFailed:       return "Dynamic Crisis Failed";
    case DynamicCrisisCompleted:    return "Dynamic Crisis Completed";
    case GalacticAnomalySystemEntered: return "Galactic Anomaly Entered";
    default:                        return nullptr;
  }
}

// ---------------------------------------------------------------------------
// Platform notification delivery
// ---------------------------------------------------------------------------
#if _WIN32
static void show_system_notification(const char* title, const char* body)
{
  try {
    using namespace winrt::Windows::UI::Notifications;
    using namespace winrt::Windows::Data::Xml::Dom;

    auto xml   = ToastNotificationManager::GetTemplateContent(ToastTemplateType::ToastText02);
    auto nodes = xml.GetElementsByTagName(L"text");
    nodes.Item(0).InnerText(winrt::to_hstring(title));
    nodes.Item(1).InnerText(winrt::to_hstring(body));

    auto notification = ToastNotification(xml);
    auto notifier     = ToastNotificationManager::CreateToastNotifier(L"Star Trek Fleet Command");
    notifier.Show(notification);
  } catch (const winrt::hresult_error& e) {
    spdlog::warn("[Notify] WinRT notification failed: {}", winrt::to_string(e.message()));
  } catch (...) {
    spdlog::warn("[Notify] WinRT notification failed (unknown error)");
  }
}
#endif

// ---------------------------------------------------------------------------
// SEH wrapper — catches access violations from bad IL2CPP pointers
// ---------------------------------------------------------------------------
template <typename Fn>
static bool seh_call(Fn fn)
{
#if _WIN32
  __try {
    fn();
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  fn();
  return true;
#endif
}

// ---------------------------------------------------------------------------
// SEH-protected object to string conversion
// ---------------------------------------------------------------------------
static Il2CppString* seh_to_string_raw(Il2CppObject* obj)
{
  auto* cls = il2cpp_object_get_class(obj);
  if (cls) {
    auto* name = il2cpp_class_get_name(cls);
    if (name && strcmp(name, "String") == 0)
      return reinterpret_cast<Il2CppString*>(obj);
  }
  // Virtual ToString(); boxed value types dispatch to their own override.
  return static_cast<il2cpp::Object*>(obj)->Call<"ToString", Il2CppString*>();
}

static std::string safe_il2cpp_to_string(Il2CppObject* obj, il2cpp_array_size_t index)
{
  Il2CppString* raw = nullptr;
  if (!seh_call([&] { raw = seh_to_string_raw(obj); })) {
    spdlog::warn("[Notify] SEH: ToString crashed for param {}", index);
    return {};
  }
  if (!raw) return {};
  return to_string(raw);
}

// ---------------------------------------------------------------------------
// Replace {N} placeholders in template with values from an IL2CPP object[]
// ---------------------------------------------------------------------------
static std::string format_with_array(const std::string& tmpl, il2cpp::Array<Il2CppObject*>* arr)
{
  if (!arr) return tmpl;

  auto len = arr->size();
  if (len == 0) return tmpl;

  std::string result = tmpl;
  for (il2cpp_array_size_t i = 0; i < len; ++i) {
    auto* elem = (*arr)[i];
    if (!elem) continue;

    auto val = safe_il2cpp_to_string(elem, i);
    if (val.empty()) continue;

    auto prefix = "{" + std::to_string(i);
    for (auto pos = result.find(prefix); pos != std::string::npos; pos = result.find(prefix, pos + val.size())) {
      auto suffix = pos + prefix.size();
      auto end    = result.find('}', suffix);
      if (end == std::string::npos) break;

      // Accept both {N} and the numeric zero-padding form used by toast
      // templates, for example {5:000}.
      auto replacement = val;
      if (suffix != end) {
        if (result[suffix] != ':') continue;
        auto format = result.substr(suffix + 1, end - suffix - 1);
        if (!format.empty() && format.find_first_not_of('0') == std::string::npos && replacement.size() < format.size())
          replacement.insert(0, format.size() - replacement.size(), '0');
      }

      result.replace(pos, end - pos + 1, replacement);
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// Try every instance array field on an object. Toast parameter arrays have
// moved and changed names between game builds, including generated backing
// fields, so runtime metadata is more reliable than names or offsets. This
// scan stays on the raw field API because no member name is known up front.
// ---------------------------------------------------------------------------
static std::string format_with_array_fields(const std::string& tmpl, Il2CppObject* obj, std::string& fields_seen)
{
  if (!obj) return tmpl;

  auto result = tmpl;
  for (auto* cls = il2cpp_object_get_class(obj); cls; cls = il2cpp_class_get_parent(cls)) {
    void* iter = nullptr;
    while (auto* field = il2cpp_class_get_fields(cls, &iter)) {
      if ((il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC) != 0) continue;

      auto* type = il2cpp_field_get_type(field);
      auto  kind = type ? il2cpp_type_get_type(type) : IL2CPP_TYPE_END;
      if (kind != IL2CPP_TYPE_SZARRAY && kind != IL2CPP_TYPE_ARRAY) continue;

      il2cpp::Array<Il2CppObject*>* arr = nullptr;
      il2cpp_field_get_value(obj, field, &arr);

      if (!fields_seen.empty()) fields_seen += ", ";
      fields_seen += il2cpp_field_get_name(field);
      fields_seen += arr ? "[" + std::to_string(arr->size()) + "]" : "[null]";

      if (!arr || arr->size() == 0) continue;
      auto candidate = format_with_array(tmpl, arr);
      if (std::ranges::count(candidate, '{') < std::ranges::count(result, '{'))
        result = std::move(candidate);
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// Replace {N} placeholders using Toast.TextParameters, falling back to
// LocaleTextContext._textParameters if the Toast array is empty.
// ---------------------------------------------------------------------------
static std::string format_placeholders(const std::string& tmpl, Toast* toast, LocaleTextContext* ltc)
{
  // Try Toast.TextParameters first.
  auto* toast_arr = toast->get_TextParameters();
  if (toast_arr && toast_arr->size() > 0) {
    return format_with_array(tmpl, toast_arr);
  }

  std::string fields_seen;
  auto result = format_with_array_fields(tmpl, toast, fields_seen);
  if (result != tmpl) return result;

  result = format_with_array_fields(tmpl, ltc, fields_seen);
  if (result != tmpl) return result;

  if (tmpl.find('{') != std::string::npos) {
    spdlog::warn("[Notify] Placeholders unresolved; array fields seen: {}. Template: \"{}\"",
                 fields_seen.empty() ? "none" : fields_seen, tmpl);
  }
  return tmpl;
}

// ---------------------------------------------------------------------------
// Resolve basic localized text from a Toast's TextLocaleTextContext
// ---------------------------------------------------------------------------
static std::string resolve_toast_text(Toast* toast)
{
  auto* ltc = static_cast<LocaleTextContext*>(toast->get_TextLocaleTextContext());
  if (!ltc) {
    spdlog::debug("[Notify] No TextLocaleTextContext for toast state {}", toast->get_State());
    return {};
  }

  // Try the static LocaleUtilities.Localize(LTC, bool, bool) first — it handles
  // parameter substitution internally and returns a fully formatted string.
  if (auto* ret = LocaleUtilities::Localize(ltc, true, false)) {
    auto result = to_string(ret);
    // LocaleUtilities.Localize may return a template with unresolved {N}
    // placeholders if the LTC's own _textParameters are empty.  The actual
    // parameter values may be in Toast.TextParameters, so run
    // format_placeholders to substitute them.
    if (result.find('{') != std::string::npos)
      result = format_placeholders(result, toast, ltc);
    return result;
  }
  spdlog::debug("[Notify] LocaleUtilities.Localize returned null for toast state {}", toast->get_State());

  // Fall back to instance LanguageManager.Localize(out string, LTC)
  auto* langMgr = LanguageManager::Instance();
  if (!langMgr) return {};

  Il2CppString* resolved = nullptr;
  if (!langMgr->Localize(resolved, ltc) || !resolved) {
    spdlog::debug("[Notify] LanguageManager.Localize returned false for toast state {}", toast->get_State());
    return {};
  }

  auto tmpl = to_string(resolved);
  return format_placeholders(tmpl, toast, ltc);
}

// ---------------------------------------------------------------------------
// Resolve the event category from Toast.Data (EventModel.category_) and map
// it to a human-readable name.
// ---------------------------------------------------------------------------
static const char* event_category_name(int32_t cat)
{
  switch (cat) {
    case 0:  return "Standard";
    case 1:  return "Daily Goals";
    case 2:  return "Daily Milestone";
    case 3:  return "Leaderboard";
    case 4:  return "Stat";
    case 5:  return "Battle Pass Season";
    case 6:  return "Battle Pass Event";
    case 7:  return "Treasury Progress";
    case 8:  return "Treasury Reward";
    case 9:  return "Server Clash";
    case 10: return "Webstore Event";
    case 11: return "Player Lifecycle";
    case 12: return "Field Training";
    case 13: return "FT Category";
    case 14: return "Cutscenes";
    case 15: return "Minigame";
    case 16: return "Minigame Stage";
    case 17: return "Warchest";
    case 18: return "Alliance Game";
    case 19: return "Alliance Game Task";
    case 20: return "Meta Event";
    case 21: return "Meta Event Objective";
    case 22: return "Invasion";
    case 23: return "Loop Museum";
    case 24: return "Loop Museum Task";
    case 25: return "PLC BP Season";
    case 26: return "PLC BP Event";
    case 27: return "Progression Reward";
    case 28: return "Faction Weekly Events";
    default: return nullptr;
  }
}

static std::string resolve_event_category(Toast* toast)
{
  auto* data = toast->get_Data();
  auto* event = data ? static_cast<il2cpp::Object*>(data)->As<EventModel>() : nullptr;
  if (!event) return {};

  auto* name = event_category_name(event->category().flagValue);
  return name ? name : std::string{};
}

// ---------------------------------------------------------------------------
// Strip Unity rich text tags (e.g. <color=#FF0000>, <b>, </size>)
// ---------------------------------------------------------------------------
static std::string strip_unity_rich_text(const std::string& s)
{
  std::string result;
  result.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '<') {
      auto end = s.find('>', i);
      if (end != std::string::npos) { i = end + 1; continue; }
    }
    result += s[i++];
  }
  return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void notification_init()
{
  // IL2CPP members resolve on first use; only the platform side needs setup.
#if _WIN32
  try {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    spdlog::info("[Notify] Windows notification service initialized");
  } catch (const winrt::hresult_error& e) {
    spdlog::warn("[Notify] Windows notification service failed: {:#x} {}",
                 static_cast<unsigned long>(e.code()), winrt::to_string(e.message()));
  } catch (...) {
    spdlog::warn("[Notify] Windows notification service failed (unknown error)");
  }
#else
  spdlog::info("[Notify] Notification service: platform not supported (no-op)");
#endif
}

void notification_handle_toast(Toast* toast)
{
  if (!toast)
    return;
  notification_audio_play(Config::Get().NotificationSoundForToast(static_cast<int>(toast->get_State())));
#if !_WIN32
  return; // No notification delivery on non-Windows platforms yet
#else
  auto state = toast->get_State();

  // Check if this toast type is in the user's notify list
  const auto& notify_types = Config::Get().notify_banner_types;
  if (std::ranges::find(notify_types, state) == notify_types.end()) {
    return;
  }

  auto* title = toast_state_title(state);
  if (!title) {
    spdlog::debug("[Notify] No title mapping for toast state {}, skipping", state);
    return;
  }

  auto body = battle_notify_parse(toast);
  if (body.empty()) {
    body = strip_unity_rich_text(resolve_toast_text(toast));
  }
  if (body.empty()) {
    body = "(no details available)";
  }

  // Prepend event category for event-based toasts that use EventModel as Data
  if (state == Achievement || state == Tournament || state == ChainedEventScored ||
      state == TreasuryProgress || state == TreasuryFull ||
      state == WarchestProgress || state == WarchestFull ||
      state == FactionWeeklyEventsProgress || state == FactionWeeklyEventsComplete) {
    auto category = resolve_event_category(toast);
    if (!category.empty()) {
      body = category + " - " + body;
    }
  }

  show_system_notification(title, body.c_str());
#endif
}

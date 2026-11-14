#include "patches/battle_notify_parser.h"

#include "str_utils.h"

#include <il2cpp/il2cpp.h>
#include <prime/BattleResultHeader.h>
#include <prime/HullSpec.h>
#include <prime/LocaleTextContext.h>
#include <prime/SpecService.h>
#include <prime/Toast.h>
#include <prime/UserProfile.h>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include <string>

#if _WIN32
#include <windows.h>
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
// IL2CPP localization infrastructure
// ---------------------------------------------------------------------------
namespace {

using SystemObject = il2cpp::Class<"mscorlib", "System", "Object">;
using SystemInt64  = il2cpp::Class<"mscorlib", "System", "Int64">;

// Create a LocaleTextContext(identifier, category), apply the given parameter,
// and call LocaleUtilities.Localize. Returns empty string on failure.
std::string localize(const char* identifier, const char* category, Il2CppObject* param)
{
  auto* object_class = SystemObject::Raw();
  if (!object_class || !param) return {};

  std::string result;
  if (!seh_call([&] {
        auto* idStr  = il2cpp_string_new(identifier);
        auto* catStr = il2cpp_string_new(category);

        auto* arr = il2cpp::Array<Il2CppObject*>::New(object_class, 1);
        if (!arr) return;
        arr->Set(0, param);

        auto* ltc = il2cpp::New<LocaleTextContext>(idStr, catStr);
        if (!ltc) return;

        ltc->ApplyIdentifierParameters(arr);
        if (auto* ret = LocaleUtilities::Localize(ltc, false, false))
          result = to_string(ret);
      }))
    spdlog::warn("[Notify] SEH: localize crashed for \"{}/{}\"", identifier, category);
  return result;
}

// Box an int64 as Il2CppObject for use as a localization parameter.
Il2CppObject* box_int64(int64_t value)
{
  auto* int64_class = SystemInt64::Raw();
  return int64_class ? il2cpp_value_box(int64_class, &value) : nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Hull name key → human-readable name
//   "Hull_L30_Destroyer_Klingon_LIVE" → "Lv.30 Destroyer Klingon"
// ---------------------------------------------------------------------------
static std::string parse_hull_key(const std::string& key)
{
  auto s = key;

  if (s.size() > 5 && s.ends_with("_LIVE"))
    s = s.substr(0, s.size() - 5);
  if (s.starts_with("Hull_"))
    s = s.substr(5);

  for (auto& c : s)
    if (c == '_') c = ' ';

  if (s.size() >= 2 && s[0] == 'L' && std::isdigit(s[1])) {
    auto space = s.find(' ');
    auto lvl   = s.substr(1, space == std::string::npos ? std::string::npos : space - 1);
    auto rest  = space == std::string::npos ? "" : s.substr(space);
    s = "Lv." + lvl + rest;
  }

  bool newWord = true;
  for (auto& c : s) {
    if (newWord && std::isalpha(static_cast<unsigned char>(c))) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      newWord = false;
    } else if (!newWord && std::isalpha(static_cast<unsigned char>(c))) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (c == ' ') newWord = true;
  }

  return s;
}

// ---------------------------------------------------------------------------
// Localization helpers — thin wrappers over localize()
// ---------------------------------------------------------------------------

// Hull name via numeric locaId: "ship_name_{0}" / "ships"
static std::string localize_hull_name(int64_t locaId)
{
  auto* boxed = box_int64(locaId);
  return boxed ? localize("ship_name_{0}", "ships", boxed) : std::string{};
}

// Hull name via string locaStringId: "ship_name_{0}" / "ships"
static std::string localize_hull_name(const std::string& locaStringId)
{
  auto* strObj = il2cpp_string_new(locaStringId.c_str());
  return localize("ship_name_{0}", "ships", reinterpret_cast<Il2CppObject*>(strObj));
}

// Enemy commander name via locaId: "marauder_name_only_{0}" / "navigation"
static std::string localize_enemy_name(int64_t locaId)
{
  auto* boxed = box_int64(locaId);
  return boxed ? localize("marauder_name_only_{0}", "navigation", boxed) : std::string{};
}

// ---------------------------------------------------------------------------
// Resolve hull ID → display name via SpecService + game localization
// ---------------------------------------------------------------------------
static std::string resolve_hull_name(BattleResultHeader* brh, int64_t hullId)
{
  if (hullId == 0) return "";

  auto* specSvc = static_cast<SpecService*>(brh->get_SpecService());
  if (!specSvc) return fmt::format("Hull#{}", hullId);

  auto* hull = specSvc->GetHull(hullId);
  if (!hull) return fmt::format("Hull#{}", hullId);

  std::string locaStringId;
  int64_t locaId = 0;
  if (auto* idRefs = hull->idRefs()) {
    if (auto* locaStrPtr = idRefs->locaStringId()) locaStringId = to_string(locaStrPtr);
    locaId = idRefs->locaId();
  }

  // Try numeric locaId first, then string locaStringId, then manual parse
  if (locaId != 0) {
    auto localized = localize_hull_name(locaId);
    if (!localized.empty() && localized != fmt::format("ship_name_{}", locaId))
      return localized;
  }
  if (!locaStringId.empty()) {
    auto localized = localize_hull_name(locaStringId);
    if (!localized.empty() && localized != fmt::format("ship_name_{}", locaStringId))
      return localized;
  }

  auto* nameStr = hull->Name();
  if (!nameStr) return fmt::format("Hull#{}", hullId);
  return parse_hull_key(to_string(nameStr));
}

// ---------------------------------------------------------------------------
// Normalize ALL CAPS ship names to Title Case
// ---------------------------------------------------------------------------
static std::string normalize_ship_name(std::string name)
{
  bool nextUpper = true;
  for (auto& c : name) {
    if (nextUpper && c >= 'a' && c <= 'z')
      c = std::toupper(static_cast<unsigned char>(c));
    else if (!nextUpper && c >= 'A' && c <= 'Z')
      c = std::tolower(static_cast<unsigned char>(c));
    nextUpper = (c == ' ' || c == '-' || c == '_');
  }
  return name;
}

// ---------------------------------------------------------------------------
// Battle summary data model
// ---------------------------------------------------------------------------
struct BattleSummaryData {
  std::string playerName;
  std::string enemyName;
  std::string playerShip;
  std::string enemyShip;
  bool isPvp = false;

  // Format as "Player (Ship) vs Enemy (Ship)".
  // For non-PVP battles with a localized enemy name, the enemy ship class is omitted.
  std::string format_body() const
  {
    auto format_side = [](const std::string& name, const std::string& ship) -> std::string {
      if (!name.empty() && !ship.empty()) return fmt::format("{} ({})", name, ship);
      if (!name.empty()) return name;
      if (!ship.empty()) return ship;
      return "";
    };

    auto left  = format_side(playerName, playerShip);
    auto right = (!isPvp && !enemyName.empty()) ? enemyName : format_side(enemyName, enemyShip);

    if (left.empty() && right.empty()) return "";
    if (left.empty()) return right;
    if (right.empty()) return left;
    return left + " vs " + right;
  }
};

// ---------------------------------------------------------------------------
// Extract player/enemy names + ship hulls from BattleResultHeader
// ---------------------------------------------------------------------------
static BattleSummaryData build_battle_data(Il2CppObject* data)
{
  BattleSummaryData result;
  if (!data) return result;

  auto* brh = static_cast<il2cpp::Object*>(data)->As<BattleResultHeader>();
  if (!brh) return result;

  if (!seh_call([&] {
        auto* profile = static_cast<UserProfile*>(brh->get_PlayerUserProfile());
        if (profile && profile->Name())
          result.playerName = to_string(profile->Name());
      }))
    spdlog::warn("[Notify] SEH: get_PlayerUserProfile crashed");

  if (!seh_call([&] {
        auto* profile = static_cast<UserProfile*>(brh->get_EnemyUserProfile());
        if (profile) {
          if (profile->Name())
            result.enemyName = to_string(profile->Name());
          // NPC/marauder profiles have empty names — localize via LocaId
          if (result.enemyName.empty()) {
            auto locaId = profile->LocaId();
            if (locaId != 0) {
              auto localized = localize_enemy_name(locaId);
              if (!localized.empty() && localized != "Unknown")
                result.enemyName = localized;
            }
          }
        }
      }))
    spdlog::warn("[Notify] SEH: get_EnemyUserProfile crashed");

  // PVP battle types — for non-PVP we drop the enemy hull classification
  if (!seh_call([&] {
        auto bt = brh->get_BattleType();
        result.isPvp = (bt == static_cast<int>(BattleType::Fleet) ||
                        bt == static_cast<int>(BattleType::Base) ||
                        bt == static_cast<int>(BattleType::ArmadaBase) ||
                        bt == static_cast<int>(BattleType::ArmadaAsb) ||
                        bt == static_cast<int>(BattleType::ArmadaMta) ||
                        bt == static_cast<int>(BattleType::PvpCuttingBeam) ||
                        bt == static_cast<int>(BattleType::PvpChainShot));
      }))
    spdlog::warn("[Notify] SEH: get_BattleType crashed");

  if (!seh_call([&] {
        result.playerShip = normalize_ship_name(resolve_hull_name(brh, brh->PlayerShipHullId()));
      }))
    spdlog::warn("[Notify] SEH: PlayerShipHullId crashed");

  if (!seh_call([&] {
        result.enemyShip = normalize_ship_name(resolve_hull_name(brh, brh->EnemyShipHullId()));
      }))
    spdlog::warn("[Notify] SEH: EnemyShipHullId crashed");

  return result;
}

// Toast data types vary by state, so fields resolve on the receiver's runtime class.
template <il2cpp::Name N, typename T>
static T read_instance_field(Il2CppObject* obj)
{
  return obj ? static_cast<il2cpp::Object*>(obj)->Field<N, T>() : T{};
}

static std::string build_armada_created_body(Il2CppObject* data)
{
  std::string result;
  if (!seh_call([&] {
        auto* attack   = read_instance_field<"ArmadaAttack", Il2CppObject*>(data);
        auto* alliance = read_instance_field<"Alliance", Il2CppObject*>(data);
        auto* owner    = read_instance_field<"<Owner>k__BackingField", Il2CppObject*>(attack);
        auto* target   = read_instance_field<"<TargetUserProfile>k__BackingField", Il2CppObject*>(attack);

        auto* tag_value   = read_instance_field<"tag_", Il2CppString*>(alliance);
        auto* owner_value = read_instance_field<"name_", Il2CppString*>(owner);
        auto* target_value = read_instance_field<"name_", Il2CppString*>(target);

        auto tag         = tag_value ? to_string(tag_value) : std::string{};
        auto owner_name  = owner_value ? to_string(owner_value) : std::string{};
        auto target_name = target_value ? to_string(target_value) : std::string{};
        auto target_level = read_instance_field<"<TargetLevel>k__BackingField", int32_t>(attack);

        if (target_name.empty()) {
          auto loca_id = read_instance_field<"_locaId", int64_t>(target);
          if (loca_id != 0) target_name = localize_enemy_name(loca_id);
        }

        if (owner_name.empty()) owner_name = "An alliance member";
        if (target_name.empty() || target_name == "Unknown") target_name = "Armada target";

        auto owner_label = tag.empty() ? owner_name : fmt::format("[{}] {}", tag, owner_name);
        result = fmt::format("{} started an Armada!\nLv.{:03} {}", owner_label, target_level, target_name);
      })) {
    spdlog::warn("[Notify] SEH: ArmadaCreated data parsing crashed");
    return {};
  }
  return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
std::string battle_notify_parse(Toast* toast)
{
  if (toast->get_State() == ArmadaCreated)
    return build_armada_created_body(toast->get_Data());

  switch (toast->get_State()) {
    case Victory:
    case Defeat:
    case PartialVictory:
    case StationVictory:
    case StationDefeat:
    case StationBattle:
    case IncomingAttack:
    case FleetBattle:
    case ArmadaBattleWon:
    case ArmadaBattleLost:
    case AssaultVictory:
    case AssaultDefeat:
      break;
    default:
      return {};
  }

  auto* data = toast->get_Data();
  if (!data) return {};

  auto bsd = build_battle_data(data);
  return bsd.format_body();
}

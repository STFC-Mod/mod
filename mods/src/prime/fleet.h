#pragma once

// Player fleets and ships: fleet data and managers, the fleet bar, the action queue, and the ship management screens
// and dock.

#include "prime/Hub.h"
#include "prime/server.h"
#include "prime/ui.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>
#include <optional>
#include <string>

enum class DeployedFleetType : int32_t {
  Nonexistent,
  Player,
  Marauder,
  NpcInstantiated,
  Sentinel,
  Alliance,
  Challenge,
};

struct FleetDeployedData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData">;

  HullSpec* Hull()
  { return Get<"Hull", HullSpec*>(); }
  // Nonexistent when the property is missing.
  DeployedFleetType FleetType()
  { return Get<"FleetType", DeployedFleetType>(); }
};

struct BattleTargetData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BattleTargetData">;

  FleetDeployedData* TargetFleetDeployedData()
  { return Field<"TargetFleetDeployedData", FleetDeployedData*>(); }
};

enum class FleetState : int32_t {
  Unknown                = 0,
  IdleInSpace            = 1,
  Docked                 = 2,
  Mining                 = 4,
  Destroyed              = 8,
  TieringUp              = 16,
  CanReplaceOfficers     = 18,
  Repairing              = 32,
  CannotLaunch           = 56,
  Battling               = 64,
  WarpCharging           = 128,
  Warping                = 256,
  CanRemove              = 384,
  Impulsing              = 512,
  CanActivateAbility     = 513,
  CanDisco               = 515,
  Capturing              = 1024,
  AutoHunting            = 2048,
  CannotMove             = 2552,
  CanManage              = 2947,
  CanBeTargetedByAbility = 3589,
  CanEngage              = 3591,
  Outposting             = 4096,
  CanRecall              = 5637,
  Deployed               = 8133,
  CanLocate              = 8135
};

// Cargo, mining slot and timers off FleetPlayerData, read for fleet sync. The interfaces are bound as such: their
// getters dispatch to the runtime class, which also covers FleetMiningSlot's explicit IMiningSlot implementation.

// System.DateTime: ticks in the low 62 bits, the Kind in the top two.
struct DateTime {
  uint64_t dateData;

  // Unix seconds, or empty for DateTime.MinValue (unset).
  std::optional<int64_t> UnixSeconds() const
  {
    const auto ticks = static_cast<int64_t>(dateData & 0x3FFFFFFFFFFFFFFFull);
    if (ticks <= 0)
      return std::nullopt;
    return (ticks - 621355968000000000LL) / 10000000LL;
  }
};

// Concrete: ProgressData, CargoProgressData.
struct IProgressData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "IProgressData">;

  double CurrentValue()
  { return Get<"CurrentValue", double>(); }
  double MaxValue()
  { return Get<"MaxValue", double>(); }
};

struct CargoHoldData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CargoHoldData">;

  IProgressData* CurrentCargo()
  { return Get<"CurrentCargo", IProgressData*>(); }
  IProgressData* ProtectedCargoProgress()
  { return Get<"ProtectedCargoProgress", IProgressData*>(); }
};

struct IMiningNodeParams : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "IMiningNodeParams">;

  int64_t Amount()
  { return Get<"Amount", int64_t>(); }
};

// Concrete: FleetMiningSlot, MiningSlot.
struct IMiningSlot : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "IMiningSlot">;

  int64_t ParentId()
  { return Get<"ParentId", int64_t>(); }
  int64_t ResourceId()
  { return Get<"ResourceId", int64_t>(); }
  double AmountMined()
  { return Get<"AmountMined", double>(); }
  DateTime OccupyTimeUtc()
  { return Get<"OccupyTimeUtc", DateTime>(); }
  IMiningNodeParams* PointData()
  { return Get<"PointData", IMiningNodeParams*>(); }
};

// Concrete: MiningSlotTimer and others.
struct ITimerData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ITimerData">;

  DateTime DueTime()
  { return Get<"DueTime", DateTime>(); }
};

// A ship name as the game shows it, in the player's language, for a hull's localisation id; empty for none.
inline std::string LocalizedShipName(int64_t loca_id)
{
  if (loca_id <= 0)
    return {};
  auto* context = il2cpp::New<LocaleTextContext>(il2cpp::NewString("ship_name_" + std::to_string(loca_id)),
                                                 il2cpp::NewString("ships"));
  auto* name    = context ? Localizer::Localise(context) : nullptr;
  return name ? il2cpp::ToUtf8(name) : std::string();
}

struct FleetPlayerData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetPlayerData">;

  int32_t Index()
  { return Get<"Index", int32_t>(); }
  HullSpec* Hull()
  { return Get<"Hull", HullSpec*>(); }
  FleetState CurrentState()
  { return Get<"CurrentState", FleetState>(); }
  FleetState PreviousState()
  { return Get<"PreviousState", FleetState>(); }
  uint64_t Id()
  { return Get<"Id", uint64_t>(); }
  int64_t Level()
  { return Get<"Level", int64_t>(); }
  bool HasShip()
  { return Get<"HasShip", bool>(); }
  int64_t HullId()
  { return Get<"HullId", int64_t>(); }
  bool HasAppliedTech()
  {
    struct BaseShip : il2cpp::Object {
      using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BaseShip">;
      int64_t Id()
      { return Get<"Id", int64_t>(); }
    };
    struct Slot : il2cpp::Object {
      using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "EntitySlot">;
      int64_t SpecId()
      { return Get<"SlotSpecId", int64_t>(); }
      bool HasItem()
      {
        static const auto* getter = Class::Method("get_SlotItemId", 0).Info();
        if (!getter || (getter->flags & METHOD_ATTRIBUTE_STATIC)
            || getter->return_type->type != IL2CPP_TYPE_GENERICINST)
          return false;
        // Runtime invocation boxes Nullable<Int64> as null or its value, without assuming its native layout.
        return il2cpp::detail::InvokeBoxed(getter, this, nullptr) != nullptr;
      }
    };
    struct Manager : il2cpp::Object {
      using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechManager">;
      bool ActiveSlots(int64_t ship_id, il2cpp::List<Slot*>*& slots)
      { return Call<"TryGetActiveForbiddenTechSlot", bool>(ship_id, std::ref(slots)); }
    };
    auto*                ship    = Get<"Ship", BaseShip*>();
    auto*                manager = Manager::Class::Get<"Instance", Manager*>();
    il2cpp::List<Slot*>* slots   = nullptr;
    if (!ship || !manager || !manager->ActiveSlots(ship->Id(), slots) || !slots)
      return false;
    for (auto* slot : *slots) {
      if (!slot)
        continue;
      const auto spec = slot->SpecId();
      if ((spec == 3502081615 || spec == 953301906) && slot->HasItem())
        return true;
    }
    return false;
  }

  // Live state, read for fleet sync.
  bool IsHome()
  { return Get<"IsHome", bool>(); }
  bool IsDeployed()
  { return Get<"IsDeployed", bool>(); }
  bool IsMining()
  { return Get<"IsMining", bool>(); }
  bool IsRepairing()
  { return Get<"IsRepairing", bool>(); }
  bool IsDamaged()
  { return Get<"IsDamaged", bool>(); }
  bool IsDestroyed()
  { return Get<"IsDestroyed", bool>(); }
  int32_t DrydockID()
  { return Get<"DrydockID", int32_t>(); }
  float CargoResourceFillLevel()
  { return Get<"CargoResourceFillLevel", float>(); }
  CargoHoldData* CargoHold()
  { return Get<"CargoHoldData", CargoHoldData*>(); }
  IMiningSlot* MiningData()
  { return Get<"MiningData", IMiningSlot*>(); }
  ITimerData* Timer()
  { return Get<"Timer", ITimerData*>(); }
  // No property for the mining timer, so read the private field.
  ITimerData* MiningSlotTimer()
  { return Field<"_miningSlotTimer", ITimerData*>(); }
  float GetMiningRate()
  { return Call<"GetMiningRate", float>(); }
  // Damage on the fleet's ship, so a docked ship's hull is known without the server. Empty without a ship.
  std::optional<float> HullDamage()
  {
    auto* ship = Get<"Ship", ShipModel*>();
    return ship ? std::optional(ship->HullDamage()) : std::nullopt;
  }

  int64_t GetLocaId()
  { return Call<"GetLocaId", int64_t>(); }

  // The ship's name as the game shows it, in the player's language; empty without a hull.
  std::string DisplayName()
  { return LocalizedShipName(Hull() ? GetLocaId() : 0); }
};

using IActionData = void*;

// Digit.PrimeServer.Models.ActionType is a struct around a flag value, not an enum, so it crosses the
// managed boundary as a 4-byte struct.
struct ActionType {
  int32_t flagValue;

  friend bool operator==(ActionType, ActionType) = default;

  static const ActionType Manage;
  static const ActionType Repair;
  static const ActionType Recall;
};

inline constexpr ActionType ActionType::Manage{-1997567611};
inline constexpr ActionType ActionType::Repair{-1850668115};
inline constexpr ActionType ActionType::Recall{-1851055311};

enum class ActionBehaviour : int32_t {
  Default             = 1,
  Instant             = 2,
  Speedup             = 4,
  Collect             = 8,
  AskHelp             = 16,
  RequestConfirmation = 32,
};

struct AbilityTimeSpan {
  int64_t ticks;
};

struct AbilityTimerDataContext : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "TimerDataContext">;
  AbilityTimeSpan RemainingTime()
  { return Get<"RemainingTime", AbilityTimeSpan>(); }
};

struct ShipActionPanelLocalViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipActionPanelLocalViewController">;
  il2cpp::Array<int64_t>* abilityIDs()
  { return Field<"_abilityIDs", il2cpp::Array<int64_t>*>(); }
  il2cpp::Array<GenericButtonWidget*>* buttons()
  { return Field<"_buttons", il2cpp::Array<GenericButtonWidget*>*>(); }
  AbilityTimerDataContext* GetRemainingCooldown(int64_t abilityId)
  { return Call<"GetRemainingCooldown", AbilityTimerDataContext*>(abilityId); }
  bool HasShipAnActiveAbilityWithActiveEffect()
  { return Call<"HasShipAnActiveAbilityWithActiveEffect", bool>(); }
};

struct FleetLocalViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "FleetLocalViewController">;

  ShipActionPanelLocalViewController* shipActionPanelLocalViewController()
  { return Field<"_shipActionPanelLocalViewController", ShipActionPanelLocalViewController*>(); }
  FleetPlayerData* fleet()
  { return Get<"fleet", FleetPlayerData*>(); }

  void CancelButtonClicked()
  { Call<"CancelButtonClicked">(); }

  // callback is an Action<bool, ActionBehaviour>; null is accepted.
  bool RequestAction(IActionData target, ActionType type, int64_t index, ActionBehaviour behaviourMask,
                     void* callback = nullptr)
  { return Call<"RequestAction", bool>(target, type, index, behaviourMask, callback); }
};

struct FleetBarContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "FleetBarContext">;
  il2cpp::Array<FleetPlayerData*>* FleetData()
  { return Get<"FleetData", il2cpp::Array<FleetPlayerData*>*>(); }
};

struct FleetBarViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "FleetBarViewController">;

  FleetBarContext* CanvasContext()
  { return Get<"CanvasContext", FleetBarContext*>(); }

  // The int overload; the other takes the list element Component.
  void RequestSelect(int32_t index, bool simulated = false)
  { Call<"RequestSelect">(index, simulated); }
  bool IsIndexSelected(int32_t index)
  { return Call<"IsIndexSelected", bool>(index); }

  FleetLocalViewController* fleetPanelController()
  { return Field<"_fleetPanelController", FleetLocalViewController*>(); }

  // The fleet whose panel is open in the bar, or null.
  FleetPlayerData* SelectedFleet()
  {
    auto* panel = fleetPanelController();
    return panel ? panel->fleet() : nullptr;
  }
};

struct FleetsManager : MonoSingleton<FleetsManager> {
  bool HasFleetService()
  {
    static FieldInfo* const field =
        Class::Raw() ? il2cpp_class_get_field_from_name(Class::Raw(), "_fleetServiceCache") : nullptr;
    static const MethodInfo* const has_service =
        field ? il2cpp_class_get_method_from_name(il2cpp_class_from_type(il2cpp_field_get_type(field)),
                                                  "get_HasService", 0)
              : nullptr;
    if (!has_service) {
      return false;
    }

    auto* cache = reinterpret_cast<char*>(this) + il2cpp_field_get_offset(field);
    if (*reinterpret_cast<void**>(cache)) {
      return true;
    }

    Il2CppException* exception = nullptr;
    auto*            result    = il2cpp_runtime_invoke(has_service, cache, nullptr, &exception);
    return !exception && result && *static_cast<bool*>(il2cpp_object_unbox(result));
  }
  FleetPlayerData* GetFleetPlayerData(int32_t index)
  { return Call<"GetFleetPlayerData", FleetPlayerData*>(index); }

  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.FleetManagement", "FleetsManager">;

  void RequestViewFleet(FleetPlayerData* fleetData, bool showSystemInfo = false)
  { Call<"RequestViewFleet">(fleetData, showSystemInfo); }

  FleetPlayerData* GetSelectedFleetData()
  { return Call<"GetSelectedFleetData", FleetPlayerData*>(); }
};

struct ForbiddenTechManager : MonoSingleton<ForbiddenTechManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechManager">;

  bool TryGetEntitySlotFromForbiddenTech(int64_t tech_id, EntitySlot*& slot)
  { return Call<"TryGetEntitySlotFromForbiddenTech", bool>(tech_id, std::ref(slot)); }

  // _fleetServiceCache is a CachedService<FleetService> struct whose Service getter resolves the service lazily. The
  // getter runs on a copy, so the manager's own cache is left as it was; the runtime takes the struct receiver
  // unboxed.
  FleetService* fleetService()
  {
    struct CachedService {
      FleetService* service;
    };
    auto cache = Field<"_fleetServiceCache", CachedService>();
    if (cache.service)
      return cache.service;
    static FieldInfo* const field =
        Class::Raw() ? il2cpp_class_get_field_from_name(Class::Raw(), "_fleetServiceCache") : nullptr;
    static const MethodInfo* const getter =
        field ? il2cpp_class_get_method_from_name(il2cpp_class_from_type(il2cpp_field_get_type(field)), "get_Service",
                                                  0)
              : nullptr;
    if (!getter)
      return nullptr;
    return reinterpret_cast<FleetService*>(il2cpp::detail::InvokeBoxed(getter, &cache, nullptr));
  }
};

struct DeploymentManager : MonoSingleton<DeploymentManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "", "DeploymentManager">;

  bool HasInstantWarpAbility(FleetPlayerData* selectedFleet)
  { return Call<"HasInstantWarpAbility", bool>(selectedFleet); }
};

struct ActionQueueManager : MonoSingleton<ActionQueueManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager">;

  void ClearQueue(FleetPlayerData* playerData)
  { Call<"ClearQueue">(playerData); }
  bool IsQueueFull(FleetPlayerData* playerData)
  { return Call<"IsQueueFull", bool>(playerData); }
  bool IsQueueUnlocked()
  { return Call<"IsQueueUnlocked", bool>(); }
};

struct FleetMeshSelector : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "FleetMeshSelector">;

  unity::GameObject* LoadedObject()
  { return Get<"LoadedObject", unity::GameObject*>(); }
};

struct CourseData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CourseData">;

  FleetPlayerData* PlayerFleet()
  { return Get<"PlayerFleet", FleetPlayerData*>(); }
};

// Places a ship model in system view; the hook scales it by ui_scale_ship.
using SystemViewShip = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "SystemViewShip">;

struct ShipManagementViewContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementViewContext">;

  FleetPlayerData* CurrentFleet()
  { return Get<"CurrentFleet", FleetPlayerData*>(); }
  void Sort()
  { Call<"Sort">(); }
  int32_t CurrentIndex()
  { return Get<"CurrentIndex", int32_t>(); }
  void SetCurrentIndex(int32_t index)
  { Set<"CurrentIndex">(index); }
  void SetLastMovementWasToIncrement(bool increment)
  { Set<"LastMovementWasToIncrement">(increment); }
  int32_t ListCount()
  { return Get<"ListCount", int32_t>(); }
  il2cpp::List<FleetPlayerData*>* SortedIdleShips()
  { return Field<"SortedIdleShips", il2cpp::List<FleetPlayerData*>*>(); }
};

struct ShipManagementViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementViewController">;

  // m_context lives on the generic ViewController`1 base; field lookup walks the parents.
  ShipManagementViewContext* Context()
  { return Field<"m_context", ShipManagementViewContext*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
};

struct ShipManagementScreenStateViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementScreenStateViewController">;

  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  unity::Button* officerIconButton()
  { return Field<"_officerIconButton", unity::Button*>(); }
  unity::Button* swapShipButton()
  { return Field<"_swapShipButton", unity::Button*>(); }
};

struct AssignShipsWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "AssignShipsWidget">;

  InputFieldWidget* inputField()
  { return Field<"_inputField", InputFieldWidget*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  ButtonAndContextWrapper* assignButton()
  { return Field<"_assignButton", ButtonAndContextWrapper*>(); }
  SelectableList* selectableList()
  { return Field<"_selectableList", SelectableList*>(); }
};

struct ShipTileWidget : Widget<FleetPlayerData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipTileWidget">;
};

// Older clients lack this class; there the binding logs it once and ViewOwnHaven does nothing.
struct PlanetaryBaseManager : MonoSingleton<PlanetaryBaseManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlanetaryBase", "PlanetaryBaseManager">;

  static void ViewOwnHaven()
  {
    auto* manager  = Instance();
    auto* sections = Hub::Sections();
    if (!manager || !sections)
      return;

    // Preserve the game's Iconian Gateway prerequisite and its locked-building message.
    if (!manager->Call<"IsPlanetaryBaseCenterReady", bool>(true))
      return;
    // PlanetarySectionContext is reserved for visiting another player's Haven. Passing no context makes the
    // PlanetarySectionDirector initialize the complete local Haven state from the service cache.
    sections->TriggerSectionChange(SectionID::Starbase_Planetary, nullptr, false, false, true);
  }
};

struct ActivatedAbilityManager : MonoSingleton<ActivatedAbilityManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ActivatedAbilities", "ActivatedAbilityManager">;
  static constexpr int32_t RevealHostileVgerAbilityType = 86002;
  int32_t                  GetActivatedAbilityType(int64_t abilityId)
  { return Call<"GetActivatedAbilityType", int32_t>(abilityId); }
  bool CanUseAbility(FleetPlayerData* fleet, int64_t abilityId)
  { return Call<"CanUseAbility", bool>(fleet, abilityId); }
};

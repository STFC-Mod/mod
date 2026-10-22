#pragma once

// Player fleets and ships: fleet data and managers, the fleet bar, the action queue, and the ship management screens
// and dock.

#include "app.h"
#include "server.h"
#include "ui.h"

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

enum class DeployedFleetState : int32_t {
  Idle      = 0,
  Moving    = 1,
  Warping   = 2,
  Battling  = 3,
  Recall    = 4,
  Docked    = 5,
  PreBattle = 6,
};

enum class FleetRemovalReason : int32_t {
  None               = 0,
  Destroyed          = 1,
  ExitedActiveSystem = 2,
  Recalled           = 3,
  Hazard             = 4,
  Summoned           = 5,
};

// A location in the galaxy, a protobuf message.
struct NodeAddress : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "NodeAddress">;

  int64_t Galaxy()
  { return Get<"Galaxy", int64_t>(); }
  int64_t System()
  { return Get<"System", int64_t>(); }
  int64_t Planet()
  { return Get<"Planet", int64_t>(); }
  int32_t Instance()
  { return Get<"Instance", int32_t>(); }
};

// IDeployedFleet: the server's model behind a deployed fleet. Its class varies (DeployedFleet, DeployedFleetJSON),
// so members resolve on the runtime class.
struct DeployedFleetModel : il2cpp::Object {
  int64_t FleetId()
  { return Get<"FleetId", int64_t>(); }
};

struct FleetDeployedData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData">;

  HullSpec* Hull()
  { return Get<"Hull", HullSpec*>(); }
  // Nonexistent when the property is missing.
  DeployedFleetType FleetType()
  { return Get<"FleetType", DeployedFleetType>(); }
  int64_t Id()
  { return Get<"ID", int64_t>(); }
  DeployedFleetState CurrentState()
  { return Get<"CurrentState", DeployedFleetState>(); }
  FleetRemovalReason RemovalReason()
  { return Get<"RemovalReason", FleetRemovalReason>(); }
  bool IsDestroyed()
  { return Get<"IsDestroyed", bool>(); }
  bool CurrentlyBattling()
  { return Get<"CurrentlyBattling", bool>(); }
  bool IsPlanningRecallCourse()
  { return Get<"IsPlanningRecallCourse", bool>(); }
  // True for the player's own fleets.
  bool localPlayerFleet()
  { return Field<"_localPlayerFleet", bool>(); }
  NodeAddress* address()
  { return Field<"_address", NodeAddress*>(); }
  DeployedFleetModel* deploymentFleet()
  { return Field<"_deploymentFleet", DeployedFleetModel*>(); }
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
  int64_t Id()
  { return Get<"Id", int64_t>(); }
  NodeAddress* Address()
  { return Get<"Address", NodeAddress*>(); }
  int64_t Level()
  { return Get<"Level", int64_t>(); }
  bool HasShip()
  { return Get<"HasShip", bool>(); }
  int64_t HullId()
  { return Get<"HullId", int64_t>(); }
  // A forbidden tech or chaos tech item is fitted to the ship.
  bool HasAppliedTech();

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

// The hull a fleet flies.
struct BaseShip : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BaseShip">;

  int64_t Id()
  { return Get<"Id", int64_t>(); }
};

// An item slot on a ship, such as its forbidden tech slot.
struct EntitySlot : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "EntitySlot">;

  // The slot specs of a ship's forbidden tech and chaos tech slots, as the game's ship tech indicators read them.
  static constexpr int64_t kForbiddenTechSlot = 3502081615;
  static constexpr int64_t kChaosTechSlot     = 953301906;

  int64_t SlotSpecId()
  { return Get<"SlotSpecId", int64_t>(); }
  // Empty when nothing is fitted.
  il2cpp::Nullable<int64_t> SlotItemId()
  { return Get<"SlotItemId", il2cpp::Nullable<int64_t>>(); }
  ForbiddenTechSlotParams* forbiddenTechSlotParams()
  { return Get<"ForbiddenTechSlotParams", ForbiddenTechSlotParams*>(); }
};

struct ForbiddenTechManager : MonoSingleton<ForbiddenTechManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechManager">;

  bool TryGetActiveForbiddenTechSlot(int64_t ship_id, il2cpp::List<EntitySlot*>*& slots)
  { return Call<"TryGetActiveForbiddenTechSlot", bool>(ship_id, std::ref(slots)); }
  bool TryGetEntitySlotFromForbiddenTech(int64_t tech_id, EntitySlot*& slot)
  { return Call<"TryGetEntitySlotFromForbiddenTech", bool>(tech_id, std::ref(slot)); }

  // CachedService<FleetService>: a struct around the service, empty until something asks for it.
  struct CachedService {
    FleetService* cached_service;
  };

  // The fleet service. Until the cache holds it, the cache's Service getter looks it up and keeps it.
  FleetService* fleetService()
  {
    if (auto* service = Field<"_fleetServiceCache", CachedService>().cached_service)
      return service;
    return CallOnField<"_fleetServiceCache", CachedService, "get_Service", FleetService*>();
  }
};

inline bool FleetPlayerData::HasAppliedTech()
{
  auto*                      ship    = Get<"Ship", BaseShip*>();
  auto*                      manager = ForbiddenTechManager::Instance();
  il2cpp::List<EntitySlot*>* slots   = nullptr;
  if (!ship || !manager || !manager->TryGetActiveForbiddenTechSlot(ship->Id(), slots) || !slots)
    return false;
  for (auto* slot : *slots) {
    if (!slot)
      continue;
    const auto spec = slot->SlotSpecId();
    if ((spec == EntitySlot::kForbiddenTechSlot || spec == EntitySlot::kChaosTechSlot) && slot->SlotItemId().has_value)
      return true;
  }
  return false;
}

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

// Digit.PrimeServer.Models.TimerDataContext, read on its runtime class: the mod only needs the remaining time, and
// the class has moved between client builds.
struct TimerDataContext : il2cpp::Object {
  unity::TimeSpan RemainingTime()
  { return Get<"RemainingTime", unity::TimeSpan>(); }
};

struct ShipActionPanelLocalViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipActionPanelLocalViewController">;
  il2cpp::Array<int64_t>* abilityIDs()
  { return Field<"_abilityIDs", il2cpp::Array<int64_t>*>(); }
  il2cpp::Array<GenericButtonWidget*>* buttons()
  { return Field<"_buttons", il2cpp::Array<GenericButtonWidget*>*>(); }
  TimerDataContext* GetRemainingCooldown(int64_t abilityId)
  { return Call<"GetRemainingCooldown", TimerDataContext*>(abilityId); }
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
  // `target` is the IActionData the action is on, such as a FleetPlayerData.
  bool RequestAction(il2cpp::Object* target, ActionType type, int64_t index, ActionBehaviour behaviourMask,
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
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.FleetManagement", "FleetsManager">;

  // CachedService<FleetService>: a struct around the service, empty until something asks for it.
  struct CachedService {
    il2cpp::Object* cached_service;
  };

  // The fleet service is up. Until the cache holds it, the cache's HasService looks it up and keeps it.
  bool HasFleetService()
  {
    if (Field<"_fleetServiceCache", CachedService>().cached_service)
      return true;
    return CallOnField<"_fleetServiceCache", CachedService, "get_HasService", bool>();
  }
  FleetPlayerData* GetFleetPlayerData(int32_t index)
  { return Call<"GetFleetPlayerData", FleetPlayerData*>(index); }

  void RequestViewFleet(FleetPlayerData* fleetData, bool showSystemInfo = false)
  { Call<"RequestViewFleet">(fleetData, showSystemInfo); }

  FleetPlayerData* GetSelectedFleetData()
  { return Call<"GetSelectedFleetData", FleetPlayerData*>(); }
};

struct DeploymentManager : MonoSingleton<DeploymentManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "", "DeploymentManager">;

  bool HasInstantWarpAbility(FleetPlayerData* selectedFleet)
  { return Call<"HasInstantWarpAbility", bool>(selectedFleet); }
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

struct ShipManagementViewController : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementViewController">;

  // m_context lives on the generic ViewController`1 base; field lookup walks the parents.
  ShipManagementViewContext* Context()
  { return Field<"m_context", ShipManagementViewContext*>(); }
};

struct ShipManagementScreenStateViewController : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementScreenStateViewController">;

  unity::Button* officerIconButton()
  { return Field<"_officerIconButton", unity::Button*>(); }
  unity::Button* swapShipButton()
  { return Field<"_swapShipButton", unity::Button*>(); }
};

struct AssignShipsWidget : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "AssignShipsWidget">;

  InputFieldWidget* inputField()
  { return Field<"_inputField", InputFieldWidget*>(); }
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
  // ActivatedAbilityType.RevealHostileVger.
  static constexpr int32_t kRevealHostileVger = 86002;

  int32_t GetActivatedAbilityType(int64_t abilityId)
  { return Call<"GetActivatedAbilityType", int32_t>(abilityId); }
  bool CanUseAbility(FleetPlayerData* fleet, int64_t abilityId)
  { return Call<"CanUseAbility", bool>(fleet, abilityId); }
};

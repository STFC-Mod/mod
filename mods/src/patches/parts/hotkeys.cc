#include "config.h"
#include "patches/object_tracker.h"
#include "patches/ship_shortcut_hint.h"
#include "prime/fleet.h"

#include <spdlog/spdlog.h>
#include <spud/detour.h>


// Object Viewers
#include "prime/viewers.h"

#include "prime/fleet.h"
#include "prime/ui.h"
#include "prime/navigation.h"
#include "prime/chat.h"
#include "prime/inventory.h"
#include "prime/Hub.h"
#include "prime/KeyCode.h"

#include "patches/parts/action_queue.h"
#include "patches/key.h"
#include "patches/mapkey.h"
#include "patches/parts/daily_faction_bulk_claim.h"
#include "patches/parts/double_click_assign_ship.h"
#include "patches/parts/focus_search.h"
#include "str_utils.h"

#include <il2cpp/il2cpp.h>

#include "patches/screen_update_hook.h"

#ifdef _MODDBG
#include "patches/fleet_watch.h"
#endif

#include <EASTL/vector.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
// Classes only the shortcut hint badges touch.
struct InputAction : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.InputSystem", "UnityEngine.InputSystem", "InputAction">;

  Il2CppString* name()
  { return Get<"name", Il2CppString*>(); }
};

struct InputActionReference : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.InputSystem", "UnityEngine.InputSystem", "InputActionReference">;

  InputAction* action()
  { return Get<"action", InputAction*>(); }
};

struct ShortcutKeybindHint : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GameInput", "ShortcutKeybindHint">;

  InputActionReference* inputAction()
  { return Field<"_inputAction", InputActionReference*>(); }
  TextLocalizer* keyTextLocalizer()
  { return Field<"_keyTextLocalizer", TextLocalizer*>(); }
};
} // namespace

static bool reset_focus_next_frame            = false;
static int  show_info_pending                 = 0;
static int  show_officer_presets_pending      = 0;
static int  show_officer_swap_pending         = 0;
static int  show_ship_swap_pending            = 0;
static bool officer_presets_assignment_opened = false;

using UpdateShortcutHintTextFn = void(ShortcutKeybindHint*);

static UpdateShortcutHintTextFn* original_shortcut_hint_update_text = nullptr;
static bool                      initialize_actions_hook_ready      = false;
static bool                      shortcut_hints_ready               = false;

bool SetNativeShortcutHintsVisible(bool visible)
{
  auto* shortcuts_manager = ShortcutsManager::Instance();
  if (!shortcuts_manager) {
    spdlog::warn("[Hotkeys] native shortcut hints are unavailable");
    return false;
  }

  if (shortcuts_manager->ShowKeybindings() != visible)
    shortcuts_manager->SetShowKeybindings(visible);

  return true;
}

bool ToggleNativeShortcutHints()
{
  auto* shortcuts_manager = shortcut_hints_ready ? ShortcutsManager::Instance() : nullptr;
  if (!shortcuts_manager) {
    spdlog::warn("[Hotkeys] native shortcut hints are unavailable");
    return false;
  }

  // Match the native OnShowKeybindingsAction gate while avoiding a fabricated InputAction.CallbackContext.
  if (!ShortcutsManager::CanUseShortcuts())
    return false;

  shortcuts_manager->SetShowKeybindings(!shortcuts_manager->ShowKeybindings());

  return true;
}

struct NativeShortcutMapping {
  std::string_view action_name;
  GameFunction     primary;
  GameFunction     fallback = GameFunction::Max;
};

constexpr auto kNativeShortcutMappings = std::to_array<NativeShortcutMapping>({
    {"interior_view", GameFunction::ShowStationInterior},
    {"exterior_view", GameFunction::ShoWStationExterior},
    {"system_view", GameFunction::ShowSystem},
    {"galaxy_view", GameFunction::ShowGalaxy},
    {"events", GameFunction::ShowEvents},
    {"ship_a", GameFunction::SelectShip1},
    {"ship_b", GameFunction::SelectShip2},
    {"ship_c", GameFunction::SelectShip3},
    {"ship_d", GameFunction::SelectShip4},
    {"ship_e", GameFunction::SelectShip5},
    {"ship_f", GameFunction::SelectShip6},
    {"ship_g", GameFunction::SelectShip7},
    {"ship_h", GameFunction::SelectShip8},
    {"alliance", GameFunction::ShowAlliance},
    {"chat", GameFunction::ShowChat},
    {"side_chat", GameFunction::ShowChatSide1, GameFunction::ShowChatSide2},
    {"away_teams", GameFunction::ShowAwayTeam},
    {"missions", GameFunction::ShowMissions},
    {"daily_goals", GameFunction::ShowDaily},
    {"ship_locate", GameFunction::SelectCurrent},
    {"ship_manage", GameFunction::ShowShips},
    {"research", GameFunction::ShowResearch},
    {"consumables", GameFunction::ShowExoComp},
    {"ship_recall", GameFunction::ActionRecall},
    {"show_keybindings", GameFunction::ToggleShortcutHints},
    {"gifts", GameFunction::ShowGifts},
    {"help_alliance", GameFunction::ShowAllianceHelp},
    {"officers", GameFunction::ShowOfficers},
    {"factions", GameFunction::ShowFactions},
    {"items", GameFunction::ShowInventory},
    {"refinery", GameFunction::ShowRefinery},
    {"commanders", GameFunction::ShowCommander},
    {"challenges", GameFunction::ShowQTrials},
});

consteval bool NativeShortcutActionsAreUnique()
{
  for (size_t index = 0; index < kNativeShortcutMappings.size(); ++index) {
    for (size_t other = index + 1; other < kNativeShortcutMappings.size(); ++other) {
      if (kNativeShortcutMappings[index].action_name == kNativeShortcutMappings[other].action_name) {
        return false;
      }
    }
  }
  return true;
}

static_assert(NativeShortcutActionsAreUnique());

constexpr const NativeShortcutMapping* NativeShortcutForAction(std::string_view action_name)
{
  for (const auto& mapping : kNativeShortcutMappings) {
    if (mapping.action_name == action_name) {
      return &mapping;
    }
  }
  return nullptr;
}

static_assert(NativeShortcutForAction("interior_view")->primary == GameFunction::ShowStationInterior);
static_assert(NativeShortcutForAction("side_chat")->fallback == GameFunction::ShowChatSide2);
static_assert(NativeShortcutForAction("unknown") == nullptr);

void ShortcutKeybindHint_UpdateText_Hook(auto original, ShortcutKeybindHint* _this)
{
  if (!_this || !shortcut_hints_ready) {
    return original(_this);
  }

  auto* text_localizer = _this->keyTextLocalizer();
  if (!text_localizer) {
    return original(_this);
  }

  // UpdateText does not clear an existing localization override. Clear ours first so native, disabled, and unknown
  // actions cannot retain a stale mod badge.
  text_localizer->ClearTextOverride();
  original(_this);

  const auto& config = Config::Get();
  if (!config.hotkeys_enabled || config.use_scopely_hotkeys) {
    return;
  }

  auto* input_action_reference = _this->inputAction();
  if (!input_action_reference) {
    return;
  }

  auto* action = input_action_reference->action();
  auto* name   = action ? action->name() : nullptr;
  if (!name) {
    return;
  }

  const auto  action_name = to_string(name);
  const auto* mapping     = NativeShortcutForAction(action_name);
  if (!mapping) {
    text_localizer->OverrideLocalizedText(il2cpp_string_new("-"));
    return;
  }

  auto hint_action = mapping->primary;
  if (hint_action >= GameFunction::SelectShip1 && hint_action <= GameFunction::SelectShip8) {
    // Use the same context array as FleetBarViewController::RequestSelect(int, bool), not the native dock index.
    auto* bar     = ObjectFinder<FleetBarViewController>::Get();
    auto* context = bar ? bar->CanvasContext() : nullptr;
    auto* fleets  = context ? context->FleetData() : nullptr;
    hint_action   = ShipShortcutHintAction(hint_action, fleets ? fleets->size() : 0, [fleets](std::size_t position) {
      auto* fleet = (*fleets)[position];
      return fleet ? fleet->Index() : -1;
    });
  }
  auto shortcut = hint_action == GameFunction::Max ? std::string{} : MapKey::GetShortcutHint(hint_action);
  if (shortcut.empty() && mapping->fallback != GameFunction::Max) {
    shortcut = MapKey::GetShortcutHint(mapping->fallback);
  }
  if (shortcut.empty()) {
    shortcut = "-";
  }
  text_localizer->OverrideLocalizedText(il2cpp_string_new(shortcut.c_str()));
}

void ShortcutKeybindHint_UpdateVisibility_Hook(auto original, ShortcutKeybindHint* _this, bool visible)
{
  original(_this, visible);
  if (visible && shortcut_hints_ready && original_shortcut_hint_update_text) {
    ShortcutKeybindHint_UpdateText_Hook(original_shortcut_hint_update_text, _this);
  }
}

// UnityEngine.InputSystem.InputAction.CallbackContext, passed empty. Generated code takes it by value with the
// platform's C++ ABI, so this layout must match the managed struct.
struct InputActionCallbackContext {
  void*   state;
  int32_t action_index;
};

static_assert(sizeof(InputActionCallbackContext) == 16);

// Calls one of ShortcutsManager's On*Action(InputAction.CallbackContext) handlers. A missing handler is reported
// once by the binding layer and does nothing.
template <il2cpp::Name Action> bool InvokeNativeShortcut(const char* action_name)
{
  auto* shortcuts_manager = ShortcutsManager::Instance();
  if (!shortcuts_manager) {
    spdlog::warn("[Hotkeys] native {} shortcut is unavailable", action_name);
    return false;
  }

  shortcuts_manager->Call<Action>(InputActionCallbackContext{});
  return true;
}

bool force_space_action_next_frame = false;

void     ChangeNavigationSection(SectionID sectionID);
void     ExecuteSpaceAction(FleetBarViewController* fleet_bar);
bool     DidExecuteRecall(FleetBarViewController* fleet_bar);
bool     DidExecuteRepair(FleetBarViewController* fleet_bar);
HullType GetHullTypeFromBattleTarget(BattleTargetData* context);
void     GotoSection(SectionID sectionID, void* screen_data = nullptr);
bool     CanHideViewers();
bool     DidHideViewers();

void CycleAutoConfirmInstantWarp(Config& config)
{
  const char* state = nullptr;
  switch (config.auto_confirm_instant_warp) {
    case InstantWarpConfirmation::None:
      config.auto_confirm_instant_warp = InstantWarpConfirmation::Warp;
      state                            = "warp";
      break;
    case InstantWarpConfirmation::Warp:
      config.auto_confirm_instant_warp = InstantWarpConfirmation::Jump;
      state                            = "jump";
      break;
    case InstantWarpConfirmation::Jump:
      config.auto_confirm_instant_warp = InstantWarpConfirmation::None;
      state                            = "none";
      break;
  }

  spdlog::info("Auto-confirm instant warp set to {}", state);
}

bool MoveOfficerCanvas(bool goLeft)
{
  auto selectors = ObjectFinder<ElementSelectorViewController>::GetAll();
  if (selectors.empty()) {
    return false;
  }

  bool acted = false;
  for (auto selector : selectors) {
    if (!selector || !selector->isActiveAndEnabled()) {
      continue;
    }
    if (goLeft) {
      selector->PressDecrement();
    } else {
      selector->PressIncrement();
    }
    acted = true;
  }

  return acted;
}

bool MoveArtifactCanvas(bool goLeft)
{
  bool acted = false;
  for (auto controller : ObjectFinder<ArtifactHallDetailsViewController>::GetAll()) {
    if (!controller) {
      spdlog::trace("MoveArtifactCanvas({}) - No controller", (int)goLeft);
      continue;
    }

    if (!controller->isActiveAndEnabled()) {
      spdlog::trace("MoveArtifactCanvas({}) - Controller not active", (int)goLeft);
      continue;
    }

    if (goLeft) {
      spdlog::debug("MoveArtifactCanvas({}) - Pressing Left Arrow", (int)goLeft);
      controller->PressLeftArrow();
    } else {
      spdlog::debug("MoveArtifactCanvas({}) - Pressing Right Arrow", (int)goLeft);
      controller->PressRightArrow();
    }
    acted = true;
  }

  return acted;
}

bool IsRealShipIndex(SelectableList* list, int32_t index)
{
  auto* item = list->DataItem(index);
  if (!item) return false;
  return static_cast<FleetPlayerData*>(item)->HasShip();
}

bool MoveShipSelectionInDock(bool goLeft)
{
  if (!Config::Get().arrow_keys_to_select_ship) {
    return false;
  }

  bool acted = false;
  for (auto widget : ObjectFinder<AssignShipsWidget>::GetAll()) {
    if (!widget) continue;

    auto canvas = GetCanvasControllerFromComponent(widget);
    if (!canvas || !canvas->Visible() || !widget->isActiveAndEnabled()) {
      spdlog::trace("MoveShipSelectionInDock({}) - widget={} not visible/active", (int)goLeft, (void*)widget);
      continue;
    }

    auto* list = widget->selectableList();
    if (!list) {
      spdlog::trace("MoveShipSelectionInDock({}) - widget={} has no _selectableList", (int)goLeft, (void*)widget);
      continue;
    }

    const auto count = list->Count();
    if (count <= 0) {
      spdlog::trace("MoveShipSelectionInDock({}) - list={} count={}", (int)goLeft, (void*)list, count);
      continue;
    }

    const auto step = goLeft ? -1 : 1;
    auto       newIndex = list->SelectedIndex() + step;
    while (newIndex >= 0 && newIndex < count && !IsRealShipIndex(list, newIndex)) {
      newIndex += step;
    }
    if (newIndex < 0 || newIndex >= count) {
      spdlog::trace("MoveShipSelectionInDock({}) - no further real ship in that direction", (int)goLeft);
      continue;
    }

    spdlog::debug("MoveShipSelectionInDock({}) - selecting index {} of {}", (int)goLeft, newIndex, count);
    list->RequestSelect(newIndex);
    acted = true;
  }

  return acted;
}

void UpdateManageShipDockSelection()
{
  static uint64_t last_fleet_id = 0;
  static auto next_update = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now < next_update)
    return;
  next_update = now + std::chrono::milliseconds(100);
  auto* sections = Hub::get_SectionManager();
  if (!Config::Get().dock_syncmanagement || !sections
      || sections->CurrentSection() != SectionID::ShipManagement_Details) {
    last_fleet_id = 0;
    return;
  }
  for (auto* controller : ObjectFinder<ShipManagementViewController>::GetAll()) {
    if (!controller || !controller->isActiveAndEnabled())
      continue;
    auto* canvas = GetCanvasControllerFromComponent(controller);
    if (!canvas || !canvas->Visible())
      continue;
    auto* context = controller->Context();
    auto* fleet = context ? context->CurrentFleet() : nullptr;
    auto* manager = FleetsManager::Instance();
    if (!fleet || !fleet->HasShip() || !manager || !manager->HasFleetService())
      continue;
    const auto index = fleet->Index();
    if (index < 0)
      continue;
    // Exclude unassigned and synthetic fleets; management list order is not dock order.
    auto* dock_fleet = manager->GetFleetPlayerData(index);
    if (!dock_fleet || dock_fleet->Id() != fleet->Id())
      continue;
    if (last_fleet_id == fleet->Id())
      return;
    auto* bar = ObjectFinder<FleetBarViewController>::Get();
    if (!bar)
      return;
    if (!bar->IsIndexSelected(index)) {
      // Use the bar's selection flow to update both the highlight and fleet panel.
      bar->RequestSelect(index);
      if (!bar->IsIndexSelected(index))
        return; // Retry next update if native selection was blocked.
      spdlog::debug("[ManageShips] selected dock {} for fleet {}", index + 1, fleet->Id());
    }
    last_fleet_id = fleet->Id();
    return;
  }
  last_fleet_id = 0;
}

void UpdateExpiredAbilityButtons()
{
  static auto next_update = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now < next_update)
    return;
  next_update = now + std::chrono::milliseconds(250);
  auto* bar = ObjectFinder<FleetBarViewController>::Get();
  auto* controller = bar ? bar->fleetPanelController() : nullptr;
  auto* fleet = controller ? controller->fleet() : nullptr;
  if (!fleet || (static_cast<int>(fleet->CurrentState()) & static_cast<int>(FleetState::CanActivateAbility)) == 0)
    return;
  auto* manager = ActivatedAbilityManager::Instance();
  auto* panel = controller->shipActionPanelLocalViewController();
  auto* canvas = panel ? GetCanvasControllerFromComponent(panel) : nullptr;
  if (!manager || !canvas || !canvas->Visible() || panel->HasShipAnActiveAbilityWithActiveEffect())
    return;
  auto* ids = panel->abilityIDs();
  auto* buttons = panel->buttons();
  if (!ids || !buttons)
    return;
  for (size_t index = 0; index < ids->size() && index < buttons->size(); ++index) {
    auto* button = (*buttons)[index];
    auto* context = button ? button->Context() : nullptr;
    if (!button || !button->isActiveAndEnabled() || !context || context->Interactable())
      continue;
    const auto ability_id = (*ids)[index];
    auto* timer = panel->GetRemainingCooldown(ability_id);
    // TimeSpan is an eight-byte value in 100 ns ticks. Repair at -1 second or below.
    if (timer && timer->RemainingTime().ticks <= -10000000 && manager->CanUseAbility(fleet, ability_id))
      context->SetInteractable(true);
  }
}

bool MoveDockInManagementView(bool goLeft)
{
  if (!Config::Get().arrow_keys_to_select_ship) {
    return false;
  }

  auto* section_manager = Hub::get_SectionManager();
  if (!section_manager || section_manager->CurrentSection() != SectionID::ShipManagement_Details) {
    return false;
  }

  bool acted = false;
  for (auto controller : ObjectFinder<ShipManagementViewController>::GetAll()) {
    if (!controller || !controller->isActiveAndEnabled()) {
      continue;
    }

    auto canvas = GetCanvasControllerFromComponent(controller);
    if (!canvas || !canvas->Visible()) {
      spdlog::trace("MoveDockInManagementView({}) - controller={} not visible", (int)goLeft, (void*)controller);
      continue;
    }

    auto* context = controller->Context();
    if (!context) {
      spdlog::trace("MoveDockInManagementView({}) - controller={} has no context", (int)goLeft,
                    (void*)controller);
      continue;
    }

    const int32_t count   = context->ListCount();
    const int32_t current = context->CurrentIndex();
    if (count <= 1 || current < 0) {
      spdlog::trace("MoveDockInManagementView({}) - context={} index={} count={}", (int)goLeft, (void*)context,
                    current, count);
      continue;
    }

    const auto next = current + (goLeft ? -1 : 1);
    if (next < 0 || next >= count) {
      spdlog::trace("MoveDockInManagementView({}) - no further dock (index={} count={})", (int)goLeft, current, count);
      continue;
    }

    spdlog::debug("MoveDockInManagementView({}) - switching dock {} -> {}", (int)goLeft, current, next);
    context->SetLastMovementWasToIncrement(!goLeft);
    context->SetCurrentIndex(next);
    acted = true;
  }

  return acted;
}

bool PressShipSwapButton()
{
  for (auto controller : ObjectFinder<ShipManagementScreenStateViewController>::GetAll()) {
    if (!controller) {
      continue;
    }

    auto canvas = GetCanvasControllerFromComponent(controller);
    if (!canvas || !canvas->Visible() || !controller->isActiveAndEnabled()) {
      continue;
    }

    auto* button = controller->swapShipButton();
    if (button) {
      spdlog::info("[ShipSwap] pressing swap button controller={} button={}", static_cast<void*>(controller),
                   static_cast<void*>(button));
      button->Press();
      return true;
    }
  }

  return false;
}

bool PressOfficerSwapButton()
{
  for (auto controller : ObjectFinder<ShipManagementScreenStateViewController>::GetAll()) {
    if (!controller) {
      continue;
    }

    auto canvas = GetCanvasControllerFromComponent(controller);
    if (!canvas || !canvas->Visible() || !controller->isActiveAndEnabled()) {
      continue;
    }

    auto* button = controller->officerIconButton();
    if (button) {
      spdlog::info("[OfficerSwap] pressing officer button controller={} button={}", static_cast<void*>(controller),
                   static_cast<void*>(button));
      button->Press();
      return true;
    }
  }

  return false;
}

bool PressOfficerPresetsButton()
{
  for (auto controller : ObjectFinder<OfficerAssignmentViewController>::GetAll()) {
    if (!controller) {
      continue;
    }

    auto canvas = GetCanvasControllerFromComponent(controller);
    if (!canvas || !canvas->Visible() || !controller->isActiveAndEnabled()) {
      continue;
    }

    auto* button = controller->officerPresetsButton();
    auto* listener = button ? button->SemaphoreListener() : nullptr;
    auto* native_button = listener ? listener->TheButton() : nullptr;
    if (native_button) {
      spdlog::info("[OfficerPresets] pressing presets button controller={} button={}",
                   static_cast<void*>(controller), static_cast<void*>(button));
      native_button->Press();
      return true;
    }
  }

  return false;
}

void ScreenManager_Update_Hook(auto original, ScreenManager* _this)
{
  dispatch_screen_manager_update_callbacks();
  if (!Config::Get().installHotkeyHooks) {
    return original(_this);
  }

  UpdateManageShipDockSelection();
  UpdateExpiredAbilityButtons();

  // This function is called every frame to update the screen manager.
  // Create a global clock to detect time elapsed
  static std::chrono::time_point<std::chrono::steady_clock> select_clock             = std::chrono::steady_clock::now();
  static int32_t                                            last_ship_select_request = -1;

  Key::ResetCache();

  if (MapKey::IsDown(GameFunction::DisableHotKeys)) {
    if (shortcut_hints_ready) {
      SetNativeShortcutHintsVisible(false);
    }
    Config::Get().hotkeys_enabled = false;
    spdlog::warn("Setting hotkeys to DISABLED");
    return;
  } else if (MapKey::IsDown(GameFunction::EnableHotKeys)) {
    Config::Get().hotkeys_enabled = true;
    spdlog::warn("Setting hotkeys to ENABLED");
    return;
  }

  // Per-frame driver - must run every frame, before the hotkey-mode
  // early-returns below (it must also run in Scopely-hotkey mode and
  // while hotkeys are toggled off).
  AssignShipEnterKeyUpdate();

  if (show_officer_presets_pending > 0) {
    if (PressOfficerPresetsButton()) {
      show_officer_presets_pending       = 0;
      officer_presets_assignment_opened = false;
      return;
    }

    if (!officer_presets_assignment_opened && PressOfficerSwapButton()) {
      officer_presets_assignment_opened = true;
    }

    if (--show_officer_presets_pending == 0) {
      officer_presets_assignment_opened = false;
      spdlog::warn("[Hotkeys] timed out waiting for the Officer Presets button");
    }
  }

  if (show_officer_swap_pending > 0) {
    if (PressOfficerSwapButton()) {
      show_officer_swap_pending = 0;
      return;
    }

    if (--show_officer_swap_pending == 0) {
      spdlog::warn("[Hotkeys] timed out waiting for the Officer Swap button");
    }
  }

  if (show_ship_swap_pending > 0) {
    if (PressShipSwapButton()) {
      show_ship_swap_pending = 0;
      return;
    }

    if (--show_ship_swap_pending == 0) {
      spdlog::warn("[Hotkeys] timed out waiting for the Ship Swap button");
    }
  }

  if (Config::Get().use_scopely_hotkeys && Config::Get().hotkeys_enabled) {
    return original(_this);
  }

  if (!Config::Get().hotkeys_enabled) {
    return;
  }

  if (shortcut_hints_ready && MapKey::IsDown(GameFunction::ToggleShortcutHints)) {
    ToggleNativeShortcutHints();
    return;
  }

  const auto is_in_chat = Hub::IsInChat();
  const auto config     = &Config::Get();

  if (MapKey::IsDown(GameFunction::Restart)) {
    spdlog::info("Clearing localisation cache and restarting");
    LanguageManager::ClearCache();
    Hub::get_App()->Reload();
    return;
  }

#ifdef _WIN32
  if (MapKey::IsDown(GameFunction::Quit)) {
    TerminateProcess(GetCurrentProcess(), 1);
  }
#elif defined(__APPLE__)
  if (MapKey::IsDown(GameFunction::Quit)) {
    Hub::get_App()->Quit();
    return;
  }
#endif

  int32_t ship_select_request = -1;
  if (MapKey::IsDown(GameFunction::SelectShip1)) {
    ship_select_request = 0;
  } else if (MapKey::IsDown(GameFunction::SelectShip2)) {
    ship_select_request = 1;
  } else if (MapKey::IsDown(GameFunction::SelectShip3)) {
    ship_select_request = 2;
  } else if (MapKey::IsDown(GameFunction::SelectShip4)) {
    ship_select_request = 3;
  } else if (MapKey::IsDown(GameFunction::SelectShip5)) {
    ship_select_request = 4;
  } else if (MapKey::IsDown(GameFunction::SelectShip6)) {
    ship_select_request = 5;
  } else if (MapKey::IsDown(GameFunction::SelectShip7)) {
    ship_select_request = 6;
  } else if (MapKey::IsDown(GameFunction::SelectShip8)) {
    ship_select_request = 7;
  }

  if (ship_select_request != -1 && !Key::IsInputFocused()) {

    // TODO: Shift+ship towing with the Discovery never ran on earlier clients (its PlanCourse and
    // SetTowRequest bindings had drifted) and has not been tested in game since they were fixed. It sends a
    // real tow request, so keep it off until someone verifies it. Shift+ship falls through as a no-op meanwhile.
    constexpr bool kEnableDiscoveryTow = false;
    if (Key::HasShift()) {
      if constexpr (kEnableDiscoveryTow) {
        FleetPlayerData* foundDisco = nullptr;
        for (int discoIdx = 0; discoIdx < 10; ++discoIdx) {
          auto fleetPlayerData = FleetsManager::Instance()->GetFleetPlayerData(discoIdx);
          if (fleetPlayerData && fleetPlayerData->Hull() && fleetPlayerData->Hull()->Id() == 1307832955) {
            foundDisco = fleetPlayerData;
            break;
          }
        }

        if (foundDisco) {
          auto* towed      = FleetsManager::Instance()->GetFleetPlayerData(ship_select_request);
          auto* deployment = DeploymentManger::Instance();
          auto* plannedCourse =
              towed && deployment ? deployment->PlanCourse(towed, foundDisco->Address(), Vector3::zero(), nullptr,
                                                           nullptr, nullptr)
                                  : nullptr;
          if (plannedCourse) {
            // Drain the planning coroutine synchronously. The bound stops a coroutine that keeps yielding
            // (it waits on something only the game loop advances) from hanging the frame.
            for (int step = 0; step < 1000 && plannedCourse->MoveNext(); ++step) {
            }
            deployment->SetTowRequest(towed->Id(), foundDisco->Id());
          }
        }
      }
    } else {
      auto fleet_bar  = ObjectFinder<FleetBarViewController>::Get();
      auto can_locate = !config->disable_preview_locate || !CanHideViewers();
      if (fleet_bar) {
        std::chrono::time_point<std::chrono::steady_clock> select_now = std::chrono::steady_clock::now();
        std::chrono::milliseconds                          select_diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(select_now - select_clock);
        spdlog::debug("select_diff was {}ms", select_diff.count());
        if (can_locate && ship_select_request == last_ship_select_request
            && fleet_bar->IsIndexSelected(ship_select_request)
            && select_diff < std::chrono::milliseconds((int)Config::Get().select_timer)) {
          auto fleet_controller = fleet_bar->fleetPanelController();
          auto fleet            = fleet_controller ? fleet_controller->fleet() : nullptr;
          if (!fleet) {
            return;
          }
          if (NavigationSectionManager::Instance() && NavigationSectionManager::Instance()->SNavigationManager()) {
            NavigationSectionManager::Instance()->SNavigationManager()->HideInteraction();
          }
          FleetsManager::Instance()->RequestViewFleet(fleet, true);
        } else {
          fleet_bar->RequestSelect(ship_select_request);
        }

        last_ship_select_request = ship_select_request;
        select_clock             = select_now;
        return;
      }
    }
  }

  if (Key::Pressed(KeyCode::Escape) && (Key::IsInputFocused() || Hub::IsInChat())) {
    // This fixes issues with detecting when an input is selected
    // As the game usually doesn't clear this when using Escape, only when
    // pressing the back button with the mouse...
    return Key::ClearInputFocus();
  }

  if (!is_in_chat) {
    if (!Key::IsInputFocused()) {
      if (MapKey::IsDown(GameFunction::SelectCurrent)) {
        auto fleet_bar = ObjectFinder<FleetBarViewController>::Get();
        if (fleet_bar) {
          auto fleet_controller = fleet_bar->fleetPanelController();
          auto fleet            = fleet_controller ? fleet_controller->fleet() : nullptr;
          if (fleet) {
            if (NavigationSectionManager::Instance() && NavigationSectionManager::Instance()->SNavigationManager()) {
              NavigationSectionManager::Instance()->SNavigationManager()->HideInteraction();
            }
            FleetsManager::Instance()->RequestViewFleet(fleet, true);
            return;
          }
        }
      }

      if ((MapKey::IsDown(GameFunction::ToggleQueue))) {
        config->queue_enabled = !config->queue_enabled;
        // Forget stale recovery authorizations; existing native target queues keep processing.
        ResetQueueRecoveryTracking();
        return;
      }

      if ((MapKey::IsDown(GameFunction::ShowChat) || MapKey::IsDown(GameFunction::ShowChatSide1)
           || MapKey::IsDown(GameFunction::ShowChatSide2))) {
        if (auto chat_manager = ChatManager::Instance(); chat_manager) {
          if (chat_manager->IsSideChatOpen()) {
            if (auto view_controller = ObjectFinder<FullScreenChatViewController>::Get(); view_controller) {
              if (auto message_list = view_controller->messageList(); message_list) {
                if (auto message_field = message_list->inputField(); message_field) {
                  message_field->ActivateInputField();
                }
              }
            }
          } else if (MapKey::IsDown(GameFunction::ShowChatSide1) || MapKey::IsDown(GameFunction::ShowChatSide2)) {
            chat_manager->OpenChannel(ChatChannelCategory::Alliance, ChatViewMode::Side);
          } else {
            chat_manager->OpenChannel(ChatChannelCategory::Alliance, ChatViewMode::Fullscreen);
          }
        }
      }

      if (MapKey::IsDown(GameFunction::MoveLeft)) {
        auto const result = MoveDockInManagementView(true) || MoveShipSelectionInDock(true) || MoveArtifactCanvas(true)
                            || MoveOfficerCanvas(true);
        if (result) {
          return;
        }
      }

      if (MapKey::IsDown(GameFunction::MoveRight)) {
        auto const result = MoveDockInManagementView(false) || MoveShipSelectionInDock(false)
                            || MoveArtifactCanvas(false) || MoveOfficerCanvas(false);
        if (result) {
          return;
        }
      }

      if (Config::Get().focus_search && MapKey::IsDown(GameFunction::FocusSearch)) {
        if (FocusSearchBox()) {
          return;
        }
      }

      if (MapKey::IsDown(GameFunction::ShowQTrials)) {
        return GotoSection(SectionID::ChallengeSelection);
      } else if (MapKey::IsDown(GameFunction::ShowBookmarks)) {
        auto bookmark_manager = BookmarksManager::Instance();
        if (bookmark_manager) {
          return bookmark_manager->ViewBookmarks();
        }
        return GotoSection(SectionID::Bookmarks_Main);
      } else if (MapKey::IsDown(GameFunction::ShowLookup)) {
        auto bookmark_manager = BookmarksManager::Instance();
        if (bookmark_manager) {
          bookmark_manager->ViewCoordinateSearch();
          return;
        }
        spdlog::warn("[ShowLookup] BookmarksManager instance not available, falling back to main bookmarks");
        GotoSection(SectionID::Bookmarks_Main);
        return;
      } else if (MapKey::IsDown(GameFunction::ShowRefinery)) {
        return GotoSection(SectionID::Shop_Refining_List);
      } else if (MapKey::IsDown(GameFunction::ShowFactions)) {
        return GotoSection(SectionID::Shop_MainFactions);
      } else if (MapKey::IsDown(GameFunction::ShoWStationExterior)) {
        return GotoSection(SectionID::Starbase_Exterior);
      } else if (MapKey::IsDown(GameFunction::NativeShortcutGalaxy)) {
        InvokeNativeShortcut<"OnGalaxyAction">("Galaxy");
        return;
      } else if (MapKey::IsDown(GameFunction::ShowGalaxy)) {
        return ChangeNavigationSection(SectionID::Navigation_Galaxy);
      } else if (MapKey::IsDown(GameFunction::ShowStationInterior)) {
        return GotoSection(SectionID::Starbase_Interior);
      } else if (MapKey::IsDown(GameFunction::ShowHaven)) {
        return PlanetaryBaseManager::ViewOwnHaven();
      } else if (MapKey::IsDown(GameFunction::ShowSystem)) {
        return ChangeNavigationSection(SectionID::Navigation_System);
      } else if (MapKey::IsDown(GameFunction::ShowArtifacts)) {
        return GotoSection(SectionID::ArtifactHall_Inventory);
      } else if (MapKey::IsDown(GameFunction::ShowShipConstruction)) {
        InvokeNativeShortcut<"OnShipsAction">("Ship Construction");
        return;
      } else if (MapKey::IsDown(GameFunction::ShowOfficerSwap)) {
        auto* section_manager = Hub::get_SectionManager();
        spdlog::info("[OfficerSwap] shortcut pressed current_section={}",
                     section_manager ? static_cast<int32_t>(section_manager->CurrentSection()) : 0);
        if (PressOfficerSwapButton()) {
          return;
        }

        auto* fleet_bar        = ObjectFinder<FleetBarViewController>::Get();
        auto* fleet_controller = fleet_bar ? fleet_bar->fleetPanelController() : nullptr;
        auto* fleet            = fleet_controller ? fleet_controller->fleet() : nullptr;
        if (fleet) {
          spdlog::info("[OfficerSwap] requesting Manage Ships before officer assignment fleet={}",
                       static_cast<void*>(fleet));
          fleet_controller->RequestAction(fleet, ActionType::Manage, 0, ActionBehaviour::Default);
          show_officer_swap_pending = 300;
        } else {
          spdlog::warn("[Hotkeys] unable to open Manage Ships for Officer Swap");
        }
        return;
      } else if (MapKey::IsDown(GameFunction::ShowShipSwap)) {
        auto* section_manager = Hub::get_SectionManager();
        spdlog::info("[ShipSwap] shortcut pressed current_section={}",
                     section_manager ? static_cast<int32_t>(section_manager->CurrentSection()) : 0);
        if (PressShipSwapButton()) {
          return;
        }

        auto* fleet_bar        = ObjectFinder<FleetBarViewController>::Get();
        auto* fleet_controller = fleet_bar ? fleet_bar->fleetPanelController() : nullptr;
        auto* fleet            = fleet_controller ? fleet_controller->fleet() : nullptr;
        if (fleet) {
          spdlog::info("[ShipSwap] requesting Manage Ships before swap fleet={}", static_cast<void*>(fleet));
          fleet_controller->RequestAction(fleet, ActionType::Manage, 0, ActionBehaviour::Default);
          show_ship_swap_pending = 300;
        } else {
          spdlog::warn("[Hotkeys] unable to open Manage Ships for Ship Swap");
        }
        return;
      } else if (MapKey::IsDown(GameFunction::ShowShields)) {
        InvokeNativeShortcut<"OnPeaceShieldAction">("Peace Shields");
        return;
      } else if (MapKey::IsDown(GameFunction::ShowBattlelogs)) {
        InvokeNativeShortcut<"OnInboxAction">("Battle Reports");
        return;
      } else if (MapKey::IsDown(GameFunction::ShowInventory)) {
        return GotoSection(SectionID::InventoryList);
      } else if (MapKey::IsDown(GameFunction::ShowMissions)) {
        return GotoSection(SectionID::Missions_AcceptedList);
      } else if (MapKey::IsDown(GameFunction::ShowResearch)) {
        return GotoSection(SectionID::Research_LandingPage);
      } else if (MapKey::IsDown(GameFunction::ShowScrapYard)) {
        return GotoSection(SectionID::ShipScrapping_List);
      } else if (MapKey::IsDown(GameFunction::ShowOfficers)) {
        return GotoSection(SectionID::OfficerInventory);
      } else if (MapKey::IsDown(GameFunction::ShowOfficerPresets)) {
        if (PressOfficerPresetsButton()) {
          return;
        }

        officer_presets_assignment_opened = PressOfficerSwapButton();
        if (!officer_presets_assignment_opened) {
          auto* fleet_bar        = ObjectFinder<FleetBarViewController>::Get();
          auto* fleet_controller = fleet_bar ? fleet_bar->fleetPanelController() : nullptr;
          auto* fleet            = fleet_controller ? fleet_controller->fleet() : nullptr;
          if (fleet) {
            spdlog::info("[OfficerPresets] requesting Manage Ships before officer presets fleet={}",
                         static_cast<void*>(fleet));
            fleet_controller->RequestAction(fleet, ActionType::Manage, 0, ActionBehaviour::Default);
          } else {
            spdlog::warn("[Hotkeys] unable to open Manage Ships for Officer Presets");
            return;
          }
        }
        show_officer_presets_pending = 300;
        return;
      } else if (MapKey::IsDown(GameFunction::ShowCommander)) {
        // TODO: Does not work properly, defaults to first FleetCommander (spock, rather than selected fleet
        // commander)
        return GotoSection(SectionID::FleetCommander_Management);
      } else if (MapKey::IsDown(GameFunction::ShowAwayTeam)) {
        return GotoSection(SectionID::Missions_AwayTeamsList);
      } else if (MapKey::IsDown(GameFunction::NativeShortcutEvents)) {
        InvokeNativeShortcut<"OnEventsAction">("Events");
        return;
      } else if (MapKey::IsDown(GameFunction::ShowEvents)) {
        return GotoSection(SectionID::Tournament_Group_Selection);
      } else if (MapKey::IsDown(GameFunction::ShowExoComp)) {
        return GotoSection(SectionID::Consumables);
      } else if (MapKey::IsDown(GameFunction::ShowDaily)) {
        return GotoSection(SectionID::Missions_DailyGoals);
      } else if (MapKey::IsDown(GameFunction::ShowGifts)) {
        return GotoSection(SectionID::Shop_List);
      } else if (MapKey::IsDown(GameFunction::ShowAlliance)) {
        return GotoSection(SectionID::Alliance_Main);
      } else if (MapKey::IsDown(GameFunction::ShowAllianceHelp)) {
        return GotoSection(SectionID::Alliance_Help);
      } else if (MapKey::IsDown(GameFunction::ShowAllianceArmada)) {
        return GotoSection(SectionID::Alliance_Armadas);
      } else if (MapKey::IsDown(GameFunction::ShowSettings)) {
        return GotoSection(SectionID::GameSettings);
      } else if (MapKey::IsPressed(GameFunction::UiScaleUp)) {
        config->AdjustUiScale(true);
      } else if (MapKey::IsPressed(GameFunction::UiScaleDown)) {
        config->AdjustUiScale(false);
      } else if (MapKey::IsPressed(GameFunction::UiShipScaleUp)) {
        config->AdjustUiShipScale(true);
      } else if (MapKey::IsPressed(GameFunction::UiShipScaleDown)) {
        config->AdjustUiShipScale(false);
      } else if (MapKey::IsPressed(GameFunction::UiViewerScaleUp)) {
        config->AdjustUiViewerScale(true);
      } else if (MapKey::IsPressed(GameFunction::UiViewerScaleDown)) {
        config->AdjustUiViewerScale(false);
      } else if (MapKey::IsDown(GameFunction::ToggleAutoConfirmInstantWarp)) {
        CycleAutoConfirmInstantWarp(*config);
      } else if (MapKey::IsDown(GameFunction::TogglePreviewLocate)) {
        config->disable_preview_locate = !config->disable_preview_locate;
      } else if (MapKey::IsDown(GameFunction::TogglePreviewRecall)) {
        config->disable_preview_recall = !config->disable_preview_recall;
      } else if (MapKey::IsDown(GameFunction::ToggleCargoDefault)) {
        config->show_cargo_default = !config->show_cargo_default;
      } else if (MapKey::IsDown(GameFunction::ToggleCargoPlayer)) {
        config->show_player_cargo = !config->show_player_cargo;
      } else if (MapKey::IsDown(GameFunction::ToggleCargoStation)) {
        config->show_station_cargo = !config->show_station_cargo;
      } else if (MapKey::IsDown(GameFunction::ToggleCargoHostile)) {
        config->show_hostile_cargo = !config->show_hostile_cargo;
      } else if (MapKey::IsDown(GameFunction::ToggleCargoArmada)) {
        config->show_armada_cargo = !config->show_armada_cargo;
      } else if (MapKey::IsDown(GameFunction::LogLevelOff)) {
        // spdlog::log("Setting log level to OFF");
        spdlog::set_level(spdlog::level::off);
        spdlog::flush_on(spdlog::level::off);
      } else if (MapKey::IsDown(GameFunction::LogLevelError)) {
        spdlog::set_level(spdlog::level::err);
        spdlog::flush_on(spdlog::level::err);
        // spdlog::log("Setting log level to ERROR");
      } else if (MapKey::IsDown(GameFunction::LogLevelWarn)) {
        spdlog::set_level(spdlog::level::warn);
        spdlog::flush_on(spdlog::level::warn);
        // spdlog::log("Setting log level to WARN");
      } else if (MapKey::IsDown(GameFunction::LogLevelInfo)) {
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
        // spdlog::log("Setting log level to INFO");
      } else if (MapKey::IsDown(GameFunction::LogLevelDebug)) {
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
        // spdlog::log("Setting log level to DEBUG");
      } else if (MapKey::IsDown(GameFunction::LogLevelTrace)) {
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
        // spdlog::log("Setting log level to TRACE");
      } else if (MapKey::IsDown(GameFunction::ShowShips)) {
        auto fleet_bar        = ObjectFinder<FleetBarViewController>::Get();
        auto fleet_controller = fleet_bar ? fleet_bar->fleetPanelController() : nullptr;
        auto fleet            = fleet_controller ? fleet_controller->fleet() : nullptr;
        if (fleet) {
          fleet_controller->RequestAction(fleet, ActionType::Manage, 0, ActionBehaviour::Default);
        }
      }
    }
  } else {
    if (auto chat_manager = ChatManager::Instance(); chat_manager) {
      if (MapKey::IsDown(GameFunction::SelectChatGlobal)) {
        return chat_manager->OpenChannel(ChatChannelCategory::Global);
      } else if (MapKey::IsDown(GameFunction::SelectChatAlliance)) {
        return chat_manager->OpenChannel(ChatChannelCategory::Alliance);
      } else if (MapKey::IsDown(GameFunction::SelectChatPrivate)) {
        return chat_manager->OpenChannel(ChatChannelCategory::Private);
      }
    }
  }

  if (!Key::IsInputFocused()) {
    // Lets try to remove the pre-scan because we hit escape and it's visible
    if (Key::Pressed(KeyCode::Escape)) {
      if (config->primary_requires_visible_target) {
        force_space_action_next_frame = false;
      }
      if (DidHideViewers()) {
        return;
      }
    }

    // Dismiss the golden rewards screen when escape or space is pressed.
    if (MapKey::IsDown(GameFunction::ActionPrimary) || Key::Pressed(KeyCode::Escape)) {
      if (auto reward_controller = ObjectFinder<AnimatedRewardsScreenViewController>::Get(); reward_controller) {
        if (reward_controller->IsActive()) {
          return reward_controller->GoBackToLastSection();
        }
      }
    }

    if (MapKey::IsDown(GameFunction::ActionPrimary) || MapKey::IsDown(GameFunction::ActionSecondary)
        || MapKey::IsDown(GameFunction::ActionRecall) || MapKey::IsDown(GameFunction::ActionRepair)
        || MapKey::IsDown(GameFunction::ActionQueue) || MapKey::IsDown(GameFunction::ActionQueueClear)
        || force_space_action_next_frame) {
      if (Hub::IsInSystemOrGalaxyOrStarbase() && !Hub::IsInChat() && !Key::IsInputFocused()) {
        auto fleet_bar = ObjectFinder<FleetBarViewController>::Get();
        bool was_forced = force_space_action_next_frame;
        ExecuteSpaceAction(fleet_bar);
        if (was_forced) {
          force_space_action_next_frame = false;
        }
      }
    }

    DailyFactionBulkClaimUpdate();

    if (MapKey::IsDown(GameFunction::ActionView)) {
      auto all_pre_scan_widgets = ObjectFinder<PreScanTargetWidget>::GetAll();

      for (auto& pre_scan_widget : all_pre_scan_widgets) {
        auto visibility_controller = pre_scan_widget ? pre_scan_widget->visibilityController() : nullptr;
        auto rewardsWidget         = pre_scan_widget ? pre_scan_widget->rewardsButtonWidget() : nullptr;
        auto rewards_controller    = rewardsWidget ? rewardsWidget->rewardsController() : nullptr;
        if (visibility_controller
            && (visibility_controller->state() == VisibilityState::Visible
                || visibility_controller->state() == VisibilityState::Show)
            && rewards_controller) {
          if (rewards_controller->state() != VisibilityState::Visible
              && rewards_controller->state() != VisibilityState::Show) {
            show_info_pending = 5;
          } else {
            rewards_controller->Hide();
          }
        }
      }
    }

    // Did we not find a rewards widget in the previous frame?
    if (show_info_pending > 0) {
      auto all_pre_scan_widgets = ObjectFinder<PreScanTargetWidget>::GetAll();

      for (auto& pre_scan_widget : all_pre_scan_widgets) {
        auto       visibility_controller = pre_scan_widget ? pre_scan_widget->visibilityController() : nullptr;
        auto       rewardsWidget         = pre_scan_widget ? pre_scan_widget->rewardsButtonWidget() : nullptr;
        auto       rewards_controller    = rewardsWidget ? rewardsWidget->rewardsController() : nullptr;
        const auto pre_scan_visible      = visibility_controller
                                           && (visibility_controller->state() == VisibilityState::Visible
                                               || visibility_controller->state() == VisibilityState::Show);
        if (pre_scan_visible && rewards_controller) {
          const auto rewards_widget_visible = rewards_controller->state() == VisibilityState::Visible
                                              || rewards_controller->state() == VisibilityState::Show;
          if (!rewards_widget_visible) {
            rewards_controller->Show(true);
          }
        }
      }
      show_info_pending -= 1;
    }
  }

  if (config->disable_escape_exit && Key::Pressed(KeyCode::Escape)) {
    // Keep suppressing a held key. Only distinct key-down edges participate in
    // the double-tap window.
    if (config->disable_escape_exit_timer <= 0 || !Key::Down(KeyCode::Escape)) {
      return;
    }

    static auto previous_escape_down = std::chrono::steady_clock::time_point{};
    const auto  escape_now           = std::chrono::steady_clock::now();
    const auto  escape_diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(escape_now - previous_escape_down);

    if (previous_escape_down == std::chrono::steady_clock::time_point{}
        || escape_diff > std::chrono::milliseconds(config->disable_escape_exit_timer)) {
      previous_escape_down = escape_now;
      return;
    }

    // Consume the completed pair so a rapid third press starts a new one.
    previous_escape_down = {};
  }

  // config->Load();

  return original(_this);
}

template <typename T> inline bool CanHideViewer(T* widget, bool require_parent = false)
{
  auto* visibility = widget ? widget->visibilityController() : nullptr;
  if (!visibility || (visibility->state() != VisibilityState::Visible && visibility->state() != VisibilityState::Show)) {
    return false;
  }
  if (!require_parent) {
    return true;
  }
  auto* parent = widget->Parent();
  return parent && parent->IsShowing();
}

template <typename T> inline bool CanHideViewersOfType()
{
  for (auto widget : ObjectFinder<T>::GetAll()) {
    if (CanHideViewer(widget)) {
      return true;
    }
  }

  return false;
}

bool CanHideViewers()
{
  return (CanHideViewersOfType<AllianceStarbaseObjectViewerWidget>() || CanHideViewersOfType<ArmadaObjectViewerWidget>()
          || CanHideViewersOfType<CelestialObjectViewerWidget>() || CanHideViewersOfType<EmbassyObjectViewer>()
          || CanHideViewersOfType<HousingObjectViewerWidget>() || CanHideViewersOfType<MiningObjectViewerWidget>()
          || CanHideViewersOfType<MissionsObjectViewerWidget>() || CanHideViewersOfType<PreScanTargetWidget>()
          || CanHideViewersOfType<HousingObjectViewerWidget>());
}

template <typename T> inline bool DidHideViewersOfType()
{
  const auto objects = ObjectFinder<T>::GetAll();
  auto       didHide = false;
  for (auto widget : objects) {
    if (CanHideViewer(widget)) {
      widget->HideAllViewers();
      didHide = true;
    }
  }

  return didHide;
}

bool DidHideViewers()
{
  return DidHideViewersOfType<AllianceStarbaseObjectViewerWidget>() || DidHideViewersOfType<ArmadaObjectViewerWidget>()
         || DidHideViewersOfType<CelestialObjectViewerWidget>() || DidHideViewersOfType<EmbassyObjectViewer>()
         || DidHideViewersOfType<HousingObjectViewerWidget>() || DidHideViewersOfType<MiningObjectViewerWidget>()
         || DidHideViewersOfType<MissionsObjectViewerWidget>() || DidHideViewersOfType<PreScanTargetWidget>()
         || DidHideViewersOfType<HousingObjectViewerWidget>();
}

void GotoSection(SectionID sectionID, void* section_data)
{
  Hub::get_SectionManager()->TriggerSectionChange(sectionID, section_data, false, false, true);
}

void ChangeNavigationSection(SectionID sectionID)
{
  const auto section_data = Hub::get_SectionManager()->sectionStorage()->GetState(sectionID);

  if (section_data) {
    GotoSection(sectionID, section_data);
  } else {
    NavigationSectionManager::ChangeNavigationSection(sectionID);
  }
}

#define FleetAction_Format "Fleet {} ({}) #{} - State: {}, previous {} - canAction {}, canState {} - didAction: {}"

template <typename T>
inline bool DidExecuteFleetAction(std::string_view actionText, ActionType actionType, FleetBarViewController* fleet_bar,
                                  const std::span<const FleetState> wantedStates,
                                  FleetState                        helpState = FleetState::Unknown)
{
  if (!fleet_bar) {
    return false;
  }

  auto fleet_controller = fleet_bar->fleetPanelController();
  auto fleet            = fleet_controller ? fleet_controller->fleet() : nullptr;
  if (!fleet) {
    return false;
  }
  auto fleet_state = fleet->CurrentState();

  auto       fleet_id   = fleet->Id();
  auto       prev_state = fleet->PreviousState();
  auto       canAction  = true; // actionRequired->CheckIsMet();
  FleetState canState   = FleetState::Unknown;
  auto       didAction  = false;

  if (std::find(std::begin(wantedStates), std::end(wantedStates), fleet_state) != std::end(wantedStates)) {
    canState = fleet_state;
  }

  spdlog::trace(FleetAction_Format, actionText, actionType.flagValue, (int)fleet_id, (int)fleet_state, (int)prev_state,
                canAction, (int)canState, "[start]");

  if (canState != FleetState::Unknown && canAction) {
    if (NavigationSectionManager::Instance() && NavigationSectionManager::Instance()->SNavigationManager()) {
      NavigationSectionManager::Instance()->SNavigationManager()->HideInteraction();
    }

    didAction = fleet_controller->RequestAction(fleet, actionType, 0, ActionBehaviour::Default);
  }

  if (helpState != FleetState::Unknown && (didAction || helpState == fleet->CurrentState())) {
    didAction = didAction || fleet_controller->RequestAction(fleet, actionType, 0, ActionBehaviour::AskHelp);
  }

  spdlog::trace(FleetAction_Format, actionText, actionType.flagValue, (int)fleet_id, (int)fleet_state, (int)prev_state,
                canAction, (int)canState, didAction);

  return didAction;
}

bool DidExecuteRecall(FleetBarViewController* fleet_bar)
{
  static constexpr FleetState states[] = {FleetState::IdleInSpace, FleetState::Impulsing, FleetState::Mining,
                                          FleetState::Capturing};

  auto fleet_controller = fleet_bar->fleetPanelController();

  return DidExecuteFleetAction<RecallRequirement>("Recall", ActionType::Recall, fleet_bar, states);
}

bool DidExecuteRepair(FleetBarViewController* fleet_bar)
{
  static constexpr FleetState states[] = {FleetState::Docked, FleetState::Destroyed};

  return DidExecuteFleetAction<CanRepairRequirement>("Repair", ActionType::Repair, fleet_bar, states,
                                                     FleetState::Repairing);
}

void ExecuteSpaceAction(FleetBarViewController* fleet_bar)
{
  if (MapKey::IsDown(GameFunction::ActionPrimary)) {
    if (auto* navigation = ObjectFinder<NavigationInteractionUIViewController>::Get();
        navigation && navigation->TryPressJoinWaveDefense()) {
      return;
    }
    // Query active viewers without adding constructor detours for this popup or its subclasses.
    if (auto* viewers = WaveDefenseObjectViewerWidget::FindActiveViewers()) {
      for (auto* widget : *viewers) {
        if (CanHideViewer(widget, true)) {
          // Use the displayed native action, including its eligibility and confirmation checks.
          widget->PressPrimaryButton();
          return;
        }
      }
    }
  }

  if (!fleet_bar) {
    return;
  }

  auto fleet_controller = fleet_bar->fleetPanelController();
  auto fleet            = fleet_controller ? fleet_controller->fleet() : nullptr;
  if (!fleet) {
    return;
  }

  auto action_queue = ActionQueueManager::Instance();
  if (!action_queue) {
    return;
  }

  auto has_primary       = MapKey::IsDown(GameFunction::ActionPrimary) || force_space_action_next_frame;
  auto has_repair        = MapKey::IsDown(GameFunction::ActionRepair);
  auto has_recall_cancel = MapKey::IsDown(GameFunction::ActionRecallCancel);
  auto has_secondary     = MapKey::IsDown(GameFunction::ActionSecondary);
  auto has_queue         = MapKey::IsDown(GameFunction::ActionQueue);
  auto has_queue_clear   = MapKey::IsDown(GameFunction::ActionQueueClear);
  auto has_recall =
      MapKey::IsDown(GameFunction::ActionRecall) && (!Config::Get().disable_preview_recall || !CanHideViewers());
  const bool require_visible_target = has_primary && Config::Get().primary_requires_visible_target;

  if (has_queue_clear) {
    action_queue->ClearQueue(fleet);
  } else if (has_recall_cancel
             && (fleet->CurrentState() == FleetState::WarpCharging || fleet->CurrentState() == FleetState::Warping)) {
    fleet_controller->CancelButtonClicked();
  } else {
    auto all_pre_scan_widgets = ObjectFinder<PreScanTargetWidget>::GetAll();
    for (auto pre_scan_widget : all_pre_scan_widgets) {
      if (CanHideViewer(pre_scan_widget, require_visible_target)) {

        if (auto mine_object_viewer_widget = ObjectFinder<MiningObjectViewerWidget>::Get();
            CanHideViewer(mine_object_viewer_widget, require_visible_target)) {
          if (has_secondary && pre_scan_widget->scanEngageButtonsWidget()) {
            return pre_scan_widget->scanEngageButtonsWidget()->OnScanButtonClicked();
          } else if (has_primary) {
            return mine_object_viewer_widget->MineClicked();
          }
        }

        if (has_queue && action_queue->IsQueueUnlocked() && pre_scan_widget->addToQueueButtonWidget()
            && pre_scan_widget->scanEngageButtonsWidget()) {
          auto context = pre_scan_widget->scanEngageButtonsWidget()->Context();
          auto type    = GetHullTypeFromBattleTarget(context);

          if (type != HullType::ArmadaTarget && (type != HullType::Any || force_space_action_next_frame)) {
            if (pre_scan_widget->addToQueueButtonWidget()->isActiveAndEnabled()) {
              auto listener = pre_scan_widget->addToQueueButtonWidget()->SemaphoreListener();
              if (listener && !action_queue->IsQueueFull(fleet)) {
                auto button = listener->TheButton();
                if (button) {
                  button->Press();
                  DidHideViewers();
                }
              }
              return;
            }

            if (type == HullType::Any) {
              force_space_action_next_frame = true;
              return;
            }
          }
        }

        if (has_secondary && pre_scan_widget->scanEngageButtonsWidget()) {
          return pre_scan_widget->scanEngageButtonsWidget()->OnScanButtonClicked();
        }

        if (has_primary && pre_scan_widget->scanEngageButtonsWidget()
            && pre_scan_widget->scanEngageButtonsWidget()->enabled()) {
          auto context = pre_scan_widget->scanEngageButtonsWidget()->Context();
          auto type    = GetHullTypeFromBattleTarget(context);

          // Try once more in X frames if we get ANY
          // in-case of failed to navgitate error?
          auto armada_widget = ObjectFinder<ArmadaObjectViewerWidget>::Get();
          auto armada_state  = VisibilityState::Unknown;

          if (armada_widget) {
            if (armada_widget->visibilityController()) {
              armada_state = armada_widget->visibilityController()->State();
            } else {
              spdlog::warn("ArmadaWidget has no visibility controller, using default Visible state");
              armada_state = VisibilityState::Visible;
            }
          }

          auto canActionPrimary = type != HullType::Any;
          if (type == HullType::ArmadaTarget
              && (armada_state == VisibilityState::Visible || armada_state == VisibilityState::Show)) {
            canActionPrimary = false;
          } else if (force_space_action_next_frame) {
            canActionPrimary = true;
          }

          // Try once more in X frames if we get ANY
          // in-case of failed to navgitate error?
          if (canActionPrimary) {
            if (type == HullType::ArmadaTarget) {
              auto* attack_button = pre_scan_widget->armadaAttackButton();
              if (attack_button && attack_button->isActiveAndEnabled()) {
                auto listener = attack_button->SemaphoreListener();
                if (listener) {
                  auto button = listener->TheButton();
                  if (button) {
                    button->Press();
                  }
                }
                return;
              }
              pre_scan_widget->scanEngageButtonsWidget()->OnArmadaButtonClicked();
            } else {
              pre_scan_widget->scanEngageButtonsWidget()->OnEngageButtonClicked();
            }
            return;
          } else if (type == HullType::Any) {
            force_space_action_next_frame = true;
            return;
          }
        }
      }
    }

    if (auto mine_object_viewer_widget = ObjectFinder<MiningObjectViewerWidget>::Get();
        CanHideViewer(mine_object_viewer_widget, require_visible_target)) {
      if (has_secondary && mine_object_viewer_widget->scanEngageButtonsWidget()) {
        if (mine_object_viewer_widget->scanEngageButtonsWidget()->Context()) {
          return mine_object_viewer_widget->scanEngageButtonsWidget()->OnScanButtonClicked();
        }
      } else if (has_primary) {
        return mine_object_viewer_widget->MineClicked();
      }
    } else if (auto star_node_object_viewer_widget = ObjectFinder<StarNodeObjectViewerWidget>::Get();
               star_node_object_viewer_widget && star_node_object_viewer_widget->Context()
               && (!require_visible_target || CanHideViewer(star_node_object_viewer_widget, true))) {
      if (has_secondary) {
        star_node_object_viewer_widget->OnViewButtonActivation();
        return;
      } else if (has_primary) {
        star_node_object_viewer_widget->InitiateWarp();
        return;
      }
    } else if (auto navigation_ui_controller = ObjectFinder<NavigationInteractionUIViewController>::Get();
               navigation_ui_controller && has_primary) {
      auto armada_widget = ObjectFinder<ArmadaObjectViewerWidget>::Get();
      auto armada_state  = VisibilityState::Unknown;

      if (armada_widget) {
        if (armada_widget->visibilityController()) {
          armada_state = armada_widget->visibilityController()->State();
        } else {
          spdlog::warn("ArmadaWidget has no visibility controller, using default Visible state");
          armada_state = VisibilityState::Visible;
        }
      }

      spdlog::info("have armada? {}, State {}", (armada_widget ? "Yes" : "No"), (int)armada_state);
      if (armada_widget && (armada_state == VisibilityState::Visible || armada_state == VisibilityState::Show)) {
        auto button = armada_widget->joinContext();
        if (button && button->Interactable()) {
          armada_widget->ValidateThenJoinArmada();
          return;
        }
      } else {
        // Dismissal keeps the native context's last position/path. Preserve that legacy shortcut unless opted out.
        if (!require_visible_target || navigation_ui_controller->IsSetCourseVisible()) {
          navigation_ui_controller->OnSetCourseButtonClick();
        }
        return;
      }
    }

    if (has_recall && DidExecuteRecall(fleet_bar)) {
      force_space_action_next_frame = false;
      return;
    }
    if (has_repair && DidExecuteRepair(fleet_bar)) {
      force_space_action_next_frame = false;
      return;
    }
  }
}

HullType GetHullTypeFromBattleTarget(BattleTargetData* context)
{
  if (!context) {
    return HullType::Any;
  }
  auto deployed_data = context->TargetFleetDeployedData();
  if (!deployed_data) {
    return HullType::Any;
  }
  auto hull_spec = deployed_data->Hull();
  if (!hull_spec) {
    return HullType::Any;
  }
  return hull_spec->Type();
}

void ChatMessageListLocalViewController_AboutToShow_Hook(ChatMessageListLocalViewController* _this);
decltype(ChatMessageListLocalViewController_AboutToShow_Hook)* oChatMessageListLocalViewController_AboutToShow =
    nullptr;
void ChatMessageListLocalViewController_AboutToShow_Hook(ChatMessageListLocalViewController* _this)
{
  oChatMessageListLocalViewController_AboutToShow(_this);
  if (_this->inputField()) {
    _this->inputField()->SendOnFocus();
  }
}

void InitializeActions_Hook(auto original, ShortcutsManager* _this)
{
  if (Config::Get().use_scopely_hotkeys) {
    return original(_this);
  }
}

bool CheckShowCargo(RewardsButtonWidget* widget)
{
  if (!widget || !Config::Get().show_cargo_default) {
    return false;
  }

  if (!widget->Context()) {
    return false;
  }

  const auto target_fleet_deployed = widget->Context()->TargetFleetDeployedData();

  if (!target_fleet_deployed) {
    return Config::Get().show_station_cargo;
  }
  auto fleet_type = target_fleet_deployed->FleetType();
  if (fleet_type == DeployedFleetType::Player) {
    return Config::Get().show_player_cargo;
  } else if (fleet_type == DeployedFleetType::Marauder) {
    if (auto hull = target_fleet_deployed->Hull(); hull && hull->Type() == HullType::ArmadaTarget) {
      return Config::Get().show_armada_cargo;
    } else {
      return Config::Get().show_hostile_cargo;
    }
  }

  return false;
}

void OnDidBindContext_Hook(auto original, RewardsButtonWidget* _this)
{
  if (!_this) {
    return original(_this);
  }

  auto rewards_controller = _this->rewardsController();
  auto pre_state          = rewards_controller ? rewards_controller->state() : VisibilityState::Unknown;
  pre_state               = pre_state;
  original(_this);

  rewards_controller = _this->rewardsController();
  auto post_state    = rewards_controller ? rewards_controller->state() : VisibilityState::Unknown;
  post_state         = post_state;
  if (rewards_controller && CheckShowCargo(_this)) {
    rewards_controller->Show(true);
    show_info_pending = 1;
  }
}

void ShowWithFleet_Hook(auto original, PreScanTargetWidget* _this, void* a1)
{
  original(_this, a1);
  if (!_this) {
    return;
  }

  auto rewards_button_widget = _this->rewardsButtonWidget();
  auto rewards_controller    = rewards_button_widget ? rewards_button_widget->rewardsController() : nullptr;
  if (rewards_controller && CheckShowCargo(rewards_button_widget)) {
    rewards_controller->Show(true);
    show_info_pending = 1;
  }
}

bool install_screen_manager_update_hook()
{
  static bool installed = false;
  if (installed) {
    return true;
  }

  if (auto m = ScreenManager::Class::Method("Update")) {
    if (!SPUD_STATIC_DETOUR(m.Pointer(), ScreenManager_Update_Hook))
      return false;
    installed = true;
  }
  return installed;
}

void InstallShortcutHintHooks()
{
  // Configuration is fully parsed before hook installation. NONE, empty, and invalid bindings opt out.
  if (!MapKey::HasBinding(GameFunction::ToggleShortcutHints)) {
    return;
  }

  // Only the detour targets gate the feature. The managed members the hint path calls resolve on first use; a
  // missing one is reported once and reads as null/false, which leaves the native hint text in place.
  auto update_text       = ShortcutKeybindHint::Class::Method("UpdateText", 0);
  auto update_visibility = ShortcutKeybindHint::Class::Method("UpdateVisibility", 1);
  if (!initialize_actions_hook_ready || !update_text || !update_visibility) {
    return;
  }

  MapKey::CacheShortcutHints();
  original_shortcut_hint_update_text = SPUD_STATIC_DETOUR(update_text.Pointer(), ShortcutKeybindHint_UpdateText_Hook);
  SPUD_STATIC_DETOUR(update_visibility.Pointer(), ShortcutKeybindHint_UpdateVisibility_Hook);
  shortcut_hints_ready = true;
}

void InstallHotkeyHooks()
{
  if (auto m = ShortcutsManager::Class::Method("InitializeActions")) {
    SPUD_STATIC_DETOUR(m.Pointer(), InitializeActions_Hook);
    initialize_actions_hook_ready = true;
  }

  InstallShortcutHintHooks();

  install_screen_manager_update_hook();
#ifdef _MODDBG
  fleet_watch::InstallRuntimeProbe();
#endif

  if (auto m = RewardsButtonWidget::Class::Method("OnDidBindContext"))
    SPUD_STATIC_DETOUR(m.Pointer(), OnDidBindContext_Hook);
  if (auto m = PreScanTargetWidget::Class::Method("ShowWithFleet"))
    SPUD_STATIC_DETOUR(m.Pointer(), ShowWithFleet_Hook);
}

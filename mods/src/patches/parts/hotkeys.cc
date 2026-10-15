// Keyboard shortcuts and the mod's frame tick. ScreenManager.Update runs the other features' frame updates, then
// dispatches the shortcut that was pressed. Returning without calling the game's Update swallows the key, so the
// game's own handling of it does not run as well.

#include "config.h"
#include "patches/fleet_watch.h"
#include "patches/parts/action_queue.h"
#include "patches/input.h"
#include "patches/object_tracker.h"
#include "patches/patches.h"
#include "patches/shortcuts.h"

#include <prime/Hub.h>
#include <prime/KeyCode.h>
#include <prime/chat.h>
#include <prime/fleet.h>
#include <prime/inventory.h>
#include <prime/navigation.h>
#include <prime/ui.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;

bool hotkeys_installed = false;

// Ship Swap and Officer Swap live on the Manage Ships screen, and Officer Presets on the officer assignment screen
// that Officer Swap opens. Their shortcuts open those screens first, then press the button once it exists. 300
// frames is a few seconds at the game's frame rate.
constexpr int kButtonWaitFrames         = 300;
int           ship_swap_frames          = 0;
int           officer_swap_frames       = 0;
int           officer_presets_frames    = 0;
bool          officer_assignment_opened = false;

// UnityEngine.InputSystem.InputAction.CallbackContext, passed empty. Generated code takes it by value with the
// platform's C++ ABI, so this layout must match the managed struct.
struct InputActionCallbackContext {
  void*   state;
  int32_t action_index;
};
static_assert(sizeof(InputActionCallbackContext) == 16);

// Runs one of ShortcutsManager's On*Action(InputAction.CallbackContext) handlers, the way the game's own shortcut
// for it would. A handler missing from this client is reported once by the binding layer and does nothing.
template <il2cpp::Name Action> void Native()
{
  if (auto* manager = ShortcutsManager::Instance())
    manager->Call<Action>(InputActionCallbackContext{});
  else
    spdlog::warn("[Hotkeys] native {} shortcut is unavailable", Action.View());
}

void GotoSection(SectionID section, void* data = nullptr)
{
  if (auto* manager = Hub::get_SectionManager())
    manager->TriggerSectionChange(section, data, false, false, true);
}

template <SectionID Section> void Goto()
{ GotoSection(Section); }

// Galaxy and system views keep their state in section storage; reuse it so the camera returns where it was.
template <SectionID Section> void GotoNavigation()
{
  auto* manager = Hub::get_SectionManager();
  auto* storage = manager ? manager->sectionStorage() : nullptr;
  if (auto* state = storage ? storage->GetState(Section) : nullptr)
    GotoSection(Section, state);
  else
    NavigationSectionManager::ChangeNavigationSection(Section);
}

template <bool Config::* Setting> void Toggle()
{
  auto& config    = Config::Get();
  config.*Setting = !(config.*Setting);
}

template <spdlog::level::level_enum Level> void SetLogLevel()
{
  spdlog::set_level(Level);
  spdlog::flush_on(Level);
}

template <void (*Adjust)(bool), bool Up> void Scale()
{ Adjust(Up); }

void ShowBookmarks()
{
  if (auto* bookmarks = BookmarksManager::Instance())
    bookmarks->ViewBookmarks();
  else
    GotoSection(SectionID::Bookmarks_Main);
}

void ShowCoordinateSearch()
{
  if (auto* bookmarks = BookmarksManager::Instance())
    return bookmarks->ViewCoordinateSearch();
  spdlog::warn("[ShowLookup] BookmarksManager instance not available, falling back to main bookmarks");
  GotoSection(SectionID::Bookmarks_Main);
}

void ShowHaven()
{ PlanetaryBaseManager::ViewOwnHaven(); }

// Presses the button `pick` returns on the first visible, active Controller.
template <typename Controller> bool PressButton(Button* (*pick)(Controller*))
{
  for (auto* controller : ObjectFinder<Controller>::GetAll()) {
    auto* canvas = controller ? GetCanvasControllerFromComponent(controller) : nullptr;
    auto* button = canvas && canvas->Visible() && controller->isActiveAndEnabled() ? pick(controller) : nullptr;
    if (button) {
      button->Press();
      return true;
    }
  }
  return false;
}

bool PressShipSwapButton()
{
  return PressButton<ShipManagementScreenStateViewController>(
      [](ShipManagementScreenStateViewController* controller) { return controller->swapShipButton(); });
}

bool PressOfficerSwapButton()
{
  return PressButton<ShipManagementScreenStateViewController>(
      [](ShipManagementScreenStateViewController* controller) { return controller->officerIconButton(); });
}

bool PressOfficerPresetsButton()
{
  return PressButton<OfficerAssignmentViewController>([](OfficerAssignmentViewController* controller) -> Button* {
    auto* widget   = controller->officerPresetsButton();
    auto* listener = widget ? widget->SemaphoreListener() : nullptr;
    return listener ? listener->TheButton() : nullptr;
  });
}

void OpenManageShips()
{
  auto* fleet_bar = ObjectFinder<FleetBarViewController>::Get();
  if (auto* fleet = fleet_bar ? fleet_bar->SelectedFleet() : nullptr)
    fleet_bar->fleetPanelController()->RequestAction(fleet, ActionType::Manage, 0, ActionBehaviour::Default);
}

void ShowShipSwap()
{
  if (PressShipSwapButton())
    return;
  auto* fleet_bar = ObjectFinder<FleetBarViewController>::Get();
  if (!fleet_bar || !fleet_bar->SelectedFleet()) {
    spdlog::warn("[Hotkeys] unable to open Manage Ships for Ship Swap");
    return;
  }
  OpenManageShips();
  ship_swap_frames = kButtonWaitFrames;
}

void ShowOfficerSwap()
{
  if (PressOfficerSwapButton())
    return;
  auto* fleet_bar = ObjectFinder<FleetBarViewController>::Get();
  if (!fleet_bar || !fleet_bar->SelectedFleet()) {
    spdlog::warn("[Hotkeys] unable to open Manage Ships for Officer Swap");
    return;
  }
  OpenManageShips();
  officer_swap_frames = kButtonWaitFrames;
}

void ShowOfficerPresets()
{
  if (PressOfficerPresetsButton())
    return;
  officer_assignment_opened = PressOfficerSwapButton();
  if (!officer_assignment_opened) {
    auto* fleet_bar = ObjectFinder<FleetBarViewController>::Get();
    if (!fleet_bar || !fleet_bar->SelectedFleet()) {
      spdlog::warn("[Hotkeys] unable to open Manage Ships for Officer Presets");
      return;
    }
    OpenManageShips();
  }
  officer_presets_frames = kButtonWaitFrames;
}

// Presses the button a Show* shortcut above is waiting for once its screen is up. Returns true when one was pressed.
bool PressAwaitedButton()
{
  if (officer_presets_frames > 0) {
    if (PressOfficerPresetsButton()) {
      officer_presets_frames    = 0;
      officer_assignment_opened = false;
      return true;
    }
    // Manage Ships is up but officer assignment not yet: open it on the way.
    if (!officer_assignment_opened && PressOfficerSwapButton())
      officer_assignment_opened = true;
    if (--officer_presets_frames == 0) {
      officer_assignment_opened = false;
      spdlog::warn("[Hotkeys] timed out waiting for the Officer Presets button");
    }
  }

  if (officer_swap_frames > 0) {
    if (PressOfficerSwapButton()) {
      officer_swap_frames = 0;
      return true;
    }
    if (--officer_swap_frames == 0)
      spdlog::warn("[Hotkeys] timed out waiting for the Officer Swap button");
  }

  if (ship_swap_frames > 0) {
    if (PressShipSwapButton()) {
      ship_swap_frames = 0;
      return true;
    }
    if (--ship_swap_frames == 0)
      spdlog::warn("[Hotkeys] timed out waiting for the Ship Swap button");
  }
  return false;
}

void CycleAutoConfirmInstantWarp()
{
  auto& setting = Config::Get().auto_confirm_instant_warp;
  switch (setting) {
    case InstantWarpConfirmation::None:
      setting = InstantWarpConfirmation::Warp;
      break;
    case InstantWarpConfirmation::Warp:
      setting = InstantWarpConfirmation::Jump;
      break;
    case InstantWarpConfirmation::Jump:
      setting = InstantWarpConfirmation::None;
      break;
  }
  constexpr const char* kNames[] = {"none", "warp", "jump"};
  spdlog::info("Auto-confirm instant warp set to {}", kNames[static_cast<int>(setting)]);
}

struct Action {
  GameFunction shortcut;
  void (*run)();
  // Held actions repeat every frame while the keys are down; the rest fire once per press.
  bool held = false;
  // Navigation swallows the key; toggles let the game see it too.
  bool consumes = true;
};

// Checked in order, outside chat and text fields; the first pressed one runs.
constexpr Action kActions[] = {
    {GameFunction::ShowQTrials, Goto<SectionID::ChallengeSelection>},
    {GameFunction::ShowBookmarks, ShowBookmarks},
    {GameFunction::ShowLookup, ShowCoordinateSearch},
    {GameFunction::ShowRefinery, Goto<SectionID::Shop_Refining_List>},
    {GameFunction::ShowFactions, Goto<SectionID::Shop_MainFactions>},
    {GameFunction::ShowStationExterior, Goto<SectionID::Starbase_Exterior>},
    {GameFunction::NativeShortcutGalaxy, Native<"OnGalaxyAction">},
    {GameFunction::ShowGalaxy, GotoNavigation<SectionID::Navigation_Galaxy>},
    {GameFunction::ShowStationInterior, Goto<SectionID::Starbase_Interior>},
    {GameFunction::ShowHaven, ShowHaven},
    {GameFunction::ShowSystem, GotoNavigation<SectionID::Navigation_System>},
    {GameFunction::ShowArtifacts, Goto<SectionID::ArtifactHall_Inventory>},
    {GameFunction::ShowShipConstruction, Native<"OnShipsAction">},
    {GameFunction::ShowOfficerSwap, ShowOfficerSwap},
    {GameFunction::ShowShipSwap, ShowShipSwap},
    {GameFunction::ShowShields, Native<"OnPeaceShieldAction">},
    {GameFunction::ShowBattlelogs, Native<"OnInboxAction">},
    {GameFunction::ShowInventory, Goto<SectionID::InventoryList>},
    {GameFunction::ShowMissions, Goto<SectionID::Missions_AcceptedList>},
    {GameFunction::ShowResearch, Goto<SectionID::Research_LandingPage>},
    {GameFunction::ShowScrapYard, Goto<SectionID::ShipScrapping_List>},
    {GameFunction::ShowOfficers, Goto<SectionID::OfficerInventory>},
    {GameFunction::ShowOfficerPresets, ShowOfficerPresets},
    // TODO: opens the first fleet commander (Spock), not the selected fleet's.
    {GameFunction::ShowCommander, Goto<SectionID::FleetCommander_Management>},
    {GameFunction::ShowAwayTeam, Goto<SectionID::Missions_AwayTeamsList>},
    {GameFunction::NativeShortcutEvents, Native<"OnEventsAction">},
    {GameFunction::ShowEvents, Goto<SectionID::Tournament_Group_Selection>},
    {GameFunction::ShowExoComp, Goto<SectionID::Consumables>},
    {GameFunction::ShowDaily, Goto<SectionID::Missions_DailyGoals>},
    {GameFunction::ShowGifts, Goto<SectionID::Shop_List>},
    {GameFunction::ShowAlliance, Goto<SectionID::Alliance_Main>},
    {GameFunction::ShowAllianceHelp, Goto<SectionID::Alliance_Help>},
    {GameFunction::ShowAllianceArmada, Goto<SectionID::Alliance_Armadas>},
    {GameFunction::ShowSettings, Goto<SectionID::GameSettings>},
    {GameFunction::UiScaleUp, Scale<AdjustUiScale, true>, true, false},
    {GameFunction::UiScaleDown, Scale<AdjustUiScale, false>, true, false},
    {GameFunction::UiShipScaleUp, Scale<AdjustUiShipScale, true>, true, false},
    {GameFunction::UiShipScaleDown, Scale<AdjustUiShipScale, false>, true, false},
    {GameFunction::UiViewerScaleUp, Scale<AdjustUiViewerScale, true>, true, false},
    {GameFunction::UiViewerScaleDown, Scale<AdjustUiViewerScale, false>, true, false},
    {GameFunction::ToggleAutoConfirmInstantWarp, CycleAutoConfirmInstantWarp, false, false},
    {GameFunction::TogglePreviewLocate, Toggle<&Config::disable_preview_locate>, false, false},
    {GameFunction::TogglePreviewRecall, Toggle<&Config::disable_preview_recall>, false, false},
    {GameFunction::ToggleCargoDefault, Toggle<&Config::show_cargo_default>, false, false},
    {GameFunction::ToggleCargoPlayer, Toggle<&Config::show_player_cargo>, false, false},
    {GameFunction::ToggleCargoStation, Toggle<&Config::show_station_cargo>, false, false},
    {GameFunction::ToggleCargoHostile, Toggle<&Config::show_hostile_cargo>, false, false},
    {GameFunction::ToggleCargoArmada, Toggle<&Config::show_armada_cargo>, false, false},
    {GameFunction::LogLevelOff, SetLogLevel<spdlog::level::off>, false, false},
    {GameFunction::LogLevelError, SetLogLevel<spdlog::level::err>, false, false},
    {GameFunction::LogLevelWarn, SetLogLevel<spdlog::level::warn>, false, false},
    {GameFunction::LogLevelInfo, SetLogLevel<spdlog::level::info>, false, false},
    {GameFunction::LogLevelDebug, SetLogLevel<spdlog::level::debug>, false, false},
    {GameFunction::LogLevelTrace, SetLogLevel<spdlog::level::trace>, false, false},
    {GameFunction::ShowShips, OpenManageShips, false, false},
};

// The table's first pressed action, if any. Returns true when that action swallows the key.
bool RunAction()
{
  for (const auto& action : kActions) {
    if (action.held ? Shortcut::IsPressed(action.shortcut) : Shortcut::IsDown(action.shortcut)) {
      action.run();
      return action.consumes;
    }
  }
  return false;
}

void ViewSelectedFleet(FleetPlayerData* fleet)
{
  NavigationSectionManager::HideInteraction();
  if (auto* fleets = FleetsManager::Instance())
    fleets->RequestViewFleet(fleet, true);
}

// The number keys select a ship in the fleet bar; pressing the same one again within select_timer flies the
// camera to it. With disable_preview_locate, the second press only selects while a target card is open.
// Returns true when the key was used.
bool SelectShip()
{
  constexpr GameFunction kShips[]   = {GameFunction::SelectShip1, GameFunction::SelectShip2, GameFunction::SelectShip3,
                                       GameFunction::SelectShip4, GameFunction::SelectShip5, GameFunction::SelectShip6,
                                       GameFunction::SelectShip7, GameFunction::SelectShip8};
  static auto            last_press = Clock::now();
  static int32_t         last_index = -1;

  int32_t index = 0;
  while (index < 8 && !Shortcut::IsDown(kShips[index]))
    ++index;
  // TODO: Shift+ship used to request a tow by the Discovery. Its bindings had drifted, so it never ran on
  // recent clients, and it was disabled until someone verifies it in game. Shift+ship does nothing meanwhile.
  if (index == 8 || Key::IsInputFocused() || Key::HasShift())
    return false;

  auto* fleet_bar = ObjectFinder<FleetBarViewController>::Get();
  if (!fleet_bar)
    return false;

  const auto now        = Clock::now();
  const bool can_locate = !Config::Get().disable_preview_locate || !TargetViewerShown();
  if (can_locate && index == last_index && fleet_bar->IsIndexSelected(index)
      && now - last_press < std::chrono::milliseconds(Config::Get().select_timer)) {
    auto* fleet = fleet_bar->SelectedFleet();
    if (!fleet)
      return true;
    ViewSelectedFleet(fleet);
  } else {
    fleet_bar->RequestSelect(index);
  }
  last_index = index;
  last_press = now;
  return true;
}

// Opens chat, or focuses its input when side chat is already open. Does not swallow the key.
void OpenChat()
{
  const bool side = Shortcut::IsDown(GameFunction::ShowChatSide1) || Shortcut::IsDown(GameFunction::ShowChatSide2);
  if (!side && !Shortcut::IsDown(GameFunction::ShowChat))
    return;
  auto* chat = ChatManager::Instance();
  if (!chat)
    return;

  if (chat->IsSideChatOpen()) {
    auto* view  = ObjectFinder<FullScreenChatViewController>::Get();
    auto* list  = view ? view->messageList() : nullptr;
    auto* input = list ? list->inputField() : nullptr;
    if (input)
      input->ActivateInputField();
  } else {
    chat->OpenChannel(ChatChannelCategory::Alliance, side ? ChatViewMode::Side : ChatViewMode::Fullscreen);
  }
}

// In chat, the chat shortcuts switch channel. Returns true when the key was used.
bool SelectChatChannel()
{
  auto* chat = ChatManager::Instance();
  if (!chat)
    return false;
  for (auto [shortcut, channel] : {std::pair{GameFunction::SelectChatGlobal, ChatChannelCategory::Global},
                                   std::pair{GameFunction::SelectChatAlliance, ChatChannelCategory::Alliance},
                                   std::pair{GameFunction::SelectChatPrivate, ChatChannelCategory::Private}}) {
    if (Shortcut::IsDown(shortcut)) {
      chat->OpenChannel(channel);
      return true;
    }
  }
  return false;
}

// Left and right step through docks, ship lists, artifacts and officer pages on the screens that have them,
// before they pan the camera. Returns true when something moved.
bool StepScreen(bool left)
{
  const auto& config = Config::Get();

  if (config.arrow_keys_to_select_ship && Hub::CurrentSection() == SectionID::ShipManagement_Details) {
    bool moved = false;
    for (auto* controller : ObjectFinder<ShipManagementViewController>::GetAll()) {
      auto* canvas =
          controller && controller->isActiveAndEnabled() ? GetCanvasControllerFromComponent(controller) : nullptr;
      auto* context = canvas && canvas->Visible() ? controller->Context() : nullptr;
      if (!context)
        continue;
      const int32_t count = context->ListCount();
      const int32_t next  = context->CurrentIndex() + (left ? -1 : 1);
      if (count <= 1 || context->CurrentIndex() < 0 || next < 0 || next >= count)
        continue;
      context->SetLastMovementWasToIncrement(!left);
      context->SetCurrentIndex(next);
      moved = true;
    }
    if (moved)
      return true;
  }

  if (config.arrow_keys_to_select_ship) {
    bool moved = false;
    for (auto* widget : ObjectFinder<AssignShipsWidget>::GetAll()) {
      auto* canvas = widget ? GetCanvasControllerFromComponent(widget) : nullptr;
      auto* list   = canvas && canvas->Visible() && widget->isActiveAndEnabled() ? widget->selectableList() : nullptr;
      if (!list || list->Count() <= 0)
        continue;
      // Skip empty dock slots.
      const int32_t count = list->Count();
      int32_t       index = list->SelectedIndex();
      do
        index += left ? -1 : 1;
      while (index >= 0 && index < count
             && !(list->DataItem(index) && static_cast<FleetPlayerData*>(list->DataItem(index))->HasShip()));
      if (index < 0 || index >= count)
        continue;
      list->RequestSelect(index);
      moved = true;
    }
    if (moved)
      return true;
  }

  bool moved = false;
  for (auto* controller : ObjectFinder<ArtifactHallDetailsViewController>::GetAll()) {
    if (!controller || !controller->isActiveAndEnabled())
      continue;
    left ? controller->PressLeftArrow() : controller->PressRightArrow();
    moved = true;
  }
  if (moved)
    return true;

  for (auto* selector : ObjectFinder<ElementSelectorViewController>::GetAll()) {
    if (!selector || !selector->isActiveAndEnabled())
      continue;
    left ? selector->PressDecrement() : selector->PressIncrement();
    moved = true;
  }
  return moved;
}

// Focuses the search box of whichever inventory, officer or ship list is on screen.
bool FocusSearchBox()
{
  // Never steal focus from the chat box.
  if (auto* chat = ObjectFinder<ChatMessageListLocalViewController>::Get(); chat && chat->inputFieldSelected())
    return false;

  const auto focus = [](auto* owner, CanvasController* canvas) {
    auto* field = owner ? owner->inputField() : nullptr;
    if (!field || !canvas || !canvas->Visible() || !owner->isActiveAndEnabled() || !field->isActiveAndEnabled())
      return false;
    field->Focus();
    return true;
  };
  // InventoryListViewController also covers the officer roster and the other inventory screens.
  for (auto* controller : ObjectFinder<InventoryListViewController>::GetAll())
    if (controller && focus(controller, controller->canvasController()))
      return true;
  for (auto* controller : ObjectFinder<OfficerAssignmentViewController>::GetAll())
    if (controller && focus(controller, controller->canvasController()))
      return true;
  for (auto* widget : ObjectFinder<AssignShipsWidget>::GetAll())
    if (widget && focus(widget, GetCanvasControllerFromComponent(widget)))
      return true;
  return false;
}

// Shortcuts that act on the screen rather than navigate. Returns true when the key was swallowed.
bool RunScreenShortcuts()
{
  if (Shortcut::IsDown(GameFunction::SelectCurrent)) {
    auto* fleet_bar = ObjectFinder<FleetBarViewController>::Get();
    if (auto* fleet = fleet_bar ? fleet_bar->SelectedFleet() : nullptr) {
      ViewSelectedFleet(fleet);
      return true;
    }
  }
  if (Shortcut::IsDown(GameFunction::ToggleQueue)) {
    Toggle<&Config::queue_enabled>();
    ResetQueueRecoveryTracking();
    return true;
  }
  OpenChat();
  if (Shortcut::IsDown(GameFunction::MoveLeft) && StepScreen(true))
    return true;
  if (Shortcut::IsDown(GameFunction::MoveRight) && StepScreen(false))
    return true;
  if (Config::Get().focus_search && Shortcut::IsDown(GameFunction::FocusSearch) && FocusSearchBox())
    return true;
  return RunAction();
}

// With disable_escape_exit, Escape no longer reaches the game's exit prompt. disable_escape_exit_timer > 0 lets
// a double tap within that many milliseconds through. Returns true when Escape is swallowed.
bool SwallowEscape()
{
  const auto& config = Config::Get();
  if (!config.disable_escape_exit || !Key::Pressed(KeyCode::Escape))
    return false;
  // A held key stays swallowed; only distinct presses count towards a double tap.
  if (config.disable_escape_exit_timer <= 0 || !Key::Down(KeyCode::Escape))
    return true;

  static Clock::time_point first_press{};
  const auto               now = Clock::now();
  if (first_press == Clock::time_point{}
      || now - first_press > std::chrono::milliseconds(config.disable_escape_exit_timer)) {
    first_press = now;
    return true;
  }
  // Consume the pair so a quick third press starts a new one.
  first_press = {};
  return false;
}

void RunFrameUpdates()
{
  // Each returns at once when its feature is off. They run in every hotkey mode, including Scopely's and while
  // hotkeys are toggled off. An exception must not unwind into the game's native frame.
  for (auto update : {AllianceGamesStoreUpdate, AssignShipEnterKeyUpdate, ChatOptionsUpdate,
                      DailyFactionBulkClaimUpdate, fleet_watch::Tick, FleetSyncUpdate, GalacticAnomalyTimerUpdate,
                      WarpActionLabelUpdate}) {
    try {
      update();
    } catch (const std::exception& error) {
      spdlog::warn("[FrameUpdate] update failed: {}", error.what());
    } catch (...) {
      spdlog::warn("[FrameUpdate] update failed with an unknown exception");
    }
  }
}

// The shortcut dispatch for one frame. Returns true when the game's Update must not run, so it does not also
// handle the key.
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

bool HandleShortcuts()
{
  auto& config = Config::Get();

  if (Shortcut::IsDown(GameFunction::DisableHotKeys)) {
    HideShortcutBadges();
    config.hotkeys_enabled = false;
    spdlog::warn("Setting hotkeys to DISABLED");
    return true;
  }
  if (Shortcut::IsDown(GameFunction::EnableHotKeys)) {
    config.hotkeys_enabled = true;
    spdlog::warn("Setting hotkeys to ENABLED");
    return true;
  }

  if (PressAwaitedButton())
    return true;

  // Scopely's hotkeys are the game's own; mod hotkeys switched off block both.
  if (config.use_scopely_hotkeys && config.hotkeys_enabled)
    return false;
  if (!config.hotkeys_enabled)
    return true;

  if (Shortcut::IsDown(GameFunction::ToggleShortcutHints) && ToggleShortcutBadges())
    return true;

  if (Shortcut::IsDown(GameFunction::Restart)) {
    spdlog::info("Clearing localisation cache and restarting");
    LanguageManager::ClearCache();
    Hub::get_App()->Reload();
    return true;
  }
  if (Shortcut::IsDown(GameFunction::Quit)) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), 1);
#else
    Hub::get_App()->Quit();
    return true;
#endif
  }

  if (SelectShip())
    return true;

  // The game only clears a focused text field from its back button; Escape should too.
  if (Key::Pressed(KeyCode::Escape) && (Key::IsInputFocused() || Hub::IsInChat())) {
    Key::ClearInputFocus();
    return true;
  }

  if (Hub::IsInChat()) {
    if (SelectChatChannel())
      return true;
  } else if (!Key::IsInputFocused() && RunScreenShortcuts()) {
    return true;
  }

  return SpaceActionsUpdate() || SwallowEscape();
}

void ScreenManager_Update_Hook(auto original, ScreenManager* _this)
{
  RunFrameUpdates();
  if (hotkeys_installed) {
    UpdateManageShipDockSelection();
    UpdateExpiredAbilityButtons();
  }
  if (hotkeys_installed && HandleShortcuts())
    return;
  original(_this);
}

// With Scopely hotkeys off, the game never registers its own input actions, so its shortcuts cannot fire.
void ShortcutsManager_InitializeActions_Hook(auto original, ShortcutsManager* _this)
{
  if (Config::Get().use_scopely_hotkeys)
    original(_this);
}
} // namespace

bool InstallScreenManagerUpdateHook()
{
  static bool installed = false;
  if (!installed) {
    if (auto m = ScreenManager::Class::Method("Update")) {
      if (!SPUD_STATIC_DETOUR(m.Pointer(), ScreenManager_Update_Hook))
        return false;
      installed = true;
    }
  }
  return installed;
}

void InstallHotkeyHooks()
{
  // The badges only make sense when the game's own input actions are under the mod's control.
  if (auto m = ShortcutsManager::Class::Method("InitializeActions")) {
    SPUD_STATIC_DETOUR(m.Pointer(), ShortcutsManager_InitializeActions_Hook);
    InstallShortcutHintHooks();
  }
  hotkeys_installed = InstallScreenManagerUpdateHook();
  InstallSpaceActionHooks();
}

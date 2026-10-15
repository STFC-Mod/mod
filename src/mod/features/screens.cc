// Shortcuts that open game screens, press the buttons on them, and step through them: the show_* navigation
// shortcuts, Ship Swap and the officer swap and preset buttons, the arrow keys on docks, artifacts and selectors, and
// focusing the search box.

#include "config.h"
#include "features/features.h"
#include "shortcuts.h"

#include "game/app.h"
#include "game/fleet.h"
#include "game/inventory.h"
#include "game/navigation.h"
#include "game/officers.h"
#include "game/ui.h"

#include <il2cpp/il2cpp.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <unity/tracker.h>

namespace
{
// Ship Swap and Officer Swap live on the Manage Ships screen, and Officer Presets on the officer assignment screen
// that Officer Swap opens. Their shortcuts open those screens first, then press the button once it exists. 300
// frames is a few seconds at the game's frame rate.
constexpr int kButtonWaitFrames = 300;

enum class ButtonStage { kShipSwap, kOfficerSwap, kOfficerAssignment, kOfficerPresets };

struct ButtonWait {
  ButtonStage stage;
  int         frames = kButtonWaitFrames;
};
std::optional<ButtonWait> pending_button;

// Runs one of the game's own shortcut handlers. A handler missing from this client is reported once by the binding
// layer and does nothing.
template <il2cpp::Name Action> void Native()
{
  if (auto* manager = ShortcutsManager::Instance())
    manager->Run<Action>();
  else
    spdlog::warn("[Hotkeys] native {} shortcut is unavailable", Action.View());
}

template <SectionID Section> void Goto()
{ Hub::GotoSection(Section); }

// Galaxy and system views keep their state in section storage; reuse it so the camera returns where it was.
template <SectionID Section> void GotoNavigation()
{
  auto* manager = Hub::Sections();
  auto* storage = manager ? manager->sectionStorage() : nullptr;
  if (auto* state = storage ? storage->GetState(Section) : nullptr)
    Hub::GotoSection(Section, state);
  else
    NavigationSectionManager::ChangeNavigationSection(Section);
}

void ShowBookmarks()
{
  if (auto* bookmarks = BookmarksManager::Instance())
    bookmarks->ViewBookmarks();
  else
    Hub::GotoSection(SectionID::Bookmarks_Main);
}

void ShowCoordinateSearch()
{
  if (auto* bookmarks = BookmarksManager::Instance())
    return bookmarks->ViewCoordinateSearch();
  spdlog::warn("[ShowLookup] BookmarksManager instance not available, falling back to main bookmarks");
  Hub::GotoSection(SectionID::Bookmarks_Main);
}

void ShowHaven()
{ PlanetaryBaseManager::ViewOwnHaven(); }

// Presses the button `pick` returns on the first visible, active Controller.
template <typename Controller> bool PressButton(unity::Button* (*pick)(Controller*))
{
  for (auto* controller : unity::Instances<Controller>()) {
    auto* button = controller && CanvasController::Shows(controller) && controller->isActiveAndEnabled()
                       ? pick(controller)
                       : nullptr;
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
  return PressButton<OfficerAssignmentViewController>(
      [](OfficerAssignmentViewController* controller) -> unity::Button* {
        auto* widget = controller->officerPresetsButton();
        return widget ? widget->TheButton() : nullptr;
      });
}

void OpenManageShips()
{
  auto* fleet_bar = unity::Newest<FleetBarViewController>();
  if (auto* fleet = fleet_bar ? fleet_bar->SelectedFleet() : nullptr)
    fleet_bar->fleetPanelController()->RequestAction(fleet, ActionType::Manage, 0, ActionBehaviour::Default);
}

// Opens Manage Ships for the selected fleet, for a button that is only there. False, with a warning, without one.
bool OpenManageShipsFor(const char* what)
{
  auto* fleet_bar = unity::Newest<FleetBarViewController>();
  if (!fleet_bar || !fleet_bar->SelectedFleet()) {
    spdlog::warn("[Hotkeys] unable to open Manage Ships for {}", what);
    return false;
  }
  OpenManageShips();
  return true;
}

void ShowShipSwap()
{
  if (!PressShipSwapButton() && OpenManageShipsFor("Ship Swap"))
    pending_button = ButtonWait{ButtonStage::kShipSwap};
}

void ShowOfficerSwap()
{
  if (!PressOfficerSwapButton() && OpenManageShipsFor("Officer Swap"))
    pending_button = ButtonWait{ButtonStage::kOfficerSwap};
}

void ShowOfficerPresets()
{
  if (PressOfficerPresetsButton())
    return;
  if (PressOfficerSwapButton())
    pending_button = ButtonWait{ButtonStage::kOfficerPresets};
  else if (OpenManageShipsFor("Officer Presets"))
    pending_button = ButtonWait{ButtonStage::kOfficerAssignment};
}

struct ScreenShortcut {
  Action shortcut;
  void (*run)();
  // Opening Manage Ships lets the game see the key too.
  bool consumes = true;
};

// Checked in order; the first pressed one runs.
constexpr ScreenShortcut kScreens[] = {
    {Action::kShowQTrials, Goto<SectionID::ChallengeSelection>},
    {Action::kShowBookmarks, ShowBookmarks},
    {Action::kShowLookup, ShowCoordinateSearch},
    {Action::kShowRefinery, Goto<SectionID::Shop_Refining_List>},
    {Action::kShowFactions, Goto<SectionID::Shop_MainFactions>},
    {Action::kShowStationExterior, Goto<SectionID::Starbase_Exterior>},
    {Action::kNativeShortcutGalaxy, Native<"OnGalaxyAction">},
    {Action::kShowGalaxy, GotoNavigation<SectionID::Navigation_Galaxy>},
    {Action::kShowStationInterior, Goto<SectionID::Starbase_Interior>},
    {Action::kShowHaven, ShowHaven},
    {Action::kShowSystem, GotoNavigation<SectionID::Navigation_System>},
    {Action::kShowArtifacts, Goto<SectionID::ArtifactHall_Inventory>},
    {Action::kShowShipConstruction, Native<"OnShipsAction">},
    {Action::kShowOfficerSwap, ShowOfficerSwap},
    {Action::kShowShipSwap, ShowShipSwap},
    {Action::kShowShields, Native<"OnPeaceShieldAction">},
    {Action::kShowBattlelogs, Native<"OnInboxAction">},
    {Action::kShowInventory, Goto<SectionID::InventoryList>},
    {Action::kShowMissions, Goto<SectionID::Missions_AcceptedList>},
    {Action::kShowResearch, Goto<SectionID::Research_LandingPage>},
    {Action::kShowScrapYard, Goto<SectionID::ShipScrapping_List>},
    {Action::kShowOfficers, Goto<SectionID::OfficerInventory>},
    {Action::kShowOfficerPresets, ShowOfficerPresets},
    // TODO: opens the first fleet commander (Spock), not the selected fleet's.
    {Action::kShowCommander, Goto<SectionID::FleetCommander_Management>},
    {Action::kShowAwayTeam, Goto<SectionID::Missions_AwayTeamsList>},
    {Action::kNativeShortcutEvents, Native<"OnEventsAction">},
    {Action::kShowEvents, Goto<SectionID::Tournament_Group_Selection>},
    {Action::kShowExoComp, Goto<SectionID::Consumables>},
    {Action::kShowDaily, Goto<SectionID::Missions_DailyGoals>},
    {Action::kShowGifts, Goto<SectionID::Shop_List>},
    {Action::kShowAlliance, Goto<SectionID::Alliance_Main>},
    {Action::kShowAllianceHelp, Goto<SectionID::Alliance_Help>},
    {Action::kShowAllianceArmada, Goto<SectionID::Alliance_Armadas>},
    {Action::kShowSettings, Goto<SectionID::GameSettings>},
    {Action::kShowShips, OpenManageShips, false},
};

// Left and right step through docks, ship lists, artifacts and officer pages on the screens that have them,
// before they pan the camera. Returns true when something moved.
bool StepScreen(bool left)
{
  const auto& config = Config::Get();

  if (config.ui.arrow_keys_to_select_ship && Hub::CurrentSection() == SectionID::ShipManagement_Details) {
    bool moved = false;
    for (auto* controller : unity::Instances<ShipManagementViewController>()) {
      auto* context = controller && controller->isActiveAndEnabled() && CanvasController::Shows(controller)
                          ? controller->Context()
                          : nullptr;
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

  if (config.ui.arrow_keys_to_select_ship) {
    bool moved = false;
    for (auto* widget : unity::Instances<AssignShipsWidget>()) {
      auto* list = widget && CanvasController::Shows(widget) && widget->isActiveAndEnabled() ? widget->selectableList()
                                                                                             : nullptr;
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
  for (auto* controller : unity::Instances<ArtifactHallDetailsViewController>()) {
    if (!controller || !controller->isActiveAndEnabled())
      continue;
    left ? controller->PressLeftArrow() : controller->PressRightArrow();
    moved = true;
  }
  if (moved)
    return true;

  for (auto* selector : unity::Instances<ElementSelectorViewController>()) {
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
  const auto focus = [](auto* owner) {
    auto* field = owner ? owner->inputField() : nullptr;
    if (!field || !CanvasController::Shows(owner) || !owner->isActiveAndEnabled() || !field->isActiveAndEnabled())
      return false;
    field->Focus();
    return true;
  };
  // InventoryListViewController also covers the officer roster and the other inventory screens.
  for (auto* controller : unity::Instances<InventoryListViewController>())
    if (focus(controller))
      return true;
  for (auto* controller : unity::Instances<OfficerAssignmentViewController>())
    if (focus(controller))
      return true;
  for (auto* widget : unity::Instances<AssignShipsWidget>())
    if (focus(widget))
      return true;
  return false;
}
} // namespace

bool PressAwaitedButton()
{
  const auto& control = Config::Get().control;
  if (!control.hotkeys_enabled || control.use_scopely_hotkeys) {
    pending_button.reset();
    return false;
  }
  if (!pending_button)
    return false;

  bool        pressed = false;
  const char* name    = "Officer Presets";
  switch (pending_button->stage) {
    case ButtonStage::kShipSwap:
      name    = "Ship Swap";
      pressed = PressShipSwapButton();
      break;
    case ButtonStage::kOfficerSwap:
      name    = "Officer Swap";
      pressed = PressOfficerSwapButton();
      break;
    case ButtonStage::kOfficerAssignment:
      pressed = PressOfficerPresetsButton();
      if (!pressed && PressOfficerSwapButton())
        pending_button->stage = ButtonStage::kOfficerPresets;
      break;
    case ButtonStage::kOfficerPresets:
      pressed = PressOfficerPresetsButton();
      break;
  }
  if (pressed) {
    pending_button.reset();
    return true;
  }
  if (--pending_button->frames == 0) {
    pending_button.reset();
    spdlog::warn("[Hotkeys] timed out waiting for the {} button", name);
  }
  return false;
}

bool ScreenShortcuts()
{
  if (Shortcut::IsDown(Action::kMoveLeft) && StepScreen(true))
    return true;
  if (Shortcut::IsDown(Action::kMoveRight) && StepScreen(false))
    return true;
  if (Config::Get().ui.focus_search && Shortcut::IsDown(Action::kFocusSearch) && FocusSearchBox())
    return true;
  for (const auto& screen : kScreens) {
    if (Shortcut::IsDown(screen.shortcut)) {
      // A new screen request replaces any button still waiting on the previous screen.
      pending_button.reset();
      screen.run();
      return screen.consumes;
    }
  }
  return false;
}

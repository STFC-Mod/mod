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
#include <spdlog/spdlog.h>
#include <unity/tracker.h>

#include <algorithm>
#include <optional>

namespace
{
// --- Opening screens ---

// Runs one of the game's own shortcut handlers. A handler missing from this client is reported once by the binding
// layer and does nothing.
template <il2cpp::Name Handler> void Native()
{
  if (auto* manager = ShortcutsManager::Instance())
    manager->Run<Handler>();
  else
    spdlog::warn("[Hotkeys] native {} shortcut is unavailable", Handler.View());
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
  if (auto* bookmarks = BookmarksManager::Instance()) {
    bookmarks->ViewCoordinateSearch();
  } else {
    spdlog::warn("[Hotkeys] no bookmarks manager for the coordinate search; opening bookmarks instead");
    Hub::GotoSection(SectionID::Bookmarks_Main);
  }
}

// Opens Manage Ships for the fleet selected in the fleet bar. False when none is selected.
bool OpenManageShips()
{
  auto* panel = FleetBarViewController::NewestPanel();
  auto* fleet = panel ? panel->fleet() : nullptr;
  if (!fleet)
    return false;
  panel->RequestAction(fleet, ActionType::Manage, 0, ActionBehaviour::Default);
  return true;
}

// --- Swap and preset buttons ---

// Ship Swap and Officer Swap live on the Manage Ships screen, and Officer Presets on the officer assignment screen
// that Officer Swap opens. Their shortcuts open those screens first, then press the button once it exists. 300
// frames is a few seconds at the game's frame rate.
constexpr int kButtonWaitFrames = 300;

// kOfficerAssignment waits on Manage Ships for Officer Swap, on the way to Officer Presets.
enum class ButtonStage { kShipSwap, kOfficerSwap, kOfficerAssignment, kOfficerPresets };

struct ButtonWait {
  ButtonStage stage;
  int         frames = kButtonWaitFrames;
};
std::optional<ButtonWait> pending_button;

// Presses the button `pick` returns on the first visible, active Controller.
template <typename Controller> bool PressButton(unity::Button* (*pick)(Controller*))
{
  for (auto* controller : unity::Instances<Controller>()) {
    if (auto* button = CanvasController::OnScreen(controller) ? pick(controller) : nullptr) {
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
        return widget ? widget->UnityButton() : nullptr;
      });
}

// Opens Manage Ships for a button that only it has. False, with a warning, when no fleet is selected.
bool OpenManageShipsFor(const char* what)
{
  if (OpenManageShips())
    return true;
  spdlog::warn("[Hotkeys] no fleet is selected, so Manage Ships cannot open for {}", what);
  return false;
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

// --- Shortcut table ---

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
    {Action::kShowHaven, PlanetaryBaseManager::ViewOwnHaven},
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
    {Action::kShowShips, [] { OpenManageShips(); }, false},
};

// --- Arrow keys and search ---

// Left and right step through docks, ship lists, artifacts and officer pages on the screens that have them, before
// they pan the camera. Each Step* moves one screen by `step`, -1 or 1, and says whether it moved.

bool StepShipDetails(ShipManagementViewController* controller, int32_t step)
{
  auto* context = CanvasController::OnScreen(controller) ? controller->Context() : nullptr;
  if (!context)
    return false;
  const int32_t count   = context->ListCount();
  const int32_t current = context->CurrentIndex();
  const int32_t next    = current + step;
  if (count <= 1 || current < 0 || next < 0 || next >= count)
    return false;
  context->SetLastMovementWasToIncrement(step > 0);
  context->SetCurrentIndex(next);
  return true;
}

// Skips empty dock slots.
bool StepDock(AssignShipsWidget* widget, int32_t step)
{
  auto* list = CanvasController::OnScreen(widget) ? widget->selectableList() : nullptr;
  if (!list)
    return false;
  const int32_t count = list->Count();
  for (int32_t index = list->SelectedIndex() + step; index >= 0 && index < count; index += step) {
    if (auto* fleet = static_cast<FleetPlayerData*>(list->DataItem(index)); fleet && fleet->HasShip()) {
      list->RequestSelect(index);
      return true;
    }
  }
  return false;
}

bool StepArtifact(ArtifactHallDetailsViewController* controller, int32_t step)
{
  if (!controller->isActiveAndEnabled())
    return false;
  step < 0 ? controller->PressLeftArrow() : controller->PressRightArrow();
  return true;
}

bool StepSelector(ElementSelectorViewController* selector, int32_t step)
{
  if (!selector->isActiveAndEnabled())
    return false;
  step < 0 ? selector->PressDecrement() : selector->PressIncrement();
  return true;
}

// Steps every live Screen, and says whether any of them moved.
template <typename Screen> bool StepAll(bool (*step_one)(Screen*, int32_t), int32_t step)
{
  bool moved = false;
  for (auto* screen : unity::Instances<Screen>())
    moved |= step_one(screen, step);
  return moved;
}

// The first kind of screen that moves wins.
bool StepScreen(int32_t step)
{
  if (Config::Get().ui.arrow_keys_to_select_ship) {
    if (Hub::CurrentSection() == SectionID::ShipManagement_Details && StepAll(StepShipDetails, step))
      return true;
    if (StepAll(StepDock, step))
      return true;
  }
  return StepAll(StepArtifact, step) || StepAll(StepSelector, step);
}

// Focuses the search box of whichever inventory, officer or ship list is on screen.
bool FocusSearchBox()
{
  const auto focus = [](auto* owner) {
    auto* field = CanvasController::OnScreen(owner) ? owner->inputField() : nullptr;
    if (!field || !field->isActiveAndEnabled())
      return false;
    field->Focus();
    return true;
  };
  // InventoryListViewController also covers the officer roster and the other inventory screens.
  return std::ranges::any_of(unity::Instances<InventoryListViewController>(), focus)
         || std::ranges::any_of(unity::Instances<OfficerAssignmentViewController>(), focus)
         || std::ranges::any_of(unity::Instances<AssignShipsWidget>(), focus);
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
  if (Shortcut::IsDown(Action::kMoveLeft) && StepScreen(-1))
    return true;
  if (Shortcut::IsDown(Action::kMoveRight) && StepScreen(1))
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

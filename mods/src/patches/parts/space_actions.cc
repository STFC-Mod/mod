// The selected fleet and the target viewers in space: selecting ships with the number keys, engaging, scanning,
// mining, queueing, warping, recalling and repairing from the keyboard, dismissing viewers with Escape, opening a
// target's cargo when it is scanned, and the action queue switch.

#include "action_queue.h"
#include "config.h"
#include "patches/patches.h"
#include "patches/shortcuts.h"
#include <chrono>

#include "prime/Hub.h"
#include "prime/fleet.h"
#include "prime/navigation.h"
#include "prime/ui.h"
#include "prime/viewers.h"

#include <il2cpp/hook.h>
#include <spdlog/spdlog.h>
#include <unity/input.h>
#include <unity/tracker.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <span>
#include <utility>

namespace
{
// Frames left in which to show the cargo of a pre-scanned target. The rewards widget can appear a few frames
// after the pre-scan itself, so a request keeps trying for a while.
int show_cargo_frames = 0;

struct Pressed {
  bool primary       = false;
  bool secondary     = false;
  bool queue         = false;
  bool queue_clear   = false;
  bool recall        = false;
  bool recall_cancel = false;
  bool repair        = false;
  // A replay of last frame's press, see replay.
  bool retry = false;

  bool Any() const
  { return primary || secondary || queue || queue_clear || recall || recall_cancel || repair; }
};

// The target's hull type is only known once the target's data has arrived; until then it reads as Any. A press
// that lands in that window is replayed, as itself, on the next frame only.
std::optional<Pressed> replay;

bool Shown(VisibilityController* controller)
{ return controller && controller->IsShown(); }

template <typename T> bool TargetShown(T* widget, const Pressed& pressed)
{
  if (!widget || !Shown(widget->visibilityController()))
    return false;
  if (!pressed.primary || !Config::Get().control.primary_requires_visible_target)
    return true;
  auto* parent = widget->Parent();
  return parent && parent->IsShowing();
}

// The cards that show a target or a place in space.
template <typename... Viewers> struct ViewerList {
  static bool AnyShown()
  {
    return (std::ranges::any_of(unity::Instances<Viewers>(),
                                [](Viewers* widget) { return widget && Shown(widget->visibilityController()); })
            || ...);
  }

  // Hides the shown viewers of the first type that has any.
  static bool HideFirst()
  { return (Hide<Viewers>() || ...); }

private:
  template <typename T> static bool Hide()
  {
    bool hid = false;
    for (auto* widget : unity::Instances<T>()) {
      if (widget && Shown(widget->visibilityController())) {
        widget->HideAllViewers();
        hid = true;
      }
    }
    return hid;
  }
};

using TargetViewers = ViewerList<AllianceStarbaseObjectViewerWidget, ArmadaObjectViewerWidget,
                                 CelestialObjectViewerWidget, EmbassyObjectViewer, HousingObjectViewerWidget,
                                 MiningObjectViewerWidget, MissionsObjectViewerWidget, PreScanTargetWidget>;

HullType TargetHullType(BattleTargetData* context)
{
  auto* deployed = context ? context->TargetFleetDeployedData() : nullptr;
  auto* hull     = deployed ? deployed->Hull() : nullptr;
  return hull ? hull->Type() : HullType::Any;
}

// The armada viewer's state. A viewer without a visibility controller counts as visible.
bool ArmadaViewerShown(ArmadaObjectViewerWidget* armada)
{
  if (!armada)
    return false;
  auto* controller = armada->visibilityController();
  if (!controller) {
    spdlog::warn("ArmadaWidget has no visibility controller, using default Visible state");
    return true;
  }
  const auto state = controller->State();
  return state == VisibilityState::Visible || state == VisibilityState::Show;
}

// The primary action joins a wave defense first: through the set-course widget's join button when it listens, else
// the primary button of a shown wave defense viewer. Both press the game's own button, so its checks still run.
bool JoinWaveDefense()
{
  if (auto* navigation = unity::Newest<NavigationInteractionUIViewController>();
      navigation && navigation->TryPressJoinWaveDefense())
    return true;
  // Query active viewers without adding constructor detours for this popup or its subclasses.
  if (auto* viewers = unity::FindObjectsOfType<WaveDefenseObjectViewerWidget>()) {
    for (auto* widget : *viewers) {
      auto* parent = widget && Shown(widget->visibilityController()) ? widget->Parent() : nullptr;
      if (parent && parent->IsShowing()) {
        // Use the displayed native action, including its eligibility and confirmation checks.
        widget->PressPrimaryButton();
        return true;
      }
    }
  }
  return false;
}

// Requests `type` on the selected fleet when it is in one of `states`. With `help_state`, a fleet in that state
// (or one the action was just requested for) also asks its alliance for help.
bool RequestFleetAction(FleetBarViewController* fleet_bar, ActionType type, std::span<const FleetState> states,
                        FleetState help_state = FleetState::Unknown)
{
  auto* panel = fleet_bar->fleetPanelController();
  auto* fleet = panel ? panel->fleet() : nullptr;
  if (!fleet)
    return false;

  bool requested = false;
  if (std::ranges::find(states, fleet->CurrentState()) != states.end()) {
    NavigationSectionManager::HideInteraction();
    requested = panel->RequestAction(fleet, type, 0, ActionBehaviour::Default);
  }
  if (help_state != FleetState::Unknown && (requested || fleet->CurrentState() == help_state))
    requested = requested || panel->RequestAction(fleet, type, 0, ActionBehaviour::AskHelp);
  spdlog::trace("Fleet action {} on fleet {} in state {}: {}", type.flagValue, fleet->Id(),
                static_cast<int>(fleet->CurrentState()), requested);
  return requested;
}

// Acts on a shown pre-scan card. Returns true when the press was used, including when it was deferred to the next
// frame.
bool ActOnPreScan(PreScanTargetWidget* pre_scan, const Pressed& pressed, FleetPlayerData* fleet,
                  ActionQueueManager* queue)
{
  auto* buttons = pre_scan->scanEngageButtonsWidget();

  // A mining node's card sits on top of the pre-scan: scan it or start mining.
  if (auto* mine = unity::Newest<MiningObjectViewerWidget>(); TargetShown(mine, pressed)) {
    if (pressed.secondary && buttons) {
      buttons->OnScanButtonClicked();
      return true;
    }
    if (pressed.primary) {
      mine->MineClicked();
      return true;
    }
  }

  auto* add_to_queue = pre_scan->addToQueueButtonWidget();
  if (pressed.queue && queue->IsQueueUnlocked() && add_to_queue && buttons) {
    const auto type = TargetHullType(buttons->Context());
    if (type != HullType::ArmadaTarget && (type != HullType::Any || pressed.retry)) {
      if (add_to_queue->isActiveAndEnabled()) {
        if (auto* listener = add_to_queue->SemaphoreListener(); listener && !queue->IsQueueFull(fleet)) {
          if (auto* button = listener->TheButton()) {
            button->Press();
            TargetViewers::HideFirst();
          }
        }
        return true;
      }
      if (type == HullType::Any) {
        replay = pressed;
        return true;
      }
    }
  }

  if (pressed.secondary && buttons) {
    buttons->OnScanButtonClicked();
    return true;
  }

  if (pressed.primary && buttons && buttons->enabled()) {
    const auto type = TargetHullType(buttons->Context());
    // An armada target whose armada card is already open is joined from that card instead.
    bool can_engage = type != HullType::Any;
    if (type == HullType::ArmadaTarget && ArmadaViewerShown(unity::Newest<ArmadaObjectViewerWidget>()))
      can_engage = false;
    else if (pressed.retry)
      can_engage = true;

    if (can_engage) {
      if (type != HullType::ArmadaTarget) {
        buttons->OnEngageButtonClicked();
      } else if (auto* attack = pre_scan->armadaAttackButton(); attack && attack->isActiveAndEnabled()) {
        if (auto* button = attack->TheButton())
          button->Press();
      } else {
        buttons->OnArmadaButtonClicked();
      }
      return true;
    }
    if (type == HullType::Any) {
      replay = pressed;
      return true;
    }
  }
  return false;
}

// Acts on whatever other viewer is open: a mining node, a system node, or an empty spot in space. Returns true
// when the press was used.
bool ActOnViewer(const Pressed& pressed)
{
  if (auto* mine = unity::Newest<MiningObjectViewerWidget>(); TargetShown(mine, pressed)) {
    if (auto* buttons = mine->scanEngageButtonsWidget(); pressed.secondary && buttons) {
      if (!buttons->Context())
        return false;
      buttons->OnScanButtonClicked();
      return true;
    }
    if (pressed.primary) {
      mine->MineClicked();
      return true;
    }
    return false;
  }

  if (auto* node = unity::Newest<StarNodeObjectViewerWidget>();
      node && node->Context()
      && (!pressed.primary || !Config::Get().control.primary_requires_visible_target || TargetShown(node, pressed))) {
    if (pressed.secondary) {
      node->OnViewButtonActivation();
      return true;
    }
    if (pressed.primary) {
      node->InitiateWarp();
      return true;
    }
    return false;
  }

  auto* navigation = unity::Newest<NavigationInteractionUIViewController>();
  if (!navigation || !pressed.primary)
    return false;

  auto* armada = unity::Newest<ArmadaObjectViewerWidget>();
  if (!ArmadaViewerShown(armada)) {
    if (Config::Get().control.primary_requires_visible_target && !navigation->IsSetCourseVisible())
      return false;
    navigation->OnSetCourseButtonClick();
    return true;
  }
  if (auto* join = armada->joinContext(); join && join->Interactable()) {
    armada->ValidateThenJoinArmada();
    return true;
  }
  return false;
}

void ActOnSelectedFleet(FleetBarViewController* fleet_bar, const Pressed& pressed)
{
  if (Shortcut::IsDown(Action::kActionPrimary) && JoinWaveDefense())
    return;
  if (!fleet_bar)
    return;
  auto* panel = fleet_bar->fleetPanelController();
  auto* fleet = panel ? panel->fleet() : nullptr;
  auto* queue = ActionQueueManager::Instance();
  if (!fleet || !queue)
    return;

  if (pressed.queue_clear) {
    queue->ClearQueue(fleet);
    return;
  }
  const auto state = fleet->CurrentState();
  if (pressed.recall_cancel && (state == FleetState::WarpCharging || state == FleetState::Warping)) {
    panel->CancelButtonClicked();
    return;
  }

  for (auto* pre_scan : unity::Instances<PreScanTargetWidget>())
    if (TargetShown(pre_scan, pressed) && ActOnPreScan(pre_scan, pressed, fleet, queue))
      return;

  if (ActOnViewer(pressed))
    return;

  static constexpr FleetState kRecallable[] = {FleetState::IdleInSpace, FleetState::Impulsing, FleetState::Mining,
                                               FleetState::Capturing};
  static constexpr FleetState kRepairable[] = {FleetState::Docked, FleetState::Destroyed};
  if (pressed.recall && RequestFleetAction(fleet_bar, ActionType::Recall, kRecallable))
    return;
  if (pressed.repair)
    RequestFleetAction(fleet_bar, ActionType::Repair, kRepairable, FleetState::Repairing);
}

// Toggles a shown pre-scan card's cargo panel. Opening is retried for a few frames, see show_cargo_frames.
void ToggleTargetCargo()
{
  for (auto* pre_scan : unity::Instances<PreScanTargetWidget>()) {
    auto* rewards = pre_scan ? pre_scan->rewardsButtonWidget() : nullptr;
    auto* cargo   = rewards ? rewards->rewardsController() : nullptr;
    if (!cargo || !Shown(pre_scan->visibilityController()))
      continue;
    if (cargo->IsShown())
      cargo->Hide();
    else
      show_cargo_frames = 5;
  }
}

void ShowPendingTargetCargo()
{
  for (auto* pre_scan : unity::Instances<PreScanTargetWidget>()) {
    auto* rewards = pre_scan ? pre_scan->rewardsButtonWidget() : nullptr;
    auto* cargo   = rewards ? rewards->rewardsController() : nullptr;
    if (cargo && Shown(pre_scan->visibilityController()) && !cargo->IsShown())
      cargo->Show(true);
  }
  --show_cargo_frames;
}

// Whether the target's cargo opens by itself, per the show_*_cargo settings for its kind.
bool ShowsCargoByDefault(RewardsButtonWidget* widget)
{
  const auto& config = Config::Get();
  auto*       target = widget && config.ui.show_cargo_default ? widget->Context() : nullptr;
  if (!target)
    return false;

  auto* deployed = target->TargetFleetDeployedData();
  if (!deployed)
    return config.ui.show_station_cargo;
  switch (deployed->FleetType()) {
    case DeployedFleetType::Player:
      return config.ui.show_player_cargo;
    case DeployedFleetType::Marauder: {
      auto* hull = deployed->Hull();
      return hull && hull->Type() == HullType::ArmadaTarget ? config.ui.show_armada_cargo
                                                            : config.ui.show_hostile_cargo;
    }
    default:
      return false;
  }
}

void ShowCargoByDefault(RewardsButtonWidget* widget)
{
  auto* cargo = widget ? widget->rewardsController() : nullptr;
  if (cargo && ShowsCargoByDefault(widget)) {
    cargo->Show(true);
    show_cargo_frames = 1;
  }
}

void RewardsButtonWidget_OnDidBindContext_Hook(auto original, RewardsButtonWidget* _this)
{
  original(_this);
  ShowCargoByDefault(_this);
}

void PreScanTargetWidget_ShowWithFleet_Hook(auto original, PreScanTargetWidget* _this, void* fleet)
{
  original(_this, fleet);
  if (_this)
    ShowCargoByDefault(_this->rewardsButtonWidget());
}

// queue_enabled switches the game's action queue off by telling it the queue is locked.
bool ActionQueueManager_IsQueueUnlocked_Hook(auto original, ActionQueueManager* _this)
{ return Config::Get().control.queue_enabled && original(_this); }

void ViewSelectedFleet(FleetPlayerData* fleet)
{
  NavigationSectionManager::HideInteraction();
  if (auto* fleets = FleetsManager::Instance())
    fleets->RequestViewFleet(fleet, true);
}

void InstallQueueSwitch()
{ IL2CPP_HOOK(ActionQueueManager::Class::Method("IsQueueUnlocked"), ActionQueueManager_IsQueueUnlocked_Hook); }
} // namespace

bool TargetViewerShown()
{ return TargetViewers::AnyShown(); }

bool SelectShipShortcuts()
{
  using Clock = std::chrono::steady_clock;
  // The number keys select a ship in the fleet bar; pressing the same one again within select_timer flies the
  // camera to it. With disable_preview_locate, the second press only selects while a target card is open.
  constexpr Action kShips[] = {Action::kSelectShip1, Action::kSelectShip2, Action::kSelectShip3, Action::kSelectShip4,
                               Action::kSelectShip5, Action::kSelectShip6, Action::kSelectShip7, Action::kSelectShip8};
  static auto      last_press = Clock::now();
  static int32_t   last_index = -1;

  int32_t index = 0;
  while (index < 8 && !Shortcut::IsDown(kShips[index]))
    ++index;
  // TODO: Shift+ship used to request a tow by the Discovery. Its bindings had drifted, so it never ran on
  // recent clients, and it was disabled until someone verifies it in game. Shift+ship does nothing meanwhile.
  if (index == 8 || unity::Key::IsInputFocused())
    return false;

  auto* fleet_bar = unity::Newest<FleetBarViewController>();
  if (!fleet_bar)
    return false;

  const auto now        = Clock::now();
  const bool can_locate = !Config::Get().ui.disable_preview_locate || !TargetViewerShown();
  if (can_locate && index == last_index && fleet_bar->IsIndexSelected(index)
      && now - last_press < std::chrono::milliseconds(Config::Get().control.select_timer)) {
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

bool FleetShortcuts()
{
  if (Shortcut::IsDown(Action::kSelectCurrent)) {
    auto* fleet_bar = unity::Newest<FleetBarViewController>();
    if (auto* fleet = fleet_bar ? fleet_bar->SelectedFleet() : nullptr) {
      ViewSelectedFleet(fleet);
      return true;
    }
  }
  if (Shortcut::IsDown(Action::kToggleQueue)) {
    auto& queue_enabled = Config::Get().control.queue_enabled;
    queue_enabled       = !queue_enabled;
    ResetQueueRecoveryTracking();
    return true;
  }
  return false;
}

bool FleetActionShortcuts()
{
  // A replay lives for exactly the next frame, whatever that frame does.
  const auto replayed = std::exchange(replay, std::nullopt);
  if (unity::Key::IsInputFocused())
    return false;

  if (unity::Key::Pressed(unity::KeyCode::Escape) && TargetViewers::HideFirst())
    return true;

  // Escape or the primary action dismisses the golden rewards screen. Down, not Pressed: a held Escape would go
  // back a section every frame while the screen closes.
  if (Shortcut::IsDown(Action::kActionPrimary) || unity::Key::Down(unity::KeyCode::Escape)) {
    if (auto* rewards = unity::Newest<AnimatedRewardsScreenViewController>(); rewards && rewards->IsActive()) {
      rewards->GoBackToLastSection();
      return true;
    }
  }

  Pressed pressed{
      .primary     = Shortcut::IsDown(Action::kActionPrimary),
      .secondary   = Shortcut::IsDown(Action::kActionSecondary),
      .queue       = Shortcut::IsDown(Action::kActionQueue),
      .queue_clear = Shortcut::IsDown(Action::kActionQueueClear),
      // With disable_preview_recall, recall only works while no target card is open.
      .recall =
          Shortcut::IsDown(Action::kActionRecall) && (!Config::Get().ui.disable_preview_recall || !TargetViewerShown()),
      .recall_cancel = Shortcut::IsDown(Action::kActionRecallCancel),
      .repair        = Shortcut::IsDown(Action::kActionRepair),
  };
  if (replayed && !pressed.Any()) {
    pressed       = *replayed;
    pressed.retry = true;
  }
  if (pressed.Any() && Hub::IsInSystemOrGalaxyOrStarbase() && !Hub::IsInChat())
    ActOnSelectedFleet(unity::Newest<FleetBarViewController>(), pressed);

  if (Shortcut::IsDown(Action::kActionView))
    ToggleTargetCargo();
  if (show_cargo_frames > 0)
    ShowPendingTargetCargo();
  return false;
}

void InstallFleetActions()
{
  IL2CPP_HOOK(RewardsButtonWidget::Class::Method("OnDidBindContext"), RewardsButtonWidget_OnDidBindContext_Hook);
  IL2CPP_HOOK(PreScanTargetWidget::Class::Method("ShowWithFleet"), PreScanTargetWidget_ShowWithFleet_Hook);
}

const Feature kQueueSwitch{"QueueSwitch", &Config::Patches::testpatches, InstallQueueSwitch};

namespace
{
void UpdateExpiredAbilityButtons()
{
  static auto next_update = std::chrono::steady_clock::time_point{};
  const auto  now         = std::chrono::steady_clock::now();
  if (now < next_update)
    return;
  next_update      = now + std::chrono::milliseconds(250);
  auto* bar        = unity::Newest<FleetBarViewController>();
  auto* controller = bar ? bar->fleetPanelController() : nullptr;
  auto* fleet      = controller ? controller->fleet() : nullptr;
  if (!fleet || (static_cast<int>(fleet->CurrentState()) & static_cast<int>(FleetState::CanActivateAbility)) == 0)
    return;
  auto* manager = ActivatedAbilityManager::Instance();
  auto* panel   = controller->shipActionPanelLocalViewController();
  auto* canvas  = panel ? CanvasController::Of(panel) : nullptr;
  if (!manager || !canvas || !canvas->Visible() || panel->HasShipAnActiveAbilityWithActiveEffect())
    return;
  auto* ids     = panel->abilityIDs();
  auto* buttons = panel->buttons();
  if (!ids || !buttons)
    return;
  for (size_t index = 0; index < ids->size() && index < buttons->size(); ++index) {
    auto* button  = (*buttons)[index];
    auto* context = button ? button->Context() : nullptr;
    if (!button || !button->isActiveAndEnabled() || !context || context->Interactable())
      continue;
    const auto ability_id = (*ids)[index];
    auto*      timer      = panel->GetRemainingCooldown(ability_id);
    // TimeSpan is an eight-byte value in 100 ns ticks. Repair at -1 second or below.
    if (timer && timer->RemainingTime().ticks <= -10000000 && manager->CanUseAbility(fleet, ability_id))
      context->SetInteractable(true);
  }
}

} // namespace
const Feature kAbilityButtons{"AbilityButtons", &Config::Patches::hotkeyhooks, nullptr, UpdateExpiredAbilityButtons};

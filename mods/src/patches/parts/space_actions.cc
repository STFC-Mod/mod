// Acting on the selected fleet and the target viewers in space: engage, scan, mine, queue, warp, recall and repair
// from the keyboard, dismissing viewers with Escape, and opening a target's cargo when it is scanned.

#include "config.h"
#include "patches/input.h"
#include "patches/object_tracker.h"
#include "patches/patches.h"
#include "patches/shortcuts.h"

#include <prime/Hub.h>
#include <prime/fleet.h>
#include <prime/navigation.h>
#include <prime/ui.h>
#include <prime/viewers.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <span>

namespace
{
// Frames left in which to show the cargo of a pre-scanned target. The rewards widget can appear a few frames
// after the pre-scan itself, so a request keeps trying for a while.
int show_cargo_frames = 0;

// The target's hull type is only known once the target's data has arrived; until then it reads as Any. A press
// that lands in that window is replayed on the next frame instead of being dropped.
bool retry_next_frame = false;

struct Pressed {
  bool primary;
  bool secondary;
  bool queue;
  bool queue_clear;
  bool recall;
  bool recall_cancel;
  bool repair;
};

bool Shown(VisibilityController* controller)
{ return controller && controller->IsShown(); }

template <typename T> bool TargetShown(T* widget, const Pressed& pressed)
{
  if (!widget || !Shown(widget->visibilityController()))
    return false;
  if (!pressed.primary || !Config::Get().primary_requires_visible_target)
    return true;
  auto* parent = widget->Parent();
  return parent && parent->IsShowing();
}

template <typename T> bool AnyViewerShown()
{
  return std::ranges::any_of(ObjectFinder<T>::GetAll(),
                             [](T* widget) { return widget && Shown(widget->visibilityController()); });
}

template <typename T> bool HideViewers()
{
  bool hid = false;
  for (auto* widget : ObjectFinder<T>::GetAll()) {
    if (widget && Shown(widget->visibilityController())) {
      widget->HideAllViewers();
      hid = true;
    }
  }
  return hid;
}

// Short-circuits like the old chain: the first type with a shown viewer hides it and stops.
bool HideAnyViewer()
{
  return HideViewers<AllianceStarbaseObjectViewerWidget>() || HideViewers<ArmadaObjectViewerWidget>()
         || HideViewers<CelestialObjectViewerWidget>() || HideViewers<EmbassyObjectViewer>()
         || HideViewers<HousingObjectViewerWidget>() || HideViewers<MiningObjectViewerWidget>()
         || HideViewers<MissionsObjectViewerWidget>() || HideViewers<PreScanTargetWidget>();
}

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

void PressListenerButton(SemaphoreButtonListener* listener)
{
  if (auto* button = listener ? listener->TheButton() : nullptr)
    button->Press();
}

// The primary action joins a wave defense first: through the set-course widget's join button when it listens, else
// the primary button of a shown wave defense viewer. Both press the game's own button, so its checks still run.
bool JoinWaveDefense()
{
  if (auto* navigation = ObjectFinder<NavigationInteractionUIViewController>::Get();
      navigation && navigation->TryPressJoinWaveDefense())
    return true;
  // Query active viewers without adding constructor detours for this popup or its subclasses.
  if (auto* viewers = WaveDefenseObjectViewerWidget::FindActiveViewers()) {
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
  if (auto* mine = ObjectFinder<MiningObjectViewerWidget>::Get(); TargetShown(mine, pressed)) {
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
    if (type != HullType::ArmadaTarget && (type != HullType::Any || retry_next_frame)) {
      if (add_to_queue->isActiveAndEnabled()) {
        if (auto* listener = add_to_queue->SemaphoreListener(); listener && !queue->IsQueueFull(fleet)) {
          if (auto* button = listener->TheButton()) {
            button->Press();
            HideAnyViewer();
          }
        }
        return true;
      }
      if (type == HullType::Any) {
        retry_next_frame = true;
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
    if (type == HullType::ArmadaTarget && ArmadaViewerShown(ObjectFinder<ArmadaObjectViewerWidget>::Get()))
      can_engage = false;
    else if (retry_next_frame)
      can_engage = true;

    if (can_engage) {
      if (type != HullType::ArmadaTarget) {
        buttons->OnEngageButtonClicked();
      } else if (auto* attack = pre_scan->armadaAttackButton(); attack && attack->isActiveAndEnabled()) {
        PressListenerButton(attack->SemaphoreListener());
      } else {
        buttons->OnArmadaButtonClicked();
      }
      return true;
    }
    if (type == HullType::Any) {
      retry_next_frame = true;
      return true;
    }
  }
  return false;
}

// Acts on whatever other viewer is open: a mining node, a system node, or an empty spot in space. Returns true
// when the press was used.
bool ActOnViewer(const Pressed& pressed)
{
  if (auto* mine = ObjectFinder<MiningObjectViewerWidget>::Get(); TargetShown(mine, pressed)) {
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

  if (auto* node = ObjectFinder<StarNodeObjectViewerWidget>::Get(); node && node->Context()
      && (!pressed.primary || !Config::Get().primary_requires_visible_target || TargetShown(node, pressed))) {
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

  auto* navigation = ObjectFinder<NavigationInteractionUIViewController>::Get();
  if (!navigation || !pressed.primary)
    return false;

  auto* armada = ObjectFinder<ArmadaObjectViewerWidget>::Get();
  if (!ArmadaViewerShown(armada)) {
    if (Config::Get().primary_requires_visible_target && !navigation->IsSetCourseVisible())
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

void ActOnSelectedFleet(FleetBarViewController* fleet_bar)
{
  if (Shortcut::IsDown(GameFunction::ActionPrimary) && JoinWaveDefense())
    return;
  if (!fleet_bar)
    return;
  auto* panel = fleet_bar->fleetPanelController();
  auto* fleet = panel ? panel->fleet() : nullptr;
  auto* queue = ActionQueueManager::Instance();
  if (!fleet || !queue)
    return;

  const Pressed pressed{
      .primary     = Shortcut::IsDown(GameFunction::ActionPrimary) || retry_next_frame,
      .secondary   = Shortcut::IsDown(GameFunction::ActionSecondary),
      .queue       = Shortcut::IsDown(GameFunction::ActionQueue),
      .queue_clear = Shortcut::IsDown(GameFunction::ActionQueueClear),
      // With disable_preview_recall, recall only works while no target card is open.
      .recall        = Shortcut::IsDown(GameFunction::ActionRecall)
                       && (!Config::Get().disable_preview_recall || !TargetViewerShown()),
      .recall_cancel = Shortcut::IsDown(GameFunction::ActionRecallCancel),
      .repair        = Shortcut::IsDown(GameFunction::ActionRepair),
  };

  if (pressed.queue_clear) {
    queue->ClearQueue(fleet);
    return;
  }
  const auto state = fleet->CurrentState();
  if (pressed.recall_cancel && (state == FleetState::WarpCharging || state == FleetState::Warping)) {
    panel->CancelButtonClicked();
    return;
  }

  for (auto* pre_scan : ObjectFinder<PreScanTargetWidget>::GetAll())
    if (TargetShown(pre_scan, pressed) && ActOnPreScan(pre_scan, pressed, fleet, queue))
      return;

  if (ActOnViewer(pressed))
    return;

  static constexpr FleetState kRecallable[] = {FleetState::IdleInSpace, FleetState::Impulsing, FleetState::Mining,
                                               FleetState::Capturing};
  static constexpr FleetState kRepairable[] = {FleetState::Docked, FleetState::Destroyed};
  if ((pressed.recall && RequestFleetAction(fleet_bar, ActionType::Recall, kRecallable))
      || (pressed.repair && RequestFleetAction(fleet_bar, ActionType::Repair, kRepairable, FleetState::Repairing)))
    retry_next_frame = false;
}

// Toggles a shown pre-scan card's cargo panel. Opening is retried for a few frames, see show_cargo_frames.
void ToggleTargetCargo()
{
  for (auto* pre_scan : ObjectFinder<PreScanTargetWidget>::GetAll()) {
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
  for (auto* pre_scan : ObjectFinder<PreScanTargetWidget>::GetAll()) {
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
  auto*       target = widget && config.show_cargo_default ? widget->Context() : nullptr;
  if (!target)
    return false;

  auto* deployed = target->TargetFleetDeployedData();
  if (!deployed)
    return config.show_station_cargo;
  switch (deployed->FleetType()) {
    case DeployedFleetType::Player:
      return config.show_player_cargo;
    case DeployedFleetType::Marauder: {
      auto* hull = deployed->Hull();
      return hull && hull->Type() == HullType::ArmadaTarget ? config.show_armada_cargo : config.show_hostile_cargo;
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
} // namespace

bool TargetViewerShown()
{
  return AnyViewerShown<AllianceStarbaseObjectViewerWidget>() || AnyViewerShown<ArmadaObjectViewerWidget>()
         || AnyViewerShown<CelestialObjectViewerWidget>() || AnyViewerShown<EmbassyObjectViewer>()
         || AnyViewerShown<HousingObjectViewerWidget>() || AnyViewerShown<MiningObjectViewerWidget>()
         || AnyViewerShown<MissionsObjectViewerWidget>() || AnyViewerShown<PreScanTargetWidget>();
}

bool SpaceActionsUpdate()
{
  if (Key::IsInputFocused())
    return false;

  if (Key::Pressed(KeyCode::Escape) && HideAnyViewer())
    return true;

  // Escape or the primary action dismisses the golden rewards screen.
  if (Shortcut::IsDown(GameFunction::ActionPrimary) || Key::Pressed(KeyCode::Escape)) {
    if (auto* rewards = ObjectFinder<AnimatedRewardsScreenViewController>::Get(); rewards && rewards->IsActive()) {
      rewards->GoBackToLastSection();
      return true;
    }
  }

  const bool acting = Shortcut::IsDown(GameFunction::ActionPrimary) || Shortcut::IsDown(GameFunction::ActionSecondary)
                      || Shortcut::IsDown(GameFunction::ActionRecall) || Shortcut::IsDown(GameFunction::ActionRepair)
                      || Shortcut::IsDown(GameFunction::ActionQueue) || Shortcut::IsDown(GameFunction::ActionQueueClear)
                      || retry_next_frame;
  if (acting && Hub::IsInSystemOrGalaxyOrStarbase() && !Hub::IsInChat()) {
    const bool was_retry = retry_next_frame;
    ActOnSelectedFleet(ObjectFinder<FleetBarViewController>::Get());
    // A replayed press gets one more frame, not more.
    if (was_retry)
      retry_next_frame = false;
  }

  if (Shortcut::IsDown(GameFunction::ActionView))
    ToggleTargetCargo();
  if (show_cargo_frames > 0)
    ShowPendingTargetCargo();
  return false;
}

void InstallSpaceActionHooks()
{
  if (auto m = RewardsButtonWidget::Class::Method("OnDidBindContext"))
    SPUD_STATIC_DETOUR(m.Pointer(), RewardsButtonWidget_OnDidBindContext_Hook);
  if (auto m = PreScanTargetWidget::Class::Method("ShowWithFleet"))
    SPUD_STATIC_DETOUR(m.Pointer(), PreScanTargetWidget_ShowWithFleet_Hook);
}

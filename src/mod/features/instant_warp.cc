// auto_confirm_instant_warp and the instant_warp_* ship filters: answers the warp-or-jump prompt for the ship,
// previews that answer on the system card's visit button, and the shortcut that cycles the default.

#include "config.h"
#include "features/features.h"
#include "text.h"

#include "game/fleet.h"
#include "game/navigation.h"
#include "game/ui.h"
#include "game/viewers.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/tracker.h>

#include <algorithm>
#include <chrono>

namespace
{
// The action auto-confirm takes for `fleet`: always-ask wins, then auto-jump, then auto-warp, then the default.
InstantWarpConfirmation ResolveConfirmation(FleetPlayerData* fleet)
{
  const auto& ui      = Config::Get().ui;
  const auto  words   = fleet ? ShipNameWords(fleet->DisplayName()) : std::vector<std::string>();
  const auto  matches = [&words](const ShipFilter& filter) {
    return filter.all
           || std::ranges::any_of(filter.names, [&](const auto& name) { return ShipNameMatches(words, name); });
  };
  if (matches(ui.instant_warp_always_ask))
    return InstantWarpConfirmation::kNone;
  if (matches(ui.instant_warp_auto_jump))
    return InstantWarpConfirmation::kJump;
  if (matches(ui.instant_warp_auto_warp))
    return InstantWarpConfirmation::kWarp;
  return ui.auto_confirm_instant_warp;
}

void CoursePromptPopupViewController_AboutToShow_Hook(auto original, CoursePromptPopupViewController* _this)
{
  original(_this);
  auto* widget  = _this ? _this->PopupWidget() : nullptr;
  auto* context = widget ? widget->Context() : nullptr;
  if (!context || !context->HasInstantWarp())
    return;

  auto*      course = context->GetCourseData();
  const auto action = ResolveConfirmation(course ? course->PlayerFleet() : nullptr);
  spdlog::debug("InstantWarpConfirmation: resolved action {}", static_cast<int>(action));
  switch (action) {
    case InstantWarpConfirmation::kWarp:
      widget->InitiateRegularWarp();
      break;
    case InstantWarpConfirmation::kJump:
      widget->OnInstantWarpButtonClick();
      break;
    case InstantWarpConfirmation::kNone:
      break;
  }
}

// The system card's visit button reads WARP or JUMP when auto-confirm would pick one. This runs on the frame
// tick rather than another UI detour, and only overrides the label text: never the button's context, timer,
// resource cost, or click handler.
bool label_installed = false;

std::string CurrentText(TextLocalizer* label)
{ return il2cpp::ToUtf8(label->CurrentText()); }

// The label text this file set, so it is only cleared while it still reads that.
struct OwnedLabel {
  il2cpp::GcHandle handle;
  std::string      text;

  TextLocalizer* Get() const
  { return handle.Get<TextLocalizer>(); }

  void Clear()
  {
    auto* label    = Get();
    auto  previous = std::move(text);
    handle.Reset();
    text.clear();
    if (unity::Alive(label) && label->textOverride() && CurrentText(label) == previous)
      label->ClearTextOverride();
  }

  void Apply(TextLocalizer* label, const char* desired)
  {
    if (!label || !desired) {
      Clear();
      return;
    }
    if (Get() != label) {
      Clear();
      // Don't take ownership from an existing text override.
      if (label->textOverride())
        return;
      handle = il2cpp::GcHandle::Weak(label);
      if (!handle)
        return;
    }
    const auto current = CurrentText(label);
    if (!text.empty() && label->textOverride() && current != text) {
      Clear();
      return;
    }
    if (text == desired && label->textOverride() && current == desired)
      return;
    label->OverrideLocalizedText(il2cpp::NewString(desired));
    text = desired;
  }
};
OwnedLabel action_label;

void UpdateLabel()
{
  static auto next = std::chrono::steady_clock::time_point{};
  const auto  now  = std::chrono::steady_clock::now();
  if (now < next)
    return;
  next = now + std::chrono::milliseconds(100);

  // Without a default or a jump/warp filter every prompt resolves to "ask"; skip the fleet lookups.
  const auto& ui = Config::Get().ui;
  if (ui.auto_confirm_instant_warp == InstantWarpConfirmation::kNone && !ui.instant_warp_auto_jump.all
      && ui.instant_warp_auto_jump.names.empty() && !ui.instant_warp_auto_warp.all
      && ui.instant_warp_auto_warp.names.empty()) {
    action_label.Clear();
    return;
  }

  auto*       widget  = unity::Newest<StarNodeObjectViewerWidget>();
  const bool  active  = unity::Alive(widget) && widget->isActiveAndEnabled();
  auto*       button  = active ? widget->visitButton() : nullptr;
  const bool  enabled = unity::Alive(button) && button->isActiveAndEnabled() && button->Interactable();
  const char* desired = nullptr;
  // The ordinary visit path uses the currently selected fleet. The card's cached popup CourseData is only filled on
  // click, so it cannot preview. Toll and wormhole buttons confirm their own way and keep the game's text.
  if (enabled) {
    auto* fleets     = FleetsManager::Instance();
    auto* deployment = DeploymentManager::Instance();
    auto* fleet      = unity::Alive(fleets) ? fleets->GetSelectedFleetData() : nullptr;
    if (fleet && unity::Alive(deployment) && deployment->HasInstantWarpAbility(fleet)) {
      switch (ResolveConfirmation(fleet)) {
        case InstantWarpConfirmation::kWarp:
          desired = "WARP";
          break;
        case InstantWarpConfirmation::kJump:
          desired = "JUMP";
          break;
        case InstantWarpConfirmation::kNone:
          break;
      }
    }
  }
  auto* label = desired ? button->mainText() : nullptr;
  action_label.Apply(unity::Alive(label) ? label : nullptr, desired);
}

void InstantWarpUpdate()
{
  if (label_installed)
    UpdateLabel();
}

void InstallInstantWarp()
{
  // The hook is useless without both popup actions, so skip it (and the label) when either has drifted.
  const auto about_to_show = CoursePromptPopupViewController::Class::Method("AboutToShow", 0);
  if (!about_to_show || !CoursePromptPopupWidget::Class::Method("InitiateRegularWarp", 0)
      || !CoursePromptPopupWidget::Class::Method("OnInstantWarpButtonClick", 0)) {
    return;
  }

  if (!IL2CPP_HOOK(about_to_show, CoursePromptPopupViewController_AboutToShow_Hook))
    return;
  label_installed = true;
  spdlog::info("[WarpActionLabel] system-card action labels enabled");
}
} // namespace

void CycleInstantWarpConfirmation()
{
  auto& setting = Config::Get().ui.auto_confirm_instant_warp;
  switch (setting) {
    case InstantWarpConfirmation::kNone:
      setting = InstantWarpConfirmation::kWarp;
      break;
    case InstantWarpConfirmation::kWarp:
      setting = InstantWarpConfirmation::kJump;
      break;
    case InstantWarpConfirmation::kJump:
      setting = InstantWarpConfirmation::kNone;
      break;
  }
  constexpr const char* kNames[] = {"none", "warp", "jump"};
  spdlog::info("Auto-confirm instant warp set to {}", kNames[static_cast<int>(setting)]);
}

const Feature kInstantWarp{"InstantWarp", nullptr, InstallInstantWarp, InstantWarpUpdate};

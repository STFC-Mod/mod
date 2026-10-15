// Instant warp: confirms the warp/jump prompt per the configured ship filters, and previews the choice on the
// system card's visit button.

#include "config.h"
#include "patches/object_tracker.h"
#include "patches/patches.h"
#include "ship_name_match.h"
#include "str_utils.h"

#include <prime/fleet.h>
#include <prime/navigation.h>
#include <prime/ui.h>
#include <prime/viewers.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace
{
// The action auto-confirm takes for `fleet`: always-ask wins, then auto-jump, then auto-warp, then the default.
InstantWarpConfirmation ResolveConfirmation(FleetPlayerData* fleet)
{
  const auto& ui      = Config::Get().ui;
  const auto  words   = ShipNameMatch::DisplayWords(fleet);
  const auto  matches = [&words](const ShipFilter& filter) {
    return filter.all || (!words.empty() && std::ranges::any_of(filter.names, [&](const auto& name) {
             return ShipNameMatch::MatchesDisplay(words, ShipNameMatch::SplitWords(name));
           }));
  };
  if (matches(ui.instant_warp_always_ask))
    return InstantWarpConfirmation::None;
  if (matches(ui.instant_warp_auto_jump))
    return InstantWarpConfirmation::Jump;
  if (matches(ui.instant_warp_auto_warp))
    return InstantWarpConfirmation::Warp;
  return ui.auto_confirm_instant_warp;
}

void CoursePromptPopupViewController_AboutToShow_Hook(auto original, CoursePromptPopupViewController* _this)
{
  original(_this);

  const auto widget  = _this == nullptr ? nullptr : _this->PopupWidget();
  const auto context = widget == nullptr ? nullptr : widget->Context();
  if (context == nullptr || !context->HasInstantWarp()) {
    return;
  }

  FleetPlayerData* fleet = nullptr;
  if (const auto course = context->GetCourseData(); course != nullptr)
    fleet = course->PlayerFleet();
  const auto action = ResolveConfirmation(fleet);
  spdlog::debug("InstantWarpConfirmation: resolved action {}", static_cast<int>(action));
  switch (action) {
    case InstantWarpConfirmation::Warp:
      widget->InitiateRegularWarp();
      break;
    case InstantWarpConfirmation::Jump:
      widget->OnInstantWarpButtonClick();
      break;
    case InstantWarpConfirmation::None:
      break;
  }
}
} // namespace

namespace
{
// The system card's visit button reads WARP or JUMP when auto-confirm would pick one. This runs on the frame
// tick rather than another UI detour, and only overrides the label text: never the button's context, timer,
// resource cost, or click handler.
bool label_installed = false;

// Unity's `bool(UnityEngine.Object)`: false for null and for destroyed native objects.
bool Alive(il2cpp::Object* object)
{
  using UnityObject = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;
  return object && UnityObject::Call<"op_Implicit", bool>(object);
}

std::string CurrentText(TextLocalizer* label)
{
  auto* text = label->CurrentText();
  if (!text)
    throw std::runtime_error("expected label text");
  return to_string(text);
}

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
    // Forget ownership even if native cleanup fails.
    if (Alive(label) && label->textOverride() && CurrentText(label) == previous)
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
    label->OverrideLocalizedText(il2cpp_string_new(desired));
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
  try {
    auto*       widget  = ObjectFinder<StarNodeObjectViewerWidget>::Get();
    const bool  active  = Alive(widget) && widget->isActiveAndEnabled();
    auto*       button  = active ? widget->visitButton() : nullptr;
    const bool  enabled = Alive(button) && button->isActiveAndEnabled() && button->Interactable();
    const char* desired = nullptr;
    // The ordinary visit path uses the currently selected fleet. The card's
    // cached popup CourseData is populated on click, so it is not a preview source.
    // Toll/wormhole buttons have separate confirmation paths and retain native text.
    if (enabled) {
      auto* fleets     = FleetsManager::Instance();
      auto* deployment = DeploymentManger::Instance();
      auto* fleet      = Alive(fleets) ? fleets->GetSelectedFleetData() : nullptr;
      if (fleet && Alive(deployment) && deployment->HasInstantWarpAbility(fleet)) {
        switch (ResolveConfirmation(fleet)) {
          case InstantWarpConfirmation::Warp:
            desired = "WARP";
            break;
          case InstantWarpConfirmation::Jump:
            desired = "JUMP";
            break;
          case InstantWarpConfirmation::None:
            break;
        }
      }
    }
    auto* label = desired ? button->mainText() : nullptr;
    action_label.Apply(Alive(label) ? label : nullptr, desired);
  } catch (const std::exception& error) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      spdlog::warn("[WarpActionLabel] native label unavailable: {}", error.what());
    }
    try {
      action_label.Clear();
    } catch (...) {
    }
  }
}
} // namespace

void WarpActionLabelUpdate()
{
  if (label_installed)
    UpdateLabel();
}

void InstallInstantWarpConfirmationHooks()
{
  // The hook is useless without both popup actions, so skip it (and the label) when either has drifted.
  const auto about_to_show = CoursePromptPopupViewController::Class::Method("AboutToShow", 0);
  if (!about_to_show || !CoursePromptPopupWidget::Class::Method("InitiateRegularWarp", 0)
      || !CoursePromptPopupWidget::Class::Method("OnInstantWarpButtonClick", 0)) {
    return;
  }

  if (!SPUD_STATIC_DETOUR(about_to_show.Pointer(), CoursePromptPopupViewController_AboutToShow_Hook))
    return;
  label_installed = InstallScreenManagerUpdateHook();
  if (label_installed)
    spdlog::info("[WarpActionLabel] system-card action labels enabled");
}

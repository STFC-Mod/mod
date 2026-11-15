#include "config.h"
#include "errormsg.h"
#include "patches/instant_warp_policy.h"
#include "ship_name_match.h"

#include <prime/CourseData.h>
#include <prime/CoursePromptPopupWidget.h>

#include <spud/detour.h>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace
{
using PopupAction                         = void(CoursePromptPopupWidget*);
PopupAction* initiate_regular_warp        = nullptr;
PopupAction* on_instant_warp_button_click = nullptr;

void CoursePromptPopupViewController_AboutToShow_Hook(auto original, CoursePromptPopupViewController* _this)
{
  original(_this);

  const auto widget  = _this == nullptr ? nullptr : _this->PopupWidget;
  const auto context = widget == nullptr ? nullptr : widget->Context;
  if (context == nullptr || !context->HasInstantWarp) {
    return;
  }

  FleetPlayerData* fleet = nullptr;
  if (const auto course = context->GetCourseData(); course != nullptr)
    fleet = course->PlayerFleet;
  const auto action = ResolveInstantWarpConfirmation(fleet);
  spdlog::debug("InstantWarpConfirmation: resolved action {}", static_cast<int>(action));
  switch (action) {
    case InstantWarpConfirmation::Warp:
      initiate_regular_warp(widget);
      break;
    case InstantWarpConfirmation::Jump:
      on_instant_warp_button_click(widget);
      break;
    case InstantWarpConfirmation::None:
      break;
  }
}
} // namespace

InstantWarpConfirmation ResolveInstantWarpConfirmation(FleetPlayerData* fleet)
{
  const auto& cfg           = Config::Get();
  const auto  display_words = ShipNameMatch::DisplayWords(fleet);
  const auto  matches       = [&display_words](const std::vector<std::string>& names, bool all) {
    return all || (!display_words.empty() && std::ranges::any_of(names, [&](const auto& configured) {
             return ShipNameMatch::MatchesDisplay(display_words, ShipNameMatch::SplitWords(configured));
           }));
  };
  if (matches(cfg.instant_warp_always_ask, cfg.instant_warp_always_ask_all))
    return InstantWarpConfirmation::None;
  if (matches(cfg.instant_warp_auto_jump, cfg.instant_warp_auto_jump_all))
    return InstantWarpConfirmation::Jump;
  if (matches(cfg.instant_warp_auto_warp, cfg.instant_warp_auto_warp_all))
    return InstantWarpConfirmation::Warp;
  return cfg.auto_confirm_instant_warp;
}

void InstallInstantWarpConfirmationHooks()
{
  auto helper = CoursePromptPopupViewController::get_class_helper();
  if (!helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.ObjectViewer", "CoursePromptPopupViewController");
    return;
  }

  const auto about_to_show = helper.GetMethod("AboutToShow", 0);
  if (about_to_show == nullptr) {
    ErrorMsg::MissingMethod("CoursePromptPopupViewController", "AboutToShow");
    return;
  }

  auto widget_helper = CoursePromptPopupWidget::get_class_helper();
  if (!widget_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.Navigation", "CoursePromptPopupWidget");
    return;
  }

  initiate_regular_warp = widget_helper.GetMethod<PopupAction>("InitiateRegularWarp", 0);
  if (initiate_regular_warp == nullptr) {
    ErrorMsg::MissingMethod("CoursePromptPopupWidget", "InitiateRegularWarp");
    return;
  }

  on_instant_warp_button_click = widget_helper.GetMethod<PopupAction>("OnInstantWarpButtonClick", 0);
  if (on_instant_warp_button_click == nullptr) {
    ErrorMsg::MissingMethod("CoursePromptPopupWidget", "OnInstantWarpButtonClick");
    return;
  }

  if (SPUD_STATIC_DETOUR(about_to_show, CoursePromptPopupViewController_AboutToShow_Hook))
    InstallWarpActionLabel();
}

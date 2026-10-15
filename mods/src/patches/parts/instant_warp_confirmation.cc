#include "config.h"
#include "patches/instant_warp_policy.h"
#include "ship_name_match.h"

#include <prime/fleet.h>
#include <prime/navigation.h>

#include <spud/detour.h>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace
{
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
  const auto action = ResolveInstantWarpConfirmation(fleet);
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
  // The hook is useless without both popup actions, so skip it (and the label) when either has drifted.
  const auto about_to_show = CoursePromptPopupViewController::Class::Method("AboutToShow", 0);
  if (!about_to_show || !CoursePromptPopupWidget::Class::Method("InitiateRegularWarp", 0)
      || !CoursePromptPopupWidget::Class::Method("OnInstantWarpButtonClick", 0)) {
    return;
  }

  if (SPUD_STATIC_DETOUR(about_to_show.Pointer(), CoursePromptPopupViewController_AboutToShow_Hook))
    InstallWarpActionLabel();
}

// Double-click a ship tile, or press Enter, to press the dock's Assign button.

#include "config.h"
#include "patches/patches.h"
#include "patches/input.h"
#include "patches/object_tracker.h"

#include "double_click_assign_ship.h"
#include "prime/fleet.h"
#include "prime/ui.h"
#include "prime/KeyCode.h"
#include "ship_tile_click.h"

#include <il2cpp/il2cpp.h>
#include "patches/object_tracker.h"

#include <chrono>

namespace {

constexpr auto kDoubleClickWindow = std::chrono::milliseconds(400);

FleetPlayerData*                      g_last_clicked_ship = nullptr;
std::chrono::steady_clock::time_point g_last_click_time{};

void PressAssignButton()
{
  for (auto widget : ObjectFinder<AssignShipsWidget>::GetAll()) {
    if (!widget) continue;

    auto canvas = GetCanvasControllerFromComponent(widget);
    if (!canvas || !canvas->Visible() || !widget->isActiveAndEnabled()) continue;

    auto* buttonWrapper = widget->assignButton();
    auto* buttonWidget  = buttonWrapper ? buttonWrapper->Widget() : nullptr;
    auto* listener      = buttonWidget ? buttonWidget->SemaphoreListener() : nullptr;
    auto* button        = listener ? listener->TheButton() : nullptr;
    if (button) {
      button->Press();
    }
    return;
  }
}

} // namespace

void DoubleClickOnShipTileClicked(FleetPlayerData* ship)
{
  if (!Config::Get().double_click_to_assign_ship) return;
  if (!ship) return;

  const auto now             = std::chrono::steady_clock::now();
  const bool is_double_click = ship == g_last_clicked_ship && (now - g_last_click_time) <= kDoubleClickWindow;

  g_last_clicked_ship = ship;
  g_last_click_time   = now;

  if (!is_double_click) return;

  g_last_clicked_ship = nullptr; // consume, so a triple/quadruple click doesn't re-trigger immediately
  PressAssignButton();
}

void AssignShipEnterKeyUpdate()
{
  if (!Config::Get().double_click_to_assign_ship) return;
  if (!Key::Pressed(KeyCode::Return) && !Key::Pressed(KeyCode::KeypadEnter)) return;
  if (Key::IsInputFocused()) return;

  PressAssignButton();
}

void InstallDoubleClickAssignShipHooks()
{
  // The HandleOnClick detour is owned by the shared ship-tile click dispatcher
  // (macOS only permits one hook per function).
  ship_tile_click::TryInstallHook();
}

// [control] macos_navigation_mouse: app-local Back/Forward swipe events feed the existing MOUSE3/MOUSE4 bindings.

#include "config.h"
#include "features/features.h"
#include "features/mouse_navigation.h"
#include "mod.h"

#include <spdlog/spdlog.h>
#include <unity/input.h>

#import <AppKit/AppKit.h>

namespace
{
id navigation_mouse_monitor = nil;
MouseNavigation::Swipe navigation_mouse_swipe;

void ResetMacNavigationMouse()
{
  navigation_mouse_swipe = {};
  unity::Key::ClearNavigationMouse();
}

bool NavigationMouseEnabled()
{
  const auto& config = Config::Get();
  return config.control.macos_navigation_mouse && config.control.hotkeys_enabled
         && !config.control.use_scopely_hotkeys && !ModReloading() && NSApp.active;
}

NSEvent* NavigationMouseEvent(NSEvent* event)
{
  if (!NavigationMouseEnabled()) {
    ResetMacNavigationMouse();
    return event;
  }
  using MouseNavigation::Phase;
  const auto native_phase = event.phase;
  const auto phase = (native_phase & NSEventPhaseCancelled) ? Phase::kCancelled
                   : (native_phase & NSEventPhaseEnded) ? Phase::kEnded
                   : (native_phase & NSEventPhaseBegan) ? Phase::kBegan
                   : (native_phase & NSEventPhaseChanged) ? Phase::kChanged : Phase::kDiscrete;
  const auto direction = MouseNavigation::Button(event.deltaX, event.deltaY);
  const auto button = navigation_mouse_swipe.Observe(direction, phase);
  if (button >= 0) {
    unity::Key::QueueNavigationMouse(button);
    static bool navigation_mouse_reported[2] = {};
    if (!navigation_mouse_reported[button - 3]) {
      navigation_mouse_reported[button - 3] = true;
      spdlog::info("[MacNavigationMouse] {} gesture received; mapped to MOUSE{}", button == 3 ? "Back" : "Forward", button);
    }
  }
  return direction >= 0 || button >= 0 ? nil : event;
}

bool InstallMacNavigationMouse()
{
  // A local monitor only sees events delivered to this application and needs no global input-monitoring grant.
  navigation_mouse_monitor = [[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskSwipe
      handler:^NSEvent*(NSEvent* event) { return NavigationMouseEvent(event); }] retain];
  if (!navigation_mouse_monitor) {
    spdlog::warn("[MacNavigationMouse] could not install the app-local event monitor");
    return false;
  }
  // Kept for the process lifetime: no callbacks or AppKit messages from native static destructors at exit.
  return true;
}

void MacNavigationMouseFrame()
{
  if (!NavigationMouseEnabled())
    ResetMacNavigationMouse();
}
} // namespace

const Feature kMacNavigationMouse{"MacNavigationMouse", &Config::Patches::hotkeyhooks, InstallMacNavigationMouse,
                                  MacNavigationMouseFrame, ResetMacNavigationMouse};

// [graphics] ui_scale, ui_scale_viewer and ui_scale_ship: the interface, object viewer and system ship scales, and the
// shortcuts that adjust them during play. A ui_scale or ui_scale_viewer of 0 leaves the game's own scale alone, and
// the shortcuts do not switch it on.

#include "config.h"
#include "features/features.h"

#include "game/fleet.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/tracker.h>
#include <unity/ui.h>

#include <algorithm>
#include <cmath>

#if _WIN32
#include <Windows.h>
#endif

namespace
{
// Ships already in the system keep the scale they were loaded with, so rescale them by the change.
void RescaleLoadedShips(float old_multiplier, float new_multiplier)
{
  if (old_multiplier <= 0.0f || new_multiplier <= 0.0f)
    return;

  const auto ratio = new_multiplier / old_multiplier;
  for (auto* selector : unity::Instances<FleetMeshSelector>()) {
    auto* object    = selector->LoadedObject();
    auto* transform = object ? object->transform() : nullptr;
    if (!transform)
      continue;
    transform->SetLocalScale(transform->localScale() * ratio);
  }
}

// The scale a ship model is loaded with; ships loaded earlier are handled by RescaleLoadedShips.
bool SystemViewShip_TryGetShipAssetBundleResource_Hook(auto original, il2cpp::Object* _this, float* scale,
                                                       void** asset_bundle_resource)
{
  const auto result     = original(_this, scale, asset_bundle_resource);
  const auto multiplier = Config::Get().graphics.ui_scale_ship;
  if (result && scale && multiplier > 0.0f)
    *scale *= multiplier;
  return result;
}

void ScreenManager_UpdateCanvasRootScaleFactor_Hook(auto original, ScreenManager* _this)
{
  original(_this);

  // allow_cursor = false, as in window.cc: rescaling canvases makes the game set its own cursor again.
#if _WIN32
  static auto cursor = LoadCursor(nullptr, IDC_ARROW);
  if (!Config::Get().graphics.allow_cursor)
    SetCursor(cursor);
#endif

  const auto ui_scale = Config::Get().graphics.ui_scale;
  if (ui_scale == 0.0f)
    return;

  // The canvas scale factor counts physical pixels per UI unit. Multiplying by the monitor's display scale keeps
  // ui_scale meaning the same size on a 100% and a 150% monitor.
  const auto factor = ui_scale * WindowDpiScale();
  if (auto* scaler = _this->canvasRootScaler())
    scaler->SetScaleFactor(std::isnan(factor) ? 1.0f : std::clamp(factor, 0.1f, 5.0f));
}

void CanvasController_Show_Hook(auto original, CanvasController* _this, int32_t entry_point, bool instant)
{
  const auto viewer_scale = Config::Get().graphics.ui_scale_viewer;
  if (viewer_scale != 0.0f && il2cpp::Equals(_this->name(), "ObjectViewerTemplate_Canvas"))
    if (auto* transform = _this->transform())
      transform->SetLocalScale({viewer_scale, viewer_scale, viewer_scale});
  original(_this, entry_point, instant);
}

bool InstallUiScale()
{
  il2cpp::HookSet hooks("UiScale");
  IL2CPP_STAGE(hooks, SystemViewShip::Method("TryGetShipAssetBundleResource", 2),
               SystemViewShip_TryGetShipAssetBundleResource_Hook);
  IL2CPP_STAGE(hooks, ScreenManager::Class::Method("UpdateCanvasRootScaleFactor"),
               ScreenManager_UpdateCanvasRootScaleFactor_Hook);
  IL2CPP_STAGE(hooks, CanvasController::Class::Method("Show", {"System.Int32", "System.Boolean"}),
               CanvasController_Show_Hook);
  return hooks.Commit();
}
} // namespace

void AdjustUiScale(bool up)
{
  auto& graphics = Config::Get().graphics;
  if (graphics.ui_scale == 0.0f)
    return;
  const auto old_scale = graphics.ui_scale;
  const auto new_scale = old_scale + (up ? 1.0f : -1.0f) * graphics.ui_scale_step;
  graphics.ui_scale    = std::clamp(new_scale, 0.1f, 2.0f);
  spdlog::info("[UiScale] interface scale {} -> {} (asked {}), DPI scale {}", old_scale, graphics.ui_scale, new_scale,
               WindowDpiScale(true));
}

void AdjustUiViewerScale(bool up)
{
  auto& graphics = Config::Get().graphics;
  if (graphics.ui_scale_viewer == 0.0f)
    return;
  // Viewers change in quarter steps; a full step is too coarse at their size.
  const auto old_scale     = graphics.ui_scale_viewer;
  const auto new_scale     = old_scale + (up ? 1.0f : -1.0f) * graphics.ui_scale_step * 0.25f;
  graphics.ui_scale_viewer = std::clamp(new_scale, 0.1f, 2.0f);
  spdlog::info("[UiScale] viewer scale {} -> {} (asked {})", old_scale, graphics.ui_scale_viewer, new_scale);
}

void AdjustUiShipScale(bool up)
{
  auto&      graphics    = Config::Get().graphics;
  const auto old_scale   = graphics.ui_scale_ship;
  const auto new_scale   = old_scale + (up ? 1.0f : -1.0f) * graphics.ui_scale_step;
  graphics.ui_scale_ship = std::clamp(new_scale, 0.1f, 20.0f);
  spdlog::info("[UiScale] ship scale {} -> {} (asked {})", old_scale, graphics.ui_scale_ship, new_scale);
  RescaleLoadedShips(old_scale, graphics.ui_scale_ship);
}

const Feature kUiScale{"UiScale", &Config::Patches::uiscalehooks, InstallUiScale};

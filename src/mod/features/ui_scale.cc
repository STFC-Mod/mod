// UI, object viewer, and system ship scaling, and the hotkeys that adjust them at runtime.

#include "config.h"
#include "features/features.h"
#include "text.h"

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
float ShipScaleMultiplier()
{
  const auto multiplier = Config::Get().graphics.ui_scale_ship;
  return multiplier > 0.0f ? multiplier : 1.0f;
}

// Ships already in the system keep the scale they were loaded with, so rescale them by the change.
void RescaleLoadedShips(float old_multiplier, float new_multiplier)
{
  if (old_multiplier <= 0.0f || new_multiplier <= 0.0f)
    return;

  const auto ratio = new_multiplier / old_multiplier;
  for (auto* selector : unity::Instances<FleetMeshSelector>()) {
    auto* object    = selector ? selector->LoadedObject() : nullptr;
    auto* transform = object ? object->transform() : nullptr;
    if (!transform)
      continue;
    const auto scale = transform->localScale();
    transform->SetLocalScale({scale.x * ratio, scale.y * ratio, scale.z * ratio});
  }
}

bool SystemViewShip_TryGetShipAssetBundleResource_Hook(auto original, void* _this, float* scale,
                                                       void** asset_bundle_resource)
{
  const auto result = original(_this, scale, asset_bundle_resource);
  if (result && scale)
    *scale *= ShipScaleMultiplier();
  return result;
}

void ScreenManager_UpdateCanvasRootScaleFactor_Hook(auto original, ScreenManager* _this)
{
  original(_this);

#if _WIN32
  static auto cursor = LoadCursor(nullptr, IDC_ARROW);
  if (!Config::Get().graphics.allow_cursor)
    SetCursor(cursor);
#endif

  const auto ui_scale = Config::Get().graphics.ui_scale;
  if (ui_scale == 0.0f)
    return;

  auto factor = ui_scale * WindowDpiScale();
  factor      = std::isnan(factor) ? 1.0f : std::clamp(factor, 0.1f, 5.0f);
  if (auto* scaler = _this->canvasRootScaler())
    scaler->SetScaleFactor(factor);
}

void CanvasController_Show_Hook(auto original, CanvasController* _this, int desiredEntryPoint, bool instant)
{
  const auto viewer_scale = Config::Get().graphics.ui_scale_viewer;
  if (_this && viewer_scale != 0.0f && il2cpp::Equals(_this->name(), "ObjectViewerTemplate_Canvas")) {
    if (auto* transform = _this->transform())
      transform->SetLocalScale({viewer_scale, viewer_scale, viewer_scale});
  }
  return original(_this, desiredEntryPoint, instant);
}

void InstallUiScale()
{
  IL2CPP_HOOK(SystemViewShip::Method("TryGetShipAssetBundleResource", 2),
              SystemViewShip_TryGetShipAssetBundleResource_Hook);

  IL2CPP_HOOK(ScreenManager::Class::Method("UpdateCanvasRootScaleFactor"),
              ScreenManager_UpdateCanvasRootScaleFactor_Hook);

  IL2CPP_HOOK(CanvasController::Class::Method("Show", {"System.Int32", "System.Boolean"}), CanvasController_Show_Hook);

  WindowDpiScale(true);
}
} // namespace

void AdjustUiScale(bool up)
{
  auto& config = Config::Get();
  if (config.graphics.ui_scale == 0.0f)
    return;

  const auto old_scale     = config.graphics.ui_scale;
  const auto new_scale     = old_scale + (up ? 1.0f : -1.0f) * config.graphics.ui_scale_step;
  config.graphics.ui_scale = std::clamp(new_scale, 0.1f, 2.0f);
  spdlog::info("UI has been scaled {}, was {}, now {} (unclamped {}) @ {} DPI Scaling", up ? "UP" : "DOWN", old_scale,
               config.graphics.ui_scale, new_scale, WindowDpiScale(true));
}

void AdjustUiViewerScale(bool up)
{
  auto& config = Config::Get();
  if (config.graphics.ui_scale_viewer == 0.0f)
    return;

  // Viewers change in quarter steps; a full step is too coarse at their size.
  const auto old_scale            = config.graphics.ui_scale_viewer;
  const auto new_scale            = old_scale + (up ? 1.0f : -1.0f) * config.graphics.ui_scale_step * 0.25f;
  config.graphics.ui_scale_viewer = std::clamp(new_scale, 0.1f, 2.0f);
  spdlog::info("UI Viewer has been scaled {}, was {}, now {} (unclamped {})", up ? "UP" : "DOWN", old_scale,
               config.graphics.ui_scale_viewer, new_scale);
}

void AdjustUiShipScale(bool up)
{
  auto&      config             = Config::Get();
  const auto old_scale          = config.graphics.ui_scale_ship;
  const auto new_scale          = old_scale + (up ? 1.0f : -1.0f) * config.graphics.ui_scale_step;
  config.graphics.ui_scale_ship = std::clamp(new_scale, 0.1f, 20.0f);
  spdlog::info("System ship models have been scaled {}, was {}, now {} (unclamped {})", up ? "UP" : "DOWN", old_scale,
               config.graphics.ui_scale_ship, new_scale);
  RescaleLoadedShips(old_scale, config.graphics.ui_scale_ship);
}

const Feature kUiScale{"UiScale", &Config::Patches::uiscalehooks, InstallUiScale};

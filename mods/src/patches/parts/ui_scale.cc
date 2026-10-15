// UI, object viewer, and system ship scaling, and the hotkeys that adjust them at runtime.

#include "config.h"
#include "patches/object_tracker.h"
#include "patches/patches.h"
#include "str_utils.h"

#include <prime/fleet.h>
#include <prime/ui.h>
#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <cmath>

#if _WIN32
#include <Windows.h>
#endif

namespace
{
float ShipScaleMultiplier()
{
  const auto multiplier = Config::Get().ui_scale_ship;
  return multiplier > 0.0f ? multiplier : 1.0f;
}

// Ships already in the system keep the scale they were loaded with, so rescale them by the change.
void RescaleLoadedShips(float old_multiplier, float new_multiplier)
{
  if (old_multiplier <= 0.0f || new_multiplier <= 0.0f)
    return;

  const auto ratio = new_multiplier / old_multiplier;
  for (auto* selector : ObjectFinder<FleetMeshSelector>::GetAll()) {
    auto* object    = selector ? selector->LoadedObject() : nullptr;
    auto* transform = object ? object->Get<"transform", Transform*>() : nullptr;
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
  if (!Config::Get().allow_cursor)
    SetCursor(cursor);
#endif

  const auto ui_scale = Config::Get().ui_scale;
  if (ui_scale == 0.0f)
    return;

  auto factor = ui_scale * WindowDpiScale();
  factor      = std::isnan(factor) ? 1.0f : std::clamp(factor, 0.1f, 5.0f);
  // Property setter, not the managed SetScaleFactor method; the scaler applies it on its next Update.
  if (auto* scaler = _this->canvasRootScaler())
    scaler->set_scaleFactor(factor);
}

void CanvasController_Show(auto original, CanvasController* _this, int desiredEntryPoint, bool instant)
{
  const auto viewer_scale = Config::Get().ui_scale_viewer;
  if (_this && viewer_scale != 0.0f && to_string(_this->name()) == "ObjectViewerTemplate_Canvas") {
    if (auto* transform = _this->transform())
      transform->SetLocalScale({viewer_scale, viewer_scale, viewer_scale});
  }
  return original(_this, desiredEntryPoint, instant);
}
} // namespace

void AdjustUiScale(bool up)
{
  auto& config = Config::Get();
  if (config.ui_scale == 0.0f)
    return;

  const auto old_scale = config.ui_scale;
  const auto new_scale = old_scale + (up ? 1.0f : -1.0f) * config.ui_scale_step;
  config.ui_scale      = std::clamp(new_scale, 0.1f, 2.0f);
  spdlog::info("UI has been scaled {}, was {}, now {} (unclamped {}) @ {} DPI Scaling", up ? "UP" : "DOWN", old_scale,
               config.ui_scale, new_scale, WindowDpiScale(true));
}

void AdjustUiViewerScale(bool up)
{
  auto& config = Config::Get();
  if (config.ui_scale_viewer == 0.0f)
    return;

  // Viewers change in quarter steps; a full step is too coarse at their size.
  const auto old_scale   = config.ui_scale_viewer;
  const auto new_scale   = old_scale + (up ? 1.0f : -1.0f) * config.ui_scale_step * 0.25f;
  config.ui_scale_viewer = std::clamp(new_scale, 0.1f, 2.0f);
  spdlog::info("UI Viewer has been scaled {}, was {}, now {} (unclamped {})", up ? "UP" : "DOWN", old_scale,
               config.ui_scale_viewer, new_scale);
}

void AdjustUiShipScale(bool up)
{
  auto&      config    = Config::Get();
  const auto old_scale = config.ui_scale_ship;
  const auto new_scale = old_scale + (up ? 1.0f : -1.0f) * config.ui_scale_step;
  config.ui_scale_ship = std::clamp(new_scale, 0.1f, 20.0f);
  spdlog::info("System ship models have been scaled {}, was {}, now {} (unclamped {})", up ? "UP" : "DOWN", old_scale,
               config.ui_scale_ship, new_scale);
  RescaleLoadedShips(old_scale, config.ui_scale_ship);
}

void InstallUiScaleHooks()
{
  using SystemViewShip = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "SystemViewShip">;

  if (auto method = SystemViewShip::Method("TryGetShipAssetBundleResource", 2))
    SPUD_STATIC_DETOUR(method.Pointer(), SystemViewShip_TryGetShipAssetBundleResource_Hook);

  if (auto method = ScreenManager::Class::Method("UpdateCanvasRootScaleFactor"))
    SPUD_STATIC_DETOUR(method.Pointer(), ScreenManager_UpdateCanvasRootScaleFactor_Hook);

  if (auto method = CanvasController::Class::Method("Show", {"System.Int32", "System.Boolean"}))
    SPUD_STATIC_DETOUR(method.Pointer(), CanvasController_Show);

  WindowDpiScale(true);
}

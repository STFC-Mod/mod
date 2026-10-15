#include "patches/object_tracker.h"
#include <config.h>

#include <il2cpp/il2cpp.h>

#include <prime/unity.h>
#include <prime/fleet.h>
#include <prime/ui.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <str_utils.h>

namespace
{
float ShipScaleMultiplier()
{
  const auto multiplier = Config::Get().ui_scale_ship;
  return multiplier > 0.0f ? multiplier : 1.0f;
}

Transform* GetGameObjectTransform(GameObject* game_object)
{
  return game_object ? game_object->Get<"transform", Transform*>() : nullptr;
}
} // namespace

void ApplyUiShipScaleToLoadedShips(float old_multiplier, float new_multiplier)
{
  if (old_multiplier <= 0.0f || new_multiplier <= 0.0f) {
    return;
  }

  const auto ratio = new_multiplier / old_multiplier;

  for (auto* selector : ObjectFinder<FleetMeshSelector>::GetAll()) {
    if (selector == nullptr) {
      continue;
    }

    auto* transform = GetGameObjectTransform(selector->LoadedObject());
    if (transform == nullptr) {
      continue;
    }

    auto    local_scale = transform->localScale();
    Vector3 adjusted_scale{local_scale.x * ratio, local_scale.y * ratio, local_scale.z * ratio};
    transform->SetLocalScale(adjusted_scale);
  }
}

bool SystemViewShip_TryGetShipAssetBundleResource_Hook(auto original, void* _this, float* scale,
                                                       void** asset_bundle_resource)
{
  const auto result = original(_this, scale, asset_bundle_resource);
  if (result && scale != nullptr) {
    *scale *= ShipScaleMultiplier();
  }
  return result;
}

void ScreenManager_UpdateCanvasRootScaleFactor_Hook(auto original, ScreenManager* _this)
{
  original(_this);

  #if _WIN32
  static auto cursor = LoadCursor(NULL, IDC_ARROW);
  if (!Config::Get().allow_cursor) {
    SetCursor(cursor);
  }
  #endif

  if (Config::Get().ui_scale != 0.0f) {
    static auto get_height_method = il2cpp::ResolveICall<int()>("UnityEngine.Screen::get_height()");
    static auto get_width_method  = il2cpp::ResolveICall<int()>("UnityEngine.Screen::get_width()");

    static auto ref_height = 1080;
    static auto ref_width  = 1920;

    auto scr_height = (float)get_height_method();
    auto scr_width  = (float)get_width_method();
    auto dpi        = Config::GetDPI();

    auto adjustedFactor = scr_height / (float)ref_height;

    if (!Config::Get().adjust_scale_res) {
      adjustedFactor = 1.0f;
    }

    auto n = (Config::Get().ui_scale * adjustedFactor * dpi);
    if (isnan(n)) {
      n = 1.0f;
    }
    n = std::clamp(n, 0.1f, 5.0f);

    // Property setter, not the managed SetScaleFactor method; the scaler applies it on its next Update.
    if (auto* scaler = _this->canvasRootScaler())
      scaler->set_scaleFactor(n);
  }
}

void CanvasController_Show(auto original, CanvasController* _this, int desiredEntryPoint, bool instant)
{
  const auto ui_scale_viewer = Config::Get().ui_scale_viewer;
  if (_this && ui_scale_viewer != 0.0f && to_string(_this->name()) == "ObjectViewerTemplate_Canvas") {
    if (auto* transform = _this->transform())
      transform->SetLocalScale(Vector3{ui_scale_viewer, ui_scale_viewer, ui_scale_viewer});
  }
  return original(_this, desiredEntryPoint, instant);
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

  Config::RefreshDPI();
}

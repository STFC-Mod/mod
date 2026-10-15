#include "config.h"

#include <patches/mapkey.h>

#include <il2cpp/il2cpp.h>

#include <prime/navigation.h>
#include <prime/unity.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <cstdint>
#include <unordered_map>

namespace
{
std::unordered_map<NavigationLOD *, NavigationFleetWidget *> fleet_label_widgets;
bool                                                         fleet_label_hooks_installed         = false;
uintptr_t                                                    active_system_zoom_id               = 0;
bool                                                         system_zoom_state_valid             = false;
ZoomLevels                                                   system_zoom_level                   = ZoomLevels::None;
float                                                        system_normalized_zoom              = 0.0f;
bool                                                         threshold_state_valid               = false;
bool                                                         player_threshold_state_expanded     = false;
bool                                                         non_player_threshold_state_expanded = false;

bool FleetLabelProfilesEnabled()
{
  const auto &config = Config::Get();
  return config.zoom_label_player.detail != FleetLabelDetail::Native
         || config.zoom_label_non_player.detail != FleetLabelDetail::Native;
}

bool FleetLabelThresholdEnabled()
{
  const auto &config = Config::Get();
  return config.zoom_label_player.detail == FleetLabelDetail::Threshold
         || config.zoom_label_non_player.detail == FleetLabelDetail::Threshold;
}

const FleetLabelProfile *FleetLabelProfileFor(NavigationFleetWidget *widget)
{
  if (widget == nullptr) {
    return nullptr;
  }

  const auto &config  = Config::Get();
  auto       *context = widget->Context();
  if (context == nullptr) {
    return &config.zoom_label_non_player;
  }

  // A missing FleetType property reads as Nonexistent.
  switch (context->FleetType()) {
    case DeployedFleetType::Player:
      return &config.zoom_label_player;
    case DeployedFleetType::Marauder:
    case DeployedFleetType::NpcInstantiated:
    case DeployedFleetType::Sentinel:
    case DeployedFleetType::Alliance:
    case DeployedFleetType::Challenge:
      return &config.zoom_label_non_player;
    case DeployedFleetType::Nonexistent:
    default:
      return &config.zoom_label_non_player;
  }
}

const FleetLabelProfile *FleetLabelProfileFor(NavigationLOD *lod)
{
  const auto widget = fleet_label_widgets.find(lod);
  if (widget == fleet_label_widgets.end() || widget->second == nullptr || widget->second->Context() == nullptr) {
    return nullptr;
  }
  return FleetLabelProfileFor(widget->second);
}

ZoomLevels ExpandedFleetLabelZoomLevel(ZoomLevels native_level)
{ return native_level == ZoomLevels::Near ? ZoomLevels::Near : ZoomLevels::Middle; }

bool FleetLabelsExpandedAt(float normalized_zoom, float threshold)
{
  if (threshold <= 0.0f) {
    return false;
  }
  if (threshold >= 1.0f) {
    return true;
  }
  return normalized_zoom <= threshold;
}

ZoomLevels FleetLabelZoomLevel(ZoomLevels native_level, const FleetLabelProfile &profile)
{
  switch (profile.detail) {
    case FleetLabelDetail::Expanded:
      return ExpandedFleetLabelZoomLevel(native_level);
    case FleetLabelDetail::Compact:
      return ZoomLevels::Far;
    case FleetLabelDetail::Threshold:
      if (!system_zoom_state_valid) {
        return native_level;
      }
      return FleetLabelsExpandedAt(system_normalized_zoom, profile.zoom_threshold)
                 ? ExpandedFleetLabelZoomLevel(native_level)
                 : ZoomLevels::Far;
    case FleetLabelDetail::Native:
    default:
      return native_level;
  }
}

void ApplyFleetLabelDetail(NavigationLOD *lod, ZoomLevels native_level)
{
  if (lod == nullptr || !system_zoom_state_valid || FleetLabelProfileFor(lod) == nullptr) {
    return;
  }

  lod->UpdateLOD(native_level);
}

void ApplyAllFleetLabelDetails()
{
  for (const auto &[lod, widget] : fleet_label_widgets) {
    (void)widget;
    ApplyFleetLabelDetail(lod, system_zoom_level);
  }
}

void RestoreNativeFleetLabelDetail(NavigationLOD *lod)
{
  if (lod == nullptr || !system_zoom_state_valid) {
    return;
  }

  lod->SetTargetLevel(system_zoom_level);
  lod->UpdateLOD(system_zoom_level);
  lod->SetTargetLevel(system_zoom_level);
}

void ResetFleetLabelSystemState()
{
  active_system_zoom_id   = 0;
  system_zoom_state_valid = false;
  system_zoom_level       = ZoomLevels::None;
  system_normalized_zoom  = 0.0f;
  threshold_state_valid   = false;
}

void BeginFleetLabelSystemState(NavigationZoom *zoom)
{
  const auto zoom_id = reinterpret_cast<uintptr_t>(zoom);
  if (active_system_zoom_id != zoom_id) {
    ResetFleetLabelSystemState();
    active_system_zoom_id = zoom_id;
  }
}

void UpdateFleetLabelThreshold()
{
  if (!system_zoom_state_valid || !FleetLabelThresholdEnabled()) {
    threshold_state_valid = false;
    return;
  }

  const auto &config                    = Config::Get();
  const auto  player_uses_threshold     = config.zoom_label_player.detail == FleetLabelDetail::Threshold;
  const auto  non_player_uses_threshold = config.zoom_label_non_player.detail == FleetLabelDetail::Threshold;
  const auto  player_expanded =
      player_uses_threshold && FleetLabelsExpandedAt(system_normalized_zoom, config.zoom_label_player.zoom_threshold);
  const auto non_player_expanded =
      non_player_uses_threshold
      && FleetLabelsExpandedAt(system_normalized_zoom, config.zoom_label_non_player.zoom_threshold);
  if (threshold_state_valid && (!player_uses_threshold || player_expanded == player_threshold_state_expanded)
      && (!non_player_uses_threshold || non_player_expanded == non_player_threshold_state_expanded)) {
    return;
  }

  threshold_state_valid               = true;
  player_threshold_state_expanded     = player_expanded;
  non_player_threshold_state_expanded = non_player_expanded;
  ApplyAllFleetLabelDetails();

#ifdef _MODDBG
  spdlog::info("Fleet label detail threshold state updated at normalized zoom {:.4f}", system_normalized_zoom);
#endif
}
} // namespace

vec3 GetMouseWorldPos(Camera *cam, vec3 *pos)
{
  using MathUtils = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Client.Core", "MathUtils">;

  if (cam == nullptr || pos == nullptr) {
    return {0.0f, 0.0f, 0.0f};
  }

  return MathUtils::Call<"GetMouseWorldPos", vec3>(cam, *pos);
}

auto do_default_zoom = false;

inline void StoreZoom(std::string label, float &zoom, NavigationZoom *_this)
{
  auto old_zoom = zoom;
  zoom          = (_this->Distance() - _this->minimum()) / (_this->maximum() - _this->minimum()) * Config::Get().zoom;
  spdlog::info("Changing {} from {} to {}", label, old_zoom, zoom);
}

static float s_expectedScale = 0;

static void ApplySystemZoomRange(NavigationZoom *_this, float radius)
{
  if (!_this || radius <= 0.0f) {
    return;
  }

  auto ratio = (Config::Get().zoom / radius);
  _this->SetFarRatioSystemNormal(0.55f * ratio);
  _this->SetFarRatioSystemExtended(ratio);
}

static void SetSceneCameraFarClip(NavigationZoom *_this)
{
  if (!_this) {
    return;
  }

  auto *cam = _this->sceneCamera();
  if (!cam) {
    return;
  }

  cam->SetFarClipPlane(Config::Get().zoom * 3.75f);
  cam->SetClearFlags(2);
  cam->SetBackgroundColor({0, 0, 0, 0});
}

static void EnsureSystemZoomRange(NavigationZoom *_this)
{
  if (!_this || _this->depth() != NodeDepth::SolarSystem) {
    return;
  }

  const auto max_zoom = Config::Get().zoom;
  if (max_zoom <= 0.0f) {
    return;
  }

  ApplySystemZoomRange(_this, _this->viewRadius());
  if (_this->maximum() < max_zoom) {
    _this->SetMaximum(max_zoom);
  }

  const auto zoom_total = _this->maximum() - _this->minimum();
  if (zoom_total > 0.0f) {
    _this->SetZoomtotal(zoom_total);
  }

  SetSceneCameraFarClip(_this);
}

static void ScaleFR(il2cpp::Object *fr)
{
  if (!fr) {
    return;
  }

  float factor = Config::Get().fr_scale;
  if (factor <= 0.0f || factor == 1.0f) {
    return;
  }

  // FlatRenderable is a Component; transform resolves on its runtime class.
  auto *t = fr->Get<"transform", Transform *>();
  if (t == nullptr) {
    return;
  }

  auto scale = t->localScale();
  if (s_expectedScale > 0 && fabsf(scale.x - s_expectedScale) < 0.1f) {
    return;
  }

  Vector3 newScale = {scale.x * factor, scale.y * factor, scale.z * factor};
  t->SetLocalScale(newScale);
  s_expectedScale  = newScale.x;
}

void NavigationZoom_Update_Hook(auto original, NavigationZoom *_this)
{
  static auto GetMousePosition =
      il2cpp::ResolveICall<void(vec3 *)>("UnityEngine.Input::get_mousePosition_Injected(UnityEngine.Vector3&)");
  static auto GetDeltaTime = il2cpp::ResolveICall<float()>("UnityEngine.Time::get_deltaTime()");

  if (fleet_label_hooks_installed) {
    const auto zoom_id = reinterpret_cast<uintptr_t>(_this);
    if (_this->depth() == NodeDepth::SolarSystem) {
      BeginFleetLabelSystemState(_this);
    } else if (active_system_zoom_id == zoom_id) {
      ResetFleetLabelSystemState();
    }
  }

  const auto dt               = GetDeltaTime();
  auto       zoomDelta        = 0.0f;
  bool       do_absolute_zoom = false;
  bool       do_store_zoom    = false;
  auto       config           = &Config::Get();

  EnsureSystemZoomRange(_this);

  const auto camera_shortcuts_enabled = config->hotkeys_enabled && !config->use_scopely_hotkeys;

  if (!Key::IsInputFocused()) {
    if (camera_shortcuts_enabled) {
      if (MapKey::IsDown(GameFunction::SetZoomPreset1)) {
        return StoreZoom("System Preset 1", config->system_zoom_preset_1, _this);
      } else if (MapKey::IsDown(GameFunction::SetZoomPreset2)) {
        return StoreZoom("System Preset 2", config->system_zoom_preset_2, _this);
      } else if (MapKey::IsDown(GameFunction::SetZoomPreset3)) {
        return StoreZoom("System Preset 3", config->system_zoom_preset_3, _this);
      } else if (MapKey::IsDown(GameFunction::SetZoomPreset4)) {
        return StoreZoom("System Preset 4", config->system_zoom_preset_4, _this);
      } else if (MapKey::IsDown(GameFunction::SetZoomPreset5)) {
        return StoreZoom("System Preset 5", config->system_zoom_preset_5, _this);
      } else if (MapKey::IsDown(GameFunction::SetZoomDefault)) {
        return StoreZoom("System Default", config->default_system_zoom, _this);
      }

      do_absolute_zoom = true;
      if (MapKey::IsDown(GameFunction::ZoomPreset1)) {
        zoomDelta     = config->system_zoom_preset_1;
        do_store_zoom = true;
      } else if (MapKey::IsDown(GameFunction::ZoomPreset2)) {
        zoomDelta     = config->system_zoom_preset_2;
        do_store_zoom = true;
      } else if (MapKey::IsDown(GameFunction::ZoomPreset3)) {
        zoomDelta     = config->system_zoom_preset_3;
        do_store_zoom = true;
      } else if (MapKey::IsDown(GameFunction::ZoomPreset4)) {
        zoomDelta     = config->system_zoom_preset_4;
        do_store_zoom = true;
      } else if (MapKey::IsDown(GameFunction::ZoomPreset5)) {
        zoomDelta     = config->system_zoom_preset_5;
        do_store_zoom = true;
      }

      if (config->hotkeys_extended) {
        if (MapKey::IsDown(GameFunction::ZoomReset)) {
          do_absolute_zoom = false;
          do_default_zoom  = true;
        } else if (MapKey::IsDown(GameFunction::ZoomMin)) {
          zoomDelta = config->zoom;
        } else if (MapKey::IsDown(GameFunction::ZoomMax)) {
          zoomDelta = 100;
        }
      }
    }

    if (do_default_zoom) {
      do_absolute_zoom = true;
      zoomDelta        = config->default_system_zoom;
    }

    if (zoomDelta == 0.0f) {
      do_absolute_zoom = false;
      zoomDelta        = config->keyboard_zoom_speed * dt;
    }

    if ((camera_shortcuts_enabled && MapKey::IsPressed(GameFunction::ZoomIn)) || do_absolute_zoom) {
      vec3 mousePos;
      GetMousePosition(&mousePos);
      _this->SetZoomLocation(vec2{.x = mousePos.x, .y = mousePos.y});
      if (do_absolute_zoom) {
        auto zoom_distance = _this->minimum() + (_this->maximum() - _this->minimum()) * (zoomDelta / config->zoom);
        _this->SetDistance(zoom_distance);
      } else {
        _this->SetZoomDelta(zoomDelta);
        _this->SetLastZoomDelta(zoomDelta);
      }
      _this->SetWorldPoint(GetMouseWorldPos(_this->sceneCamera(), &mousePos));
      _this->ZoomCameraAtWorldPoint();
    } else if (camera_shortcuts_enabled && MapKey::IsPressed(GameFunction::ZoomOut)) {
      vec3 mousePos;
      GetMousePosition(&mousePos);
      _this->SetZoomLocation(vec2{.x = mousePos.x, .y = mousePos.y});
      _this->SetZoomDelta(-1.0f * zoomDelta);
      _this->SetLastZoomDelta(-1.0f * zoomDelta);
      _this->SetWorldPoint(GetMouseWorldPos(_this->sceneCamera(), &mousePos));
      _this->ZoomCameraAtWorldPoint();
    }
  }

  if (zoomDelta > 0.0f && config->use_presets_as_default && do_store_zoom) {
    StoreZoom("System Preset Default from Preset", config->default_system_zoom, _this);
  }

  do_default_zoom = false;

  original(_this);

  EnsureSystemZoomRange(_this);
  if (fleet_label_hooks_installed && _this->depth() == NodeDepth::SolarSystem) {
    BeginFleetLabelSystemState(_this);
    const auto state_was_valid   = system_zoom_state_valid;
    const auto threshold_enabled = FleetLabelThresholdEnabled();
    system_zoom_level            = _this->zoomLevel();
    if (threshold_enabled) {
      system_normalized_zoom = _this->NormalizedZoom();
    }
    system_zoom_state_valid = true;
    if (threshold_enabled) {
      UpdateFleetLabelThreshold();
    } else if (!state_was_valid) {
      ApplyAllFleetLabelDetails();
    }
  }
}

void NavigationLOD_UpdateLOD_Hook(auto original, NavigationLOD *_this, ZoomLevels level)
{
  const auto *profile         = FleetLabelProfileFor(_this);
  const auto  effective_level = profile != nullptr ? FleetLabelZoomLevel(level, *profile) : level;
  if (profile != nullptr) {
    _this->SetTargetLevel(effective_level);
  }
  original(_this, effective_level);
  if (profile != nullptr) {
    _this->SetTargetLevel(effective_level);
  }
}

void NavigationFleetWidget_OnEnable_Hook(auto original, NavigationFleetWidget *_this)
{
  original(_this);
  if (!_this) {
    return;
  }

  // Fresh pooled widgets are enabled before BindDataContext assigns m_context. Forcing an LOD in that window can fire
  // OnLODChanged callbacks that dereference the not-yet-bound fleet data.
  if (_this->Context() == nullptr) {
    return;
  }

  auto *lod = _this->lod();
  if (lod) {
    fleet_label_widgets.insert_or_assign(lod, _this);
    if (FleetLabelThresholdEnabled() && !system_zoom_state_valid) {
      threshold_state_valid = false;
    }
    ApplyFleetLabelDetail(lod, system_zoom_level);
  }
}

void NavigationFleetWidget_OnDisable_Hook(auto original, NavigationFleetWidget *_this)
{
  if (_this) {
    auto *lod = _this->lod();
    fleet_label_widgets.erase(lod);
    if (_this->Context() != nullptr) {
      RestoreNativeFleetLabelDetail(lod);
    }
  }
  original(_this);
}

void NavigationFleetWidget_OnDidBindContext_Hook(auto original, NavigationFleetWidget *_this)
{
  original(_this);
  if (_this == nullptr) {
    return;
  }

  auto *lod = _this->lod();
  if (lod != nullptr) {
    fleet_label_widgets.insert_or_assign(lod, _this);
    ApplyFleetLabelDetail(lod, system_zoom_level);
  }
}

void NavigationFleetWidget_OnAboutToReleaseContext_Hook(auto original, NavigationFleetWidget *_this)
{
  if (_this != nullptr) {
    auto *lod = _this->lod();
    if (lod != nullptr) {
      fleet_label_widgets.erase(lod);
      if (_this->Context() != nullptr) {
        RestoreNativeFleetLabelDetail(lod);
      }
    }
  }
  original(_this);
}

void PlanetViewUtils_CameraZoomedEventHandler_Hook(auto original, PlanetViewUtils *_this, float zoomDistance,
                                                   float normalizedZoom)
{
  original(_this, zoomDistance, normalizedZoom);

  if (_this != nullptr) {
    _this->GetFlatRenderable(); // probe: triggers get_FlatRenderable_Hook, which scales the FR; game often reads the
                                // field directly so our detour needs this call-path
  }
}

void NavigationZoom_SetViewParameters_Hook(auto original, NavigationZoom *_this, float radius, NodeDepth depth)
{
  if (fleet_label_hooks_installed && depth != NodeDepth::SolarSystem) {
    if (active_system_zoom_id == reinterpret_cast<uintptr_t>(_this)) {
      ResetFleetLabelSystemState();
    }
  } else if (fleet_label_hooks_installed) {
    BeginFleetLabelSystemState(_this);
  }

  if (depth == NodeDepth::SolarSystem) {
    ApplySystemZoomRange(_this, radius);
    SetSceneCameraFarClip(_this);

    original(_this, radius, depth);

    SetSceneCameraFarClip(_this);
    do_default_zoom = true;
  } else {
    original(_this, radius, depth);
  }
}

void NavigationZoom_SetDepth_Hook(auto original, NavigationZoom *_this, NodeDepth depth)
{
  if (fleet_label_hooks_installed && depth != NodeDepth::SolarSystem) {
    if (active_system_zoom_id == reinterpret_cast<uintptr_t>(_this)) {
      ResetFleetLabelSystemState();
    }
  } else if (fleet_label_hooks_installed) {
    BeginFleetLabelSystemState(_this);
  }

  if (depth == NodeDepth::SolarSystem) {
    ApplySystemZoomRange(_this, _this->viewRadius());
    SetSceneCameraFarClip(_this);

    original(_this, depth);

    SetSceneCameraFarClip(_this);
    do_default_zoom = true;
  } else {
    original(_this, depth);
  }
}

void *PlanetViewUtils_get_FlatRenderable_Hook(auto original, PlanetViewUtils *_this)
{
  auto *fr = original(_this);
  if (!fr) {
    return fr;
  }

  ScaleFR(static_cast<il2cpp::Object *>(fr));
  return fr;
}

void InstallZoomHooks()
{
  auto update = NavigationZoom::Class::Method("Update");

  if (FleetLabelProfilesEnabled()) {
    // Installed as a set. Missing fields and properties (_lod, Context, FleetType) read as null or zero, which the hooks
    // treat as native labels, so only the methods gate installation.
    auto update_lod                  = NavigationLOD::Class::Method("UpdateLOD");
    auto on_enable                   = NavigationFleetWidget::Class::Method("OnEnable");
    auto on_disable                  = NavigationFleetWidget::Class::Method("OnDisable");
    auto on_did_bind_context         = NavigationFleetWidget::Class::Method("OnDidBindContext");
    auto on_about_to_release_context = NavigationFleetWidget::Class::Method("OnAboutToReleaseContext");

    if (update && update_lod && on_enable && on_disable && on_did_bind_context && on_about_to_release_context) {
      fleet_label_hooks_installed = true;
      SPUD_STATIC_DETOUR(update_lod.Pointer(), NavigationLOD_UpdateLOD_Hook);
      SPUD_STATIC_DETOUR(on_enable.Pointer(), NavigationFleetWidget_OnEnable_Hook);
      SPUD_STATIC_DETOUR(on_disable.Pointer(), NavigationFleetWidget_OnDisable_Hook);
      SPUD_STATIC_DETOUR(on_did_bind_context.Pointer(), NavigationFleetWidget_OnDidBindContext_Hook);
      SPUD_STATIC_DETOUR(on_about_to_release_context.Pointer(), NavigationFleetWidget_OnAboutToReleaseContext_Hook);
    } else {
      spdlog::error("Fleet label detail hooks were not installed; using native fleet labels");
    }
  }

  if (auto method = PlanetViewUtils::Class::Method("CameraZoomedEventHandler"))
    SPUD_STATIC_DETOUR(method.Pointer(), PlanetViewUtils_CameraZoomedEventHandler_Hook);
  if (auto method = PlanetViewUtils::Class::Method("get_FlatRenderable"))
    SPUD_STATIC_DETOUR(method.Pointer(), PlanetViewUtils_get_FlatRenderable_Hook);

  if (update)
    SPUD_STATIC_DETOUR(update.Pointer(), NavigationZoom_Update_Hook);

#if _WIN32
  if (auto method = NavigationZoom::Class::Method("SetDepth"))
    SPUD_STATIC_DETOUR(method.Pointer(), NavigationZoom_SetDepth_Hook);
#endif

  if (auto method = NavigationZoom::Class::Method("SetViewParameters"))
    SPUD_STATIC_DETOUR(method.Pointer(), NavigationZoom_SetViewParameters_Hook);
}

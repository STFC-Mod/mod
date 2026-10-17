#include "config.h"
#include "errormsg.h"
#include "patches/native_hook_extent.h"
#include "settings/upstream_settings.h"

#include <patches/mapkey.h>

#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>

#include <prime/NavigationFleetWidget.h>
#include <prime/NavigationLOD.h>
#include <prime/NavigationPan.h>
#include <prime/NavigationZoom.h>
#include <prime/PlanetViewUtils.h>
#include <prime/Transform.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace
{
struct FleetLabelWidget {
  Il2CppGCHandle lod{}, widget{};
  bool           overridden = false;
};
std::unordered_map<NavigationLOD *, FleetLabelWidget> fleet_label_widgets;
using FleetLabelAliveFn             = bool (*)(void *);
FleetLabelAliveFn fleet_label_alive = nullptr;
bool              FleetLabelAlive(void *object)
{ return object && fleet_label_alive && fleet_label_alive(object); }
struct FleetLabelRoot {
  Il2CppGCHandle handle{};
  explicit FleetLabelRoot(void *object)
      : handle(object ? il2cpp_gchandle_new(static_cast<Il2CppObject *>(object), false) : nullptr)
  {
  }
  ~FleetLabelRoot()
  {
    if (handle)
      il2cpp_gchandle_free(handle);
  }
  FleetLabelRoot(const FleetLabelRoot &) = delete;
};
void FreeFleetLabelWidget(FleetLabelWidget &entry)
{
  if (entry.lod)
    il2cpp_gchandle_free(entry.lod);
  if (entry.widget)
    il2cpp_gchandle_free(entry.widget);
  entry = {};
}
bool ForgetFleetLabelWidget(NavigationLOD *lod, NavigationFleetWidget *widget)
{
  const auto found = fleet_label_widgets.find(lod);
  if (found == fleet_label_widgets.end()
      || il2cpp_gchandle_get_target(found->second.widget) != reinterpret_cast<Il2CppObject *>(widget))
    return false;
  const bool overridden = found->second.overridden;
  FreeFleetLabelWidget(found->second);
  fleet_label_widgets.erase(found);
  return overridden;
}
void TrackFleetLabelWidget(NavigationLOD *lod, NavigationFleetWidget *widget)
{
  if (!FleetLabelAlive(lod) || !FleetLabelAlive(widget) || !widget->Context)
    return;
  const auto found = fleet_label_widgets.find(lod);
  if (found != fleet_label_widgets.end()
      && il2cpp_gchandle_get_target(found->second.lod) == reinterpret_cast<Il2CppObject *>(lod)
      && il2cpp_gchandle_get_target(found->second.widget) == reinterpret_cast<Il2CppObject *>(widget))
    return;
  FleetLabelWidget entry{il2cpp_gchandle_new_weakref(reinterpret_cast<Il2CppObject *>(lod), false),
                         il2cpp_gchandle_new_weakref(reinterpret_cast<Il2CppObject *>(widget), false)};
  if (!entry.lod || !entry.widget) {
    FreeFleetLabelWidget(entry);
    return;
  }
  if (found != fleet_label_widgets.end())
    FreeFleetLabelWidget(found->second);
  fleet_label_widgets.insert_or_assign(lod, entry);
}
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
  if (!FleetLabelAlive(widget)) {
    return nullptr;
  }
  FleetLabelRoot widget_root(widget);
  if (!widget_root.handle)
    return nullptr;

  const auto &config  = Config::Get();
  auto       *context = widget->Context;
  if (context == nullptr) {
    return &config.zoom_label_non_player;
  }
  FleetLabelRoot context_root(context);
  if (!context_root.handle)
    return nullptr;

  DeployedFleetType fleet_type;
  if (!context->TryGetFleetType(fleet_type)) {
    return &config.zoom_label_non_player;
  }

  switch (fleet_type) {
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
  const auto entry = fleet_label_widgets.find(lod);
  if (entry == fleet_label_widgets.end()
      || il2cpp_gchandle_get_target(entry->second.lod) != reinterpret_cast<Il2CppObject *>(lod)
      || !FleetLabelAlive(lod))
    return nullptr;
  auto *widget = reinterpret_cast<NavigationFleetWidget *>(il2cpp_gchandle_get_target(entry->second.widget));
  return FleetLabelAlive(widget) && widget->Context ? FleetLabelProfileFor(widget) : nullptr;
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

void RestoreNativeFleetLabelDetail(NavigationLOD *lod)
{
  if (!FleetLabelAlive(lod) || !system_zoom_state_valid)
    return;
  FleetLabelRoot root(lod);
  if (!root.handle)
    return;
  lod->SetTargetLevel(system_zoom_level);
  lod->UpdateLOD(system_zoom_level);
  if (FleetLabelAlive(lod))
    lod->SetTargetLevel(system_zoom_level);
}

void ApplyFleetLabelDetail(NavigationLOD *lod, ZoomLevels native_level)
{
  if (!system_zoom_state_valid)
    return;
  const auto *profile = FleetLabelProfileFor(lod);
  if (!profile)
    return;
  if (profile->detail == FleetLabelDetail::Native) {
    const auto found = fleet_label_widgets.find(lod);
    if (found != fleet_label_widgets.end() && found->second.overridden) {
      found->second.overridden = false;
      RestoreNativeFleetLabelDetail(lod);
    }
    return;
  }
  FleetLabelRoot root(lod);
  if (root.handle)
    lod->UpdateLOD(native_level);
}

void ApplyAllFleetLabelDetails()
{
  // Native LOD callbacks can release/rebind other widgets. Snapshot identities,
  // then re-resolve weak roots before each call; never iterate across callbacks.
  std::vector<NavigationLOD *> labels;
  labels.reserve(fleet_label_widgets.size());
  for (auto entry = fleet_label_widgets.begin(); entry != fleet_label_widgets.end();) {
    if (il2cpp_gchandle_get_target(entry->second.lod) != reinterpret_cast<Il2CppObject *>(entry->first)
        || !FleetLabelAlive(entry->first) || !FleetLabelAlive(il2cpp_gchandle_get_target(entry->second.widget))) {
      FreeFleetLabelWidget(entry->second);
      entry = fleet_label_widgets.erase(entry);
    } else {
      labels.push_back(entry->first);
      ++entry;
    }
  }
  for (auto *lod : labels)
    ApplyFleetLabelDetail(lod, system_zoom_level);
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

bool FleetLabelControlsAvailable()
{ return fleet_label_hooks_installed; }

void RefreshFleetLabelControls()
{
  if (!fleet_label_hooks_installed)
    return;
  threshold_state_valid = false;
  ApplyAllFleetLabelDetails();
}

vec3 GetMouseWorldPos(void *cam, vec3 *pos)
{
  static auto class_helper = il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.Client.Core", "MathUtils");
  static auto fn           = class_helper.GetMethodInfo("GetMouseWorldPos");

  if (fn == nullptr || cam == nullptr || pos == nullptr) {
    return {0.0f, 0.0f, 0.0f};
  }

  void            *args[2]   = {cam, (void *)pos};
  Il2CppException *exception = nullptr;
  auto             result    = il2cpp_runtime_invoke(fn, nullptr, args, &exception);
  if (exception != nullptr || result == nullptr) {
    return {0.0f, 0.0f, 0.0f};
  }

  auto unboxed = il2cpp_object_unbox(result);
  return unboxed != nullptr ? *reinterpret_cast<vec3 *>(unboxed) : vec3{0.0f, 0.0f, 0.0f};
}

auto do_default_zoom = false;

inline void StoreZoom(std::string label, float &zoom, NavigationZoom *_this, const char *key = nullptr)
{
  auto old_zoom = zoom;
  zoom          = (_this->Distance - _this->_minimum) / (_this->_maximum - _this->_minimum) * Config::Get().zoom;
  spdlog::info("Changing {} from {} to {}", label, old_zoom, zoom);
  if (key)
    mod_settings::PersistCurrentSetting("graphics", key);
}

static float s_expectedScale = 0;
static uint64_t s_backdropLoadRevision = 0;

static void ApplySystemZoomRange(NavigationZoom *_this, float radius)
{
  if (!_this || radius <= 0.0f) {
    return;
  }

  auto ratio                     = (Config::Get().zoom / radius);
  _this->_farRatioSystemNormal   = 0.55f * ratio;
  _this->_farRatioSystemExtended = ratio;
}

static void SetSceneCameraFarClip(NavigationZoom *_this)
{
  if (!_this) {
    return;
  }

  auto *cam = _this->_sceneCamera;
  if (!cam) {
    return;
  }

  cam->farClipPlane    = Config::Get().zoom * 3.75f;
  cam->clearFlags      = 2;
  cam->backgroundColor = {0, 0, 0, 0};
}

static void EnsureSystemZoomRange(NavigationZoom *_this)
{
  if (!_this || _this->_depth != NodeDepth::SolarSystem) {
    return;
  }

  const auto max_zoom = Config::Get().zoom;
  if (max_zoom <= 0.0f) {
    return;
  }

  ApplySystemZoomRange(_this, _this->_viewRadius);
  if (_this->_maximum < max_zoom) {
    _this->_maximum = max_zoom;
  }

  const auto zoom_total = _this->_maximum - _this->_minimum;
  if (zoom_total > 0.0f) {
    _this->_zoomtotal = zoom_total;
  }

  SetSceneCameraFarClip(_this);
}

static void ScaleFR(void *fr)
{
  if (!fr) {
    return;
  }

  float factor = Config::Get().fr_scale;
  if (factor <= 0.0f || factor == 1.0f) {
    return;
  }

  static auto comp_helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Component");
  if (!comp_helper.isValidHelper()) {
    return;
  }

  static auto get_transform = comp_helper.GetProperty("transform");
  if (!get_transform.isValidHelper()) {
    return;
  }

  auto *t = reinterpret_cast<Transform *>(get_transform.GetRaw<Il2CppObject>(fr));
  if (t == nullptr) {
    return;
  }

  auto *scale = t->localScale;
  if (scale == nullptr || (s_expectedScale > 0 && fabsf(scale->x - s_expectedScale) < 0.1f)) {
    return;
  }

  Vector3 newScale = {scale->x * factor, scale->y * factor, scale->z * factor};
  t->localScale    = &newScale;
  s_expectedScale  = newScale.x;
}

void NavigationZoom_Update_Hook(auto original, NavigationZoom *_this)
{
  static auto GetMousePosition =
      il2cpp_resolve_icall_typed<void(vec3 *)>("UnityEngine.Input::get_mousePosition_Injected(UnityEngine.Vector3&)");
  static auto GetDeltaTime = il2cpp_resolve_icall_typed<float()>("UnityEngine.Time::get_deltaTime()");

  if (fleet_label_hooks_installed) {
    const auto zoom_id = reinterpret_cast<uintptr_t>(_this);
    if (_this->_depth == NodeDepth::SolarSystem) {
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
        return StoreZoom("System Preset 1", config->system_zoom_preset_1, _this, "system_zoom_preset_1");
      } else if (MapKey::IsDown(GameFunction::SetZoomPreset2)) {
        return StoreZoom("System Preset 2", config->system_zoom_preset_2, _this, "system_zoom_preset_2");
      } else if (MapKey::IsDown(GameFunction::SetZoomPreset3)) {
        return StoreZoom("System Preset 3", config->system_zoom_preset_3, _this, "system_zoom_preset_3");
      } else if (MapKey::IsDown(GameFunction::SetZoomPreset4)) {
        return StoreZoom("System Preset 4", config->system_zoom_preset_4, _this, "system_zoom_preset_4");
      } else if (MapKey::IsDown(GameFunction::SetZoomPreset5)) {
        return StoreZoom("System Preset 5", config->system_zoom_preset_5, _this, "system_zoom_preset_5");
      } else if (MapKey::IsDown(GameFunction::SetZoomDefault)) {
        return StoreZoom("System Default", config->default_system_zoom, _this, "default_system_zoom");
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
      _this->_zoomLocation = vec2{.x = mousePos.x, .y = mousePos.y};
      if (do_absolute_zoom) {
        auto zoom_distance = _this->_minimum + (_this->_maximum - _this->_minimum) * (zoomDelta / config->zoom);
        _this->Distance    = zoom_distance;
      } else {
        _this->_zoomDelta     = zoomDelta;
        _this->_lastZoomDelta = zoomDelta;
      }
      auto worldPos      = GetMouseWorldPos(_this->_sceneCamera, &mousePos);
      _this->_worldPoint = worldPos;
      _this->ZoomCameraAtWorldPoint();
    } else if (camera_shortcuts_enabled && MapKey::IsPressed(GameFunction::ZoomOut)) {
      vec3 mousePos;
      GetMousePosition(&mousePos);
      _this->_zoomLocation  = vec2{.x = mousePos.x, .y = mousePos.y};
      _this->_zoomDelta     = -1.0f * zoomDelta;
      _this->_lastZoomDelta = -1.0f * zoomDelta;
      auto worldPos         = GetMouseWorldPos(_this->_sceneCamera, &mousePos);
      _this->_worldPoint    = worldPos;
      _this->ZoomCameraAtWorldPoint();
    }
  }

  if (zoomDelta > 0.0f && config->use_presets_as_default && do_store_zoom) {
    StoreZoom("System Preset Default from Preset", config->default_system_zoom, _this);
  }

  do_default_zoom = false;

  original(_this);

  EnsureSystemZoomRange(_this);
  if (fleet_label_hooks_installed && _this->_depth == NodeDepth::SolarSystem) {
    BeginFleetLabelSystemState(_this);
    const auto state_was_valid   = system_zoom_state_valid;
    const auto threshold_enabled = FleetLabelThresholdEnabled();
    system_zoom_level            = _this->_zoomLevel;
    if (threshold_enabled) {
      system_normalized_zoom = _this->NormalizedZoom;
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
  if (!fleet_label_hooks_installed || !FleetLabelProfilesEnabled()) {
    original(_this, level);
    return;
  }
  const auto *profile = fleet_label_hooks_installed ? FleetLabelProfileFor(_this) : nullptr;
  if (!profile || profile->detail == FleetLabelDetail::Native) {
    original(_this, level);
    return;
  }
  FleetLabelRoot root(_this);
  if (!root.handle) {
    original(_this, level);
    return;
  }
  const auto effective_level               = FleetLabelZoomLevel(level, *profile);
  fleet_label_widgets.at(_this).overridden = true;
  _this->SetTargetLevel(effective_level);
  original(_this, effective_level);
  if (FleetLabelAlive(_this) && FleetLabelProfileFor(_this))
    _this->SetTargetLevel(effective_level);
}

void NavigationFleetWidget_OnEnable_Hook(auto original, NavigationFleetWidget *_this)
{
  original(_this);
  if (!fleet_label_hooks_installed || !FleetLabelAlive(_this)) {
    return;
  }

  // Fresh pooled widgets are enabled before BindDataContext assigns m_context. Forcing an LOD in that window can fire
  // OnLODChanged callbacks that dereference the not-yet-bound fleet data.
  if (_this->Context == nullptr) {
    return;
  }

  auto *lod = _this->_lod;
  if (lod) {
    TrackFleetLabelWidget(lod, _this);
    if (FleetLabelThresholdEnabled() && !system_zoom_state_valid) {
      threshold_state_valid = false;
    }
    ApplyFleetLabelDetail(lod, system_zoom_level);
  }
}

void NavigationFleetWidget_OnDisable_Hook(auto original, NavigationFleetWidget *_this)
{
  if (fleet_label_hooks_installed && FleetLabelAlive(_this)) {
    auto *lod = _this->_lod;
    if (ForgetFleetLabelWidget(lod, _this) && _this->Context)
      RestoreNativeFleetLabelDetail(lod);
  }
  original(_this);
}

void NavigationFleetWidget_OnDidBindContext_Hook(auto original, NavigationFleetWidget *_this)
{
  original(_this);
  if (!fleet_label_hooks_installed || !FleetLabelAlive(_this)) {
    return;
  }

  auto *lod = _this->_lod;
  if (lod != nullptr) {
    TrackFleetLabelWidget(lod, _this);
    ApplyFleetLabelDetail(lod, system_zoom_level);
  }
}

void NavigationFleetWidget_OnAboutToReleaseContext_Hook(auto original, NavigationFleetWidget *_this)
{
  if (fleet_label_hooks_installed && FleetLabelAlive(_this)) {
    auto *lod = _this->_lod;
    if (ForgetFleetLabelWidget(lod, _this) && _this->Context)
      RestoreNativeFleetLabelDetail(lod);
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

void PlanetViewUtils_CreateBackdrop_Hook(auto original, PlanetViewUtils *_this, Il2CppObject *backdrop,
                                        Il2CppObject *resource, bool usingDefault)
{
  original(_this, backdrop, resource, usingDefault);
  // Native creation has now assigned the selected scenery's authored transform.
  // Reuse the existing getter/scaling path before any player zoom event.
  if (_this && resource) {
    ++s_backdropLoadRevision;
    // A newly authored scale must not be compared with the previous system.
    s_expectedScale = 0;
    _this->GetFlatRenderable();
  }
}

void PlanetViewUtils_CreateBackdropCollection_Hook(auto original, PlanetViewUtils *_this, Il2CppObject *backdrop,
                                                  Il2CppObject *resource, bool usingDefault)
{
  original(_this, backdrop, resource, usingDefault);
  // The collection overload can load scenery without the Object callback.
  if (_this && resource) {
    ++s_backdropLoadRevision;
    s_expectedScale = 0;
    _this->GetFlatRenderable();
  }
}

void PlanetViewUtils_Populate_Hook(auto original, PlanetViewUtils *_this, Il2CppObject *system, int64_t nodeId,
                                  NodeDepth depth)
{
  auto      *previous_population = _this ? _this->_popData : nullptr;
  const auto load_revision       = s_backdropLoadRevision;
  original(_this, system, nodeId, depth);

  // Cached variations reload their authored transforms in Populate rather than CreateBackdrop.
  // A cached prefab backdrop already retains its scaled transform and must not be scaled again.
  auto *population = _this ? _this->_popData : nullptr;
  if (depth == NodeDepth::SolarSystem && population && population != previous_population
      && population->VariationCollection && load_revision == s_backdropLoadRevision) {
    s_expectedScale = 0;
    _this->GetFlatRenderable();
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
    ApplySystemZoomRange(_this, _this->_viewRadius);
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

  ScaleFR(fr);
  return fr;
}

void InstallZoomHooks()
{
  auto  navigation_zoom_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "NavigationZoom");
  const auto *update_method =
      method_contract::Resolve(navigation_zoom_helper.get_cls(), "Update", false, "System.Void", {});
  auto *ptr_update               = method_contract::Pointer(update_method);
  bool  label_surface_ready      = false;
  auto *navigation_zoom_class    = navigation_zoom_helper.get_cls();
  auto *zoom_level_field         = navigation_zoom_class != nullptr
                                       ? il2cpp_class_get_field_from_name(navigation_zoom_class, "_zoomLevel")
                                       : nullptr;
  auto *normalized_zoom_property = navigation_zoom_class != nullptr
                                       ? il2cpp_class_get_property_from_name(navigation_zoom_class, "NormalizedZoom")
                                       : nullptr;
  // Install independently of the selected profile so settings can switch from
  // Native to an override later in the same session.
  {
    auto lod_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "NavigationLOD");
    auto fleet_widget_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "NavigationFleetWidget");
    auto fleet_data_helper =
        il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "FleetDeployedData");
    const auto *update_lod = method_contract::Resolve(lod_helper.get_cls(), "UpdateLOD", false, "System.Void",
                                                      {"Digit.Prime.Navigation.ZoomLevels"});
    const auto *on_enable =
        method_contract::Resolve(fleet_widget_helper.get_cls(), "OnEnable", false, "System.Void", {});
    const auto *on_disable =
        method_contract::Resolve(fleet_widget_helper.get_cls(), "OnDisable", false, "System.Void", {});
    const auto *did_bind =
        method_contract::Resolve(fleet_widget_helper.get_cls(), "OnDidBindContext", false, "System.Void", {});
    const auto *release =
        method_contract::Resolve(fleet_widget_helper.get_cls(), "OnAboutToReleaseContext", false, "System.Void", {});
    auto *ptr_update_lod                  = method_contract::Pointer(update_lod);
    auto *ptr_on_enable                   = method_contract::Pointer(on_enable);
    auto *ptr_on_disable                  = method_contract::Pointer(on_disable);
    auto *ptr_on_did_bind_context         = method_contract::Pointer(did_bind);
    auto *ptr_on_about_to_release_context = method_contract::Pointer(release);
    auto *lod_class = lod_helper.get_cls();
    auto *target_level_field =
        lod_class != nullptr ? il2cpp_class_get_field_from_name(lod_class, "<TargetLevel>k__BackingField") : nullptr;
    auto *fleet_widget_class = fleet_widget_helper.get_cls();
    auto *lod_field =
        fleet_widget_class != nullptr ? il2cpp_class_get_field_from_name(fleet_widget_class, "_lod") : nullptr;
    auto *context_field    = NavigationFleetWidget::ContextField();
    auto *fleet_data_class = fleet_data_helper.get_cls();
    auto *fleet_type_property =
        fleet_data_class != nullptr ? il2cpp_class_get_property_from_name(fleet_data_class, "FleetType") : nullptr;
    auto *fleet_type_getter =
        fleet_type_property != nullptr ? il2cpp_property_get_get_method((PropertyInfo *)fleet_type_property) : nullptr;

    if (!lod_helper.isValidHelper()) {
      ErrorMsg::MissingHelper("Navigation", "NavigationLOD");
    } else {
      if (ptr_update_lod == nullptr) {
        ErrorMsg::MissingMethod("NavigationLOD", "UpdateLOD");
      }
      if (target_level_field == nullptr) {
        ErrorMsg::MissingMethod("NavigationLOD", "<TargetLevel>k__BackingField");
      }
    }
    if (!fleet_widget_helper.isValidHelper()) {
      ErrorMsg::MissingHelper("Navigation", "NavigationFleetWidget");
    } else {
      if (ptr_on_enable == nullptr) {
        ErrorMsg::MissingMethod("NavigationFleetWidget", "OnEnable");
      }
      if (ptr_on_disable == nullptr) {
        ErrorMsg::MissingMethod("NavigationFleetWidget", "OnDisable");
      }
      if (ptr_on_did_bind_context == nullptr) {
        ErrorMsg::MissingMethod("NavigationFleetWidget", "OnDidBindContext");
      }
      if (ptr_on_about_to_release_context == nullptr) {
        ErrorMsg::MissingMethod("NavigationFleetWidget", "OnAboutToReleaseContext");
      }
      if (lod_field == nullptr) {
        ErrorMsg::MissingMethod("NavigationFleetWidget", "_lod");
      }
      if (context_field == nullptr) {
        ErrorMsg::MissingMethod("NavigationFleetWidget", "m_context");
      }
    }
    if (!fleet_data_helper.isValidHelper()) {
      ErrorMsg::MissingHelper("Digit.PrimeServer.Models", "FleetDeployedData");
    } else if (fleet_type_property == nullptr || fleet_type_getter == nullptr) {
      ErrorMsg::MissingMethod("FleetDeployedData", "FleetType");
    }
    if (!navigation_zoom_helper.isValidHelper()) {
      ErrorMsg::MissingHelper("Navigation", "NavigationZoom");
    } else {
      if (ptr_update == nullptr) {
        ErrorMsg::MissingMethod("NavigationZoom", "Update");
      }
      if (zoom_level_field == nullptr) {
        ErrorMsg::MissingMethod("NavigationZoom", "_zoomLevel");
      }
      if (normalized_zoom_property == nullptr) {
        ErrorMsg::MissingMethod("NavigationZoom", "NormalizedZoom");
      }
    }

    auto        object_helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Object");
    const auto *alive         = method_contract::Resolve(object_helper.get_cls(), "op_Implicit", true, "System.Boolean",
                                                         {"UnityEngine.Object"});
    const auto *normalized =
        method_contract::Resolve(navigation_zoom_class, "get_NormalizedZoom", false, "System.Single", {});
    const auto *fleet_type = method_contract::Resolve(fleet_data_class, "get_FleetType", false,
                                                      "Digit.PrimeServer.Models.DeployedFleetType", {});
    const auto  fieldFits  = [](const FieldInfo *field, Il2CppClass *owner, std::size_t bytes) {
      return field && field->type && !field->type->byref && owner
             && !(il2cpp_field_get_flags(const_cast<FieldInfo *>(field)) & FIELD_ATTRIBUTE_STATIC)
             && field->offset >= sizeof(Il2CppObject) && field->offset + bytes <= il2cpp_class_instance_size(owner);
    };
    const auto enumField = [&](const FieldInfo *field, Il2CppClass *owner) {
      if (!fieldFits(field, owner, sizeof(ZoomLevels))
          || !method_contract::Type(field->type, "Digit.Prime.Navigation.ZoomLevels"))
        return false;
      auto       *cls     = il2cpp_class_from_type(field->type);
      const auto *storage = cls && il2cpp_class_is_enum(cls) ? il2cpp_class_enum_basetype(cls) : nullptr;
      return storage && storage->type == IL2CPP_TYPE_I4;
    };
    const auto referenceField = [&](const FieldInfo *field, Il2CppClass *owner) {
      return fieldFits(field, owner, sizeof(void *))
             && (field->type->type == IL2CPP_TYPE_CLASS || field->type->type == IL2CPP_TYPE_GENERICINST
                 || field->type->type == IL2CPP_TYPE_OBJECT || field->type->type == IL2CPP_TYPE_VAR);
    };
    const std::array methods{update_lod, on_enable, on_disable, did_bind, release, update_method};
    bool             signatures_valid = alive && normalized && fleet_type;
    for (std::size_t i = 0; i < methods.size(); ++i) {
      if (!methods[i] || methods[i]->has_full_generic_sharing_signature) {
        signatures_valid = false;
        continue;
      }
      for (std::size_t j = 0; j < i; ++j)
        if (methods[j] && methods[i]->methodPointer == methods[j]->methodPointer)
          signatures_valid = false;
    }
    for (const auto *method : {alive, normalized, fleet_type})
      if (method && method->has_full_generic_sharing_signature)
        signatures_valid = false;
#if defined(__APPLE__)
    // Profiles can now be enabled after startup, so these five detours are
    // installed even when the initial profile is Native. Check every entry
    // before any of them is patched; unsupported Mac images retain native LOD.
    for (const auto *method : {update_lod, on_enable, on_disable, did_bind, release})
      if (method && !native_hooks::MacHookFits(reinterpret_cast<const void *>(method->methodPointer))) {
        signatures_valid = false;
        spdlog::warn("Fleet labels: {}.{} has no verified native hook extent; controls unavailable",
                     method->klass->name, method->name);
      }
#endif
    const auto fields_valid = enumField(target_level_field, lod_class)
                              && enumField(zoom_level_field, navigation_zoom_class)
                              && referenceField(lod_field, fleet_widget_class)
                              && method_contract::Type(lod_field->type, "Digit.Prime.Navigation.NavigationLOD")
                              && referenceField(context_field, fleet_widget_class);
    const auto fleet_label_dependencies_valid =
        signatures_valid && fields_valid && lod_helper.isValidHelper() && ptr_update_lod != nullptr
        && target_level_field != nullptr && fleet_widget_helper.isValidHelper() && ptr_on_enable != nullptr
        && ptr_on_disable != nullptr && ptr_on_did_bind_context != nullptr && ptr_on_about_to_release_context != nullptr
        && lod_field != nullptr && context_field != nullptr && fleet_data_helper.isValidHelper()
        && fleet_type_property != nullptr && fleet_type_getter != nullptr && navigation_zoom_helper.isValidHelper()
        && ptr_update != nullptr && zoom_level_field != nullptr && normalized_zoom_property != nullptr;
    if (fleet_label_dependencies_valid) {
      fleet_label_alive = reinterpret_cast<FleetLabelAliveFn>(method_contract::Pointer(alive));
      // A partial installation stays on the original native paths. The final
      // capability also requires the shared NavigationZoom.Update owner below.
      const std::array installed{
          bool(SPUD_STATIC_DETOUR(ptr_update_lod, NavigationLOD_UpdateLOD_Hook)),
          bool(SPUD_STATIC_DETOUR(ptr_on_enable, NavigationFleetWidget_OnEnable_Hook)),
          bool(SPUD_STATIC_DETOUR(ptr_on_disable, NavigationFleetWidget_OnDisable_Hook)),
          bool(SPUD_STATIC_DETOUR(ptr_on_did_bind_context, NavigationFleetWidget_OnDidBindContext_Hook)),
          bool(
              SPUD_STATIC_DETOUR(ptr_on_about_to_release_context, NavigationFleetWidget_OnAboutToReleaseContext_Hook))};
      label_surface_ready = std::ranges::all_of(installed, [](bool installed) { return installed; });
      if (!label_surface_ready)
        spdlog::error("Fleet label detail hook installation incomplete; using native fleet labels");
    } else {
      spdlog::error("Fleet label detail hooks were not installed; using native fleet labels");
    }
  }

  {
    auto pv_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "PlanetViewUtils");
    if (pv_helper.isValidHelper()) {
      auto ptr_populate = pv_helper.GetMethod("Populate");
      if (ptr_populate)
        SPUD_STATIC_DETOUR(ptr_populate, PlanetViewUtils_Populate_Hook);
      else
        ErrorMsg::MissingMethod("PlanetViewUtils", "Populate");

      const auto *create = method_contract::Resolve(
          pv_helper.get_cls(), "CreateBackdrop", false, "System.Void",
          {"Digit.PrimeServer.Models.ITreeNode", "UnityEngine.Object", "System.Boolean"});
      const auto *create_collection = method_contract::Resolve(
          pv_helper.get_cls(), "CreateBackdrop", false, "System.Void",
          {"Digit.PrimeServer.Models.ITreeNode", "SystemVariationConfigCollection", "System.Boolean"});
      if (create)
        SPUD_STATIC_DETOUR(create->methodPointer, PlanetViewUtils_CreateBackdrop_Hook);
      else
        ErrorMsg::MissingMethod("PlanetViewUtils", "CreateBackdrop(Object)");
      if (create_collection)
        SPUD_STATIC_DETOUR(create_collection->methodPointer, PlanetViewUtils_CreateBackdropCollection_Hook);
      else
        ErrorMsg::MissingMethod("PlanetViewUtils", "CreateBackdrop(SystemVariationConfigCollection)");

      auto ptr_zoom = pv_helper.GetMethod("CameraZoomedEventHandler");
      if (ptr_zoom != nullptr) {
        SPUD_STATIC_DETOUR(ptr_zoom, PlanetViewUtils_CameraZoomedEventHandler_Hook);
      } else {
        ErrorMsg::MissingMethod("PlanetViewUtils", "CameraZoomedEventHandler");
      }

      auto ptr_get_fr = pv_helper.GetMethod("get_FlatRenderable");
      if (ptr_get_fr != nullptr) {
        SPUD_STATIC_DETOUR(ptr_get_fr, PlanetViewUtils_get_FlatRenderable_Hook);
      } else {
        ErrorMsg::MissingMethod("PlanetViewUtils", "get_FlatRenderable");
      }
    } else {
      ErrorMsg::MissingHelper("Navigation", "PlanetViewUtils");
    }
  }

  if (!navigation_zoom_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Navigation", "NavigationZoom");
  } else {
    if (ptr_update == nullptr) {
      ErrorMsg::MissingMethod("NavigationZoom", "Update");
    } else {
      const bool zoom_installed   = SPUD_STATIC_DETOUR(ptr_update, NavigationZoom_Update_Hook);
      fleet_label_hooks_installed = label_surface_ready && zoom_installed;
    }

#if _WIN32
    auto ptr_set_depth = navigation_zoom_helper.GetMethod("SetDepth");
    if (ptr_set_depth == nullptr) {
      ErrorMsg::MissingMethod("NavigationZoom", "SetDepth");
    } else {
      SPUD_STATIC_DETOUR(ptr_set_depth, NavigationZoom_SetDepth_Hook);
    }
#endif

    auto ptr_set_view_parameters = navigation_zoom_helper.GetMethod("SetViewParameters");
    if (ptr_set_view_parameters == nullptr) {
      ErrorMsg::MissingMethod("NavigationZoom", "SetViewParameters");
    } else {
      SPUD_STATIC_DETOUR(ptr_set_view_parameters, NavigationZoom_SetViewParameters_Hook);
    }
  }
}

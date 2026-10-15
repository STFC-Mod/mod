// System view camera: the extended zoom range, keyboard zoom and presets, planet flat-renderable scaling, and how
// much detail fleet labels show at a given zoom.

#include "config.h"
#include "patches/input.h"
#include "patches/shortcuts.h"

#include <prime/navigation.h>
#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <cmath>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace
{
// Fleet label detail. The game picks each fleet label's LOD from the camera's zoom level; the zoom_label_*
// settings override that per fleet kind: always expanded, always compact, or expanded below a zoom threshold.
class FleetLabels
{
public:
  bool installed = false;

  static bool Enabled()
  {
    const auto& config = Config::Get();
    return config.graphics.zoom_label_player.detail != FleetLabelDetail::Native
           || config.graphics.zoom_label_non_player.detail != FleetLabelDetail::Native;
  }

  // The level `lod` should show instead of `native`, or nothing when its fleet is not tracked or labels are native.
  std::optional<ZoomLevels> LevelFor(NavigationLOD* lod, ZoomLevels native) const
  {
    const auto* profile = Enabled() ? Profile(lod) : nullptr;
    if (!profile)
      return std::nullopt;
    // Expanded keeps Near where the game would show it, and lifts everything else to Middle.
    const auto expanded = native == ZoomLevels::Near ? ZoomLevels::Near : ZoomLevels::Middle;
    switch (profile->detail) {
      case FleetLabelDetail::Expanded:
        return expanded;
      case FleetLabelDetail::Compact:
        return ZoomLevels::Far;
      case FleetLabelDetail::Threshold:
        if (!zoom_known_)
          return native;
        return ExpandedAt(profile->zoom_threshold) ? expanded : ZoomLevels::Far;
      default:
        return native;
    }
  }

  void Track(NavigationFleetWidget* widget)
  {
    // Fresh pooled widgets are enabled before BindDataContext assigns their context; forcing an LOD before then
    // fires OnLODChanged callbacks that dereference the unbound fleet data.
    auto* lod = widget && widget->Context() ? widget->lod() : nullptr;
    if (!lod)
      return;
    widgets_.insert_or_assign(lod, widget);
    if (zoom_known_ && Enabled())
      lod->UpdateLOD(zoom_level_);
  }

  void Untrack(NavigationFleetWidget* widget)
  {
    auto* lod = widget ? widget->lod() : nullptr;
    if (!lod)
      return;
    widgets_.erase(lod);
    // Give a reused widget back to the game at the game's own level.
    if (widget->Context() && zoom_known_ && Enabled()) {
      lod->SetTargetLevel(zoom_level_);
      lod->UpdateLOD(zoom_level_);
      lod->SetTargetLevel(zoom_level_);
    }
  }

  // The camera `zoom` is about to show `depth`. Labels only apply in a solar system, and a new camera starts over.
  void OnDepth(NavigationZoom* zoom, NodeDepth depth)
  {
    if (!installed)
      return;
    if (depth == NodeDepth::SolarSystem) {
      Follow(zoom);
    } else if (camera_ == zoom) {
      Forget();
    }
  }

  // After the camera's own update: picks up the zoom level and re-applies labels when they change.
  void OnZoomed(NavigationZoom* zoom)
  {
    if (!installed || zoom->depth() != NodeDepth::SolarSystem)
      return;
    Follow(zoom);
    const bool was_known = zoom_known_;
    zoom_level_          = zoom->zoomLevel();
    zoom_known_          = true;
    if (UsesThreshold()) {
      normalized_zoom_ = zoom->NormalizedZoom();
      UpdateThreshold();
    } else if (!was_known) {
      ApplyAll();
    }
  }

private:
  const FleetLabelProfile* Profile(NavigationLOD* lod) const
  {
    const auto it     = widgets_.find(lod);
    auto*      widget = it == widgets_.end() ? nullptr : it->second;
    auto*      fleet  = widget ? widget->Context() : nullptr;
    if (!fleet)
      return nullptr;
    // A missing FleetType property reads as Nonexistent, which counts as non-player.
    const auto& config = Config::Get();
    return fleet->FleetType() == DeployedFleetType::Player ? &config.graphics.zoom_label_player
                                                           : &config.graphics.zoom_label_non_player;
  }

  static bool UsesThreshold()
  {
    const auto& config = Config::Get();
    return config.graphics.zoom_label_player.detail == FleetLabelDetail::Threshold
           || config.graphics.zoom_label_non_player.detail == FleetLabelDetail::Threshold;
  }

  // Labels expand when zoomed in at least as far as the threshold. 0 never expands, 1 always does.
  bool ExpandedAt(float threshold) const
  { return threshold > 0.0f && (threshold >= 1.0f || normalized_zoom_ <= threshold); }

  void Follow(NavigationZoom* zoom)
  {
    if (camera_ != zoom) {
      Forget();
      camera_ = zoom;
    }
  }

  void Forget()
  {
    camera_          = nullptr;
    zoom_known_      = false;
    zoom_level_      = ZoomLevels::None;
    normalized_zoom_ = 0.0f;
    threshold_known_ = false;
  }

  // UpdateLOD goes through the hook, which picks each label's level.
  void ApplyAll()
  {
    if (!Enabled())
      return;
    for (const auto& [lod, widget] : widgets_)
      if (widget->Context())
        lod->UpdateLOD(zoom_level_);
  }

  // Re-applies labels only when a threshold profile crosses its threshold, not on every zoom step.
  void UpdateThreshold()
  {
    const auto& config     = Config::Get();
    const bool  player     = config.graphics.zoom_label_player.detail == FleetLabelDetail::Threshold
                             && ExpandedAt(config.graphics.zoom_label_player.zoom_threshold);
    const bool  non_player = config.graphics.zoom_label_non_player.detail == FleetLabelDetail::Threshold
                             && ExpandedAt(config.graphics.zoom_label_non_player.zoom_threshold);
    if (threshold_known_ && player == player_expanded_ && non_player == non_player_expanded_)
      return;
    threshold_known_     = true;
    player_expanded_     = player;
    non_player_expanded_ = non_player;
    ApplyAll();
#ifdef _MODDBG
    spdlog::info("Fleet label detail threshold state updated at normalized zoom {:.4f}", normalized_zoom_);
#endif
  }

  std::unordered_map<NavigationLOD*, NavigationFleetWidget*> widgets_;
  // The system camera the zoom state below belongs to.
  NavigationZoom* camera_              = nullptr;
  bool            zoom_known_          = false;
  ZoomLevels      zoom_level_          = ZoomLevels::None;
  float           normalized_zoom_     = 0.0f;
  bool            threshold_known_     = false;
  bool            player_expanded_     = false;
  bool            non_player_expanded_ = false;
} fleet_labels;

// Entering a system jumps to default_system_zoom on the next camera update.
bool zoom_to_default = false;

// Camera distance as a share of the zoom range, scaled to the configured maximum (the unit the presets use).
float CurrentZoom(NavigationZoom* zoom)
{ return (zoom->Distance() - zoom->minimum()) / (zoom->maximum() - zoom->minimum()) * Config::Get().graphics.zoom; }

void StorePreset(const char* label, float& preset, NavigationZoom* zoom)
{
  const auto old = preset;
  preset         = CurrentZoom(zoom);
  spdlog::info("Changing {} from {} to {}", label, old, preset);
}

struct Preset {
  GameFunction store;
  GameFunction go;
  float Config::Graphics::* value;
  const char*               label;
};

constexpr Preset kPresets[] = {
    {GameFunction::SetZoomPreset1, GameFunction::ZoomPreset1, &Config::Graphics::system_zoom_preset_1,
     "System Preset 1"},
    {GameFunction::SetZoomPreset2, GameFunction::ZoomPreset2, &Config::Graphics::system_zoom_preset_2,
     "System Preset 2"},
    {GameFunction::SetZoomPreset3, GameFunction::ZoomPreset3, &Config::Graphics::system_zoom_preset_3,
     "System Preset 3"},
    {GameFunction::SetZoomPreset4, GameFunction::ZoomPreset4, &Config::Graphics::system_zoom_preset_4,
     "System Preset 4"},
    {GameFunction::SetZoomPreset5, GameFunction::ZoomPreset5, &Config::Graphics::system_zoom_preset_5,
     "System Preset 5"},
};

// Widens the system view's zoom range to the configured maximum and pushes the far clip plane out with it.
void ApplyZoomRange(NavigationZoom* zoom, float radius)
{
  const auto max_zoom = Config::Get().graphics.zoom;
  if (radius > 0.0f) {
    zoom->SetFarRatioSystemNormal(0.55f * max_zoom / radius);
    zoom->SetFarRatioSystemExtended(max_zoom / radius);
  }
  if (auto* camera = zoom->sceneCamera()) {
    camera->SetFarClipPlane(max_zoom * 3.75f);
    camera->SetClearFlags(2); // CameraClearFlags.SolidColor
    camera->SetBackgroundColor({0, 0, 0, 0});
  }
}

// The game resets the range now and then; reapply it around every update.
void KeepZoomRange(NavigationZoom* zoom)
{
  const auto max_zoom = Config::Get().graphics.zoom;
  if (zoom->depth() != NodeDepth::SolarSystem || max_zoom <= 0.0f)
    return;
  ApplyZoomRange(zoom, zoom->viewRadius());
  if (zoom->maximum() < max_zoom)
    zoom->SetMaximum(max_zoom);
  if (const auto total = zoom->maximum() - zoom->minimum(); total > 0.0f)
    zoom->SetZoomtotal(total);
}

// Zooms around the mouse pointer, either by `delta` or (with `absolute`) to a preset value.
void ZoomAtMouse(NavigationZoom* zoom, float value, bool absolute)
{
  static auto GetMousePosition =
      il2cpp::ResolveICall<void(vec3*)>("UnityEngine.Input::get_mousePosition_Injected(UnityEngine.Vector3&)");
  using MathUtils = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Client.Core", "MathUtils">;

  vec3 mouse;
  GetMousePosition(&mouse);
  zoom->SetZoomLocation(vec2{.x = mouse.x, .y = mouse.y});
  if (absolute) {
    zoom->SetDistance(zoom->minimum() + (zoom->maximum() - zoom->minimum()) * (value / Config::Get().graphics.zoom));
  } else {
    zoom->SetZoomDelta(value);
    zoom->SetLastZoomDelta(value);
  }
  auto* camera = zoom->sceneCamera();
  zoom->SetWorldPoint(camera ? MathUtils::Call<"GetMouseWorldPos", vec3>(camera, mouse) : vec3{});
  zoom->ZoomCameraAtWorldPoint();
}

// Keyboard zoom: Q/E zoom by keyboard_zoom_speed per second, presets jump, SHIFT-F1.. store the current zoom.
// Returns false when a preset was stored, which skips the game's update for that frame.
bool KeyboardZoom(NavigationZoom* zoom)
{
  static auto GetDeltaTime = il2cpp::ResolveICall<float()>("UnityEngine.Time::get_deltaTime()");
  auto&       config       = Config::Get();
  const bool  shortcuts    = config.control.hotkeys_enabled && !config.control.use_scopely_hotkeys;

  float target      = 0.0f;
  bool  from_preset = false;
  if (shortcuts) {
    for (const auto& preset : kPresets) {
      if (Shortcut::IsDown(preset.store)) {
        StorePreset(preset.label, config.graphics.*preset.value, zoom);
        return false;
      }
    }
    if (Shortcut::IsDown(GameFunction::SetZoomDefault)) {
      StorePreset("System Default", config.graphics.default_system_zoom, zoom);
      return false;
    }
    for (const auto& preset : kPresets) {
      if (Shortcut::IsDown(preset.go)) {
        target      = config.graphics.*preset.value;
        from_preset = true;
        break;
      }
    }
    if (config.control.hotkeys_extended) {
      if (Shortcut::IsDown(GameFunction::ZoomReset))
        target = config.graphics.default_system_zoom;
      else if (Shortcut::IsDown(GameFunction::ZoomMin))
        target = config.graphics.zoom;
      else if (Shortcut::IsDown(GameFunction::ZoomMax))
        target = 100.0f;
    }
  }
  if (target != 0.0f) {
    ZoomAtMouse(zoom, target, true);
  } else {
    const float step = config.graphics.keyboard_zoom_speed * GetDeltaTime();
    if (shortcuts && Shortcut::IsPressed(GameFunction::ZoomIn))
      ZoomAtMouse(zoom, step, false);
    else if (shortcuts && Shortcut::IsPressed(GameFunction::ZoomOut))
      ZoomAtMouse(zoom, -step, false);
  }

  // With use_presets_as_default, jumping to a preset also makes it the zoom used on entering a system.
  if (from_preset && target > 0.0f && config.graphics.use_presets_as_default)
    StorePreset("System Preset Default from Preset", config.graphics.default_system_zoom, zoom);
  return true;
}

void NavigationZoom_Update_Hook(auto original, NavigationZoom* _this)
{
  Key::Poll();
  fleet_labels.OnDepth(_this, _this->depth());
  KeepZoomRange(_this);

  // The jump on entering a system is not keyboard input, so it happens even while a text field has focus.
  bool run_game_update = true;
  if (std::exchange(zoom_to_default, false))
    ZoomAtMouse(_this, Config::Get().graphics.default_system_zoom, true);
  else if (!Key::IsInputFocused())
    run_game_update = KeyboardZoom(_this);
  if (!run_game_update)
    return;

  original(_this);
  KeepZoomRange(_this);
  fleet_labels.OnZoomed(_this);
}

// Entering a system: widen the range before and after the game sets it up, and zoom to the default next update.
template <typename Original, typename... Args>
void EnterDepth(Original original, NavigationZoom* zoom, NodeDepth depth, float radius, Args... args)
{
  fleet_labels.OnDepth(zoom, depth);
  if (depth != NodeDepth::SolarSystem)
    return original(zoom, args...);
  ApplyZoomRange(zoom, radius);
  original(zoom, args...);
  ApplyZoomRange(zoom, 0.0f);
  zoom_to_default = true;
}

void NavigationZoom_SetViewParameters_Hook(auto original, NavigationZoom* _this, float radius, NodeDepth depth)
{ EnterDepth(original, _this, depth, radius, radius, depth); }

void NavigationZoom_SetDepth_Hook(auto original, NavigationZoom* _this, NodeDepth depth)
{ EnterDepth(original, _this, depth, _this->viewRadius(), depth); }

void NavigationLOD_UpdateLOD_Hook(auto original, NavigationLOD* _this, ZoomLevels level)
{
  const auto effective = fleet_labels.LevelFor(_this, level);
  if (!effective)
    return original(_this, level);
  // The target level is set on both sides of the update so the game's own bookkeeping agrees with ours.
  _this->SetTargetLevel(*effective);
  original(_this, *effective);
  _this->SetTargetLevel(*effective);
}

void NavigationFleetWidget_OnEnable_Hook(auto original, NavigationFleetWidget* _this)
{
  original(_this);
  fleet_labels.Track(_this);
}

void NavigationFleetWidget_OnDidBindContext_Hook(auto original, NavigationFleetWidget* _this)
{
  original(_this);
  fleet_labels.Track(_this);
}

void NavigationFleetWidget_OnDisable_Hook(auto original, NavigationFleetWidget* _this)
{
  fleet_labels.Untrack(_this);
  original(_this);
}

void NavigationFleetWidget_OnAboutToReleaseContext_Hook(auto original, NavigationFleetWidget* _this)
{
  fleet_labels.Untrack(_this);
  original(_this);
}

// Planet flat renderables (the 2D planet sprites) are scaled by fr_scale. The game reuses them and asks for them on
// every zoom, so each transform remembers the size it was scaled to and is left alone while it still has it.
// Planets differ in base size, so this is per transform: one shared value made two planets rescale each other.
void ScaleFlatRenderable(il2cpp::Object* renderable)
{
  // A system has a handful of planets; past this, entries from earlier systems are dropped.
  constexpr size_t                             kRemembered = 256;
  static std::unordered_map<Transform*, float> scaled;
  const float                                  factor = Config::Get().graphics.fr_scale;
  // FlatRenderable is a Component; transform resolves on its runtime class.
  auto* transform =
      renderable && factor > 0.0f && factor != 1.0f ? renderable->Get<"transform", Transform*>() : nullptr;
  if (!transform)
    return;
  const auto scale = transform->localScale();
  if (const auto it = scaled.find(transform); it != scaled.end() && std::fabs(scale.x - it->second) < 0.1f)
    return;
  if (scaled.size() >= kRemembered)
    scaled.clear();
  transform->SetLocalScale({scale.x * factor, scale.y * factor, scale.z * factor});
  scaled.insert_or_assign(transform, scale.x * factor);
}

void* PlanetViewUtils_get_FlatRenderable_Hook(auto original, PlanetViewUtils* _this)
{
  auto* renderable = original(_this);
  ScaleFlatRenderable(static_cast<il2cpp::Object*>(renderable));
  return renderable;
}

// The game mostly reads the renderable field directly; asking through the getter routes it via the hook above.
void PlanetViewUtils_CameraZoomedEventHandler_Hook(auto original, PlanetViewUtils* _this, float distance,
                                                   float normalized)
{
  original(_this, distance, normalized);
  if (_this)
    _this->GetFlatRenderable();
}
} // namespace

void InstallZoomHooks()
{
  auto update = NavigationZoom::Class::Method("Update");

  {
    // Installed as a set, whether or not zoom_label_* is set, so labels can be switched at runtime. Missing fields
    // and properties (_lod, Context, FleetType) read as null or zero, which the hooks treat as native labels, so
    // only the methods gate installation.
    auto update_lod = NavigationLOD::Class::Method("UpdateLOD");
    auto on_enable  = NavigationFleetWidget::Class::Method("OnEnable");
    auto on_disable = NavigationFleetWidget::Class::Method("OnDisable");
    auto on_bind    = NavigationFleetWidget::Class::Method("OnDidBindContext");
    auto on_release = NavigationFleetWidget::Class::Method("OnAboutToReleaseContext");
    if (update && update_lod && on_enable && on_disable && on_bind && on_release) {
      fleet_labels.installed = true;
      SPUD_STATIC_DETOUR(update_lod.Pointer(), NavigationLOD_UpdateLOD_Hook);
      SPUD_STATIC_DETOUR(on_enable.Pointer(), NavigationFleetWidget_OnEnable_Hook);
      SPUD_STATIC_DETOUR(on_disable.Pointer(), NavigationFleetWidget_OnDisable_Hook);
      SPUD_STATIC_DETOUR(on_bind.Pointer(), NavigationFleetWidget_OnDidBindContext_Hook);
      SPUD_STATIC_DETOUR(on_release.Pointer(), NavigationFleetWidget_OnAboutToReleaseContext_Hook);
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
  // TODO: SetDepth has only ever been detoured on Windows and the reason was not recorded; check its prologue
  // against SPUD's overwrite size before enabling it on macOS.
#if _WIN32
  if (auto method = NavigationZoom::Class::Method("SetDepth"))
    SPUD_STATIC_DETOUR(method.Pointer(), NavigationZoom_SetDepth_Hook);
#endif
  if (auto method = NavigationZoom::Class::Method("SetViewParameters"))
    SPUD_STATIC_DETOUR(method.Pointer(), NavigationZoom_SetViewParameters_Hook);
}

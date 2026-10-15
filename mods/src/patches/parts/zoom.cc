// The camera in system view and at the station: the extended zoom range, keyboard zoom and its presets, planet
// sprite scaling, keyboard panning, pan momentum and the target camera lock, keyboard rotation around the station, and
// held touches that keep panning. Fleet label detail, which follows the zoom, is in fleet_labels.cc.

#include "config.h"
#include "patches/patches.h"
#include "patches/shortcuts.h"

#include "prime/Hub.h"
#include "prime/navigation.h"
#include "prime/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/input.h>
#include <unity/ui.h>

#include <cmath>
#include <optional>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace
{
// --- Zoom ---

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
  Action store;
  Action go;
  float Config::Graphics::* value;
  const char*               label;
};

constexpr Preset kPresets[] = {
    {Action::kSetZoomPreset1, Action::kZoomPreset1, &Config::Graphics::system_zoom_preset_1, "System Preset 1"},
    {Action::kSetZoomPreset2, Action::kZoomPreset2, &Config::Graphics::system_zoom_preset_2, "System Preset 2"},
    {Action::kSetZoomPreset3, Action::kZoomPreset3, &Config::Graphics::system_zoom_preset_3, "System Preset 3"},
    {Action::kSetZoomPreset4, Action::kZoomPreset4, &Config::Graphics::system_zoom_preset_4, "System Preset 4"},
    {Action::kSetZoomPreset5, Action::kZoomPreset5, &Config::Graphics::system_zoom_preset_5, "System Preset 5"},
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
    camera->SetClearFlags(unity::Camera::ClearFlags::SolidColor);
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
  const auto mouse = unity::Input::mousePosition();
  zoom->SetZoomLocation({mouse.x, mouse.y});
  if (absolute) {
    zoom->SetDistance(zoom->minimum() + (zoom->maximum() - zoom->minimum()) * (value / Config::Get().graphics.zoom));
  } else {
    zoom->SetZoomDelta(value);
    zoom->SetLastZoomDelta(value);
  }
  auto* camera = zoom->sceneCamera();
  zoom->SetWorldPoint(camera ? MathUtils::GetMouseWorldPos(camera, mouse) : unity::Vector3{});
  zoom->ZoomCameraAtWorldPoint();
}

// Keyboard zoom: Q/E zoom by keyboard_zoom_speed per second, presets jump, SHIFT-F1.. store the current zoom.
// Returns false when a preset was stored, which skips the game's update for that frame.
bool KeyboardZoom(NavigationZoom* zoom)
{
  auto&      config    = Config::Get();
  const bool shortcuts = config.control.hotkeys_enabled && !config.control.use_scopely_hotkeys;

  float target      = 0.0f;
  bool  from_preset = false;
  if (shortcuts) {
    for (const auto& preset : kPresets) {
      if (Shortcut::IsDown(preset.store)) {
        StorePreset(preset.label, config.graphics.*preset.value, zoom);
        return false;
      }
    }
    if (Shortcut::IsDown(Action::kSetZoomDefault)) {
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
      if (Shortcut::IsDown(Action::kZoomReset))
        target = config.graphics.default_system_zoom;
      else if (Shortcut::IsDown(Action::kZoomMin))
        target = config.graphics.zoom;
      else if (Shortcut::IsDown(Action::kZoomMax))
        target = 100.0f;
    }
  }
  if (target != 0.0f) {
    ZoomAtMouse(zoom, target, true);
  } else {
    const float step = config.graphics.keyboard_zoom_speed * unity::Time::deltaTime();
    if (shortcuts && Shortcut::IsPressed(Action::kZoomIn))
      ZoomAtMouse(zoom, step, false);
    else if (shortcuts && Shortcut::IsPressed(Action::kZoomOut))
      ZoomAtMouse(zoom, -step, false);
  }

  // With use_presets_as_default, jumping to a preset also makes it the zoom used on entering a system.
  if (from_preset && target > 0.0f && config.graphics.use_presets_as_default)
    StorePreset("System Preset Default from Preset", config.graphics.default_system_zoom, zoom);
  return true;
}

void NavigationZoom_Update_Hook(auto original, NavigationZoom* _this)
{
  unity::Key::Poll();
  FleetLabelsFollowDepth(_this, _this->depth());
  KeepZoomRange(_this);

  // The jump on entering a system is not keyboard input, so it happens even while a text field has focus.
  bool run_game_update = true;
  if (std::exchange(zoom_to_default, false))
    ZoomAtMouse(_this, Config::Get().graphics.default_system_zoom, true);
  else if (!unity::Key::IsInputFocused())
    run_game_update = KeyboardZoom(_this);
  if (!run_game_update)
    return;

  original(_this);
  KeepZoomRange(_this);
  FleetLabelsAfterZoom(_this);
}

// Entering a system: widen the range before and after the game sets it up, and zoom to the default next update.
template <typename Original, typename... Args>
void EnterDepth(Original original, NavigationZoom* zoom, NodeDepth depth, float radius, Args... args)
{
  FleetLabelsFollowDepth(zoom, depth);
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

// Planet flat renderables (the 2D planet sprites) are scaled by fr_scale. The game reuses them and asks for them on
// every zoom, so each transform remembers the size it was scaled to and is left alone while it still has it.
// Planets differ in base size, so this is per transform: one shared value made two planets rescale each other.
void ScaleFlatRenderable(unity::Component* renderable)
{
  // A system has a handful of planets; past this, entries from earlier systems are dropped.
  constexpr size_t                                    kRemembered = 256;
  static std::unordered_map<unity::Transform*, float> scaled;
  const float                                         factor = Config::Get().graphics.fr_scale;
  auto* transform = renderable && factor > 0.0f && factor != 1.0f ? renderable->transform() : nullptr;
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

unity::Component* PlanetViewUtils_get_FlatRenderable_Hook(auto original, PlanetViewUtils* _this)
{
  auto* renderable = original(_this);
  ScaleFlatRenderable(renderable);
  return renderable;
}

// The game mostly reads the renderable field directly; asking through the getter routes it via the hook above.
void PlanetViewUtils_CameraZoomedEventHandler_Hook(auto original, PlanetViewUtils* _this, float distance,
                                                   float normalized)
{
  original(_this, distance, normalized);
  if (_this)
    _this->FlatRenderable();
}

// --- Panning and rotation ---

// Whether the mod, not the game, moves the camera with the Move* shortcuts.
bool ModMovesCamera()
{
  const auto& config = Config::Get();
  return config.control.hotkeys_enabled && !config.control.use_scopely_hotkeys && !config.ui.disable_move_keys;
}

bool PointerHeld()
{ return unity::Input::GetMouseButton(0) || unity::Input::touchCount() > 0; }

// The game's own tutorial and modal/text-input gates for keyboard panning.
bool KeyboardPanAllowed()
{
  auto* manager = Hub::Tutorial();
  return manager && manager->IsSceneInputAllowed() && ShortcutsManager::CanUseShortcuts()
         && !unity::Key::IsInputFocused();
}

// Replaces the game's LateUpdate: the keyboard source is the mod's shortcuts, and the pan keeps gliding after the
// drag or key is released. Each frame moves the camera once: by the keys while one is held, otherwise by the
// glide.
bool NavigationPan_LateUpdate_Hook(auto original, NavigationPan* _this)
{
  unity::Key::Poll();
  const auto& config                     = Config::Get();
  const bool  disable_target_camera_lock = config.graphics.disable_target_camera_lock;
  // Dropping the tracked target before either pan path runs lets the player pan away from it.
  if (disable_target_camera_lock && _this->trackingPOI())
    _this->SetTrackingPOI(nullptr);
  // An arrow held for a CTRL-arrow style shortcut must not pan as well, natively or through the mod.
  const bool arrow_owned = Shortcut::Owns(unity::KeyCode::LeftArrow) || Shortcut::Owns(unity::KeyCode::RightArrow)
                           || Shortcut::Owns(unity::KeyCode::UpArrow) || Shortcut::Owns(unity::KeyCode::DownArrow);
  const bool keyboard    = config.control.hotkeys_enabled && !config.ui.disable_move_keys && !arrow_owned;

  // With the game's own hotkeys, its LateUpdate pans and glides by itself. The mod still applies its momentum falloff
  // to that glide, without moving the camera a second time.
  const bool native = keyboard && (config.control.use_scopely_hotkeys || !config.patches.hotkeyhooks);
  if (native)
    original(_this);

  auto       d       = _this->lastDelta();
  const bool blocked = _this->BlockPan() || (_this->trackingPOI() && !disable_target_camera_lock);
  // The native recognizer owns held-drag movement, including when the legacy Input API does not report the pointer.
  // Never replay its delta as momentum.
  const bool pointer = PointerHeld() || _this->panningStarted();
  bool       by_keys = false;
  if (keyboard && !native && !blocked && !pointer && KeyboardPanAllowed()) {
    // Opposite bindings cancel; normalize diagonals like the native 2D composite.
    const float x = float(Shortcut::IsPressed(Action::kMoveRight)) - float(Shortcut::IsPressed(Action::kMoveLeft));
    const float y = float(Shortcut::IsPressed(Action::kMoveUp)) - float(Shortcut::IsPressed(Action::kMoveDown));
    if (const float length = std::sqrt(x * x + y * y); length > 0.0f) {
      d.x     = -x / length * _this->keyboardDeltaScaler();
      d.y     = -y / length * _this->keyboardDeltaScaler();
      by_keys = true;
      _this->MoveCamera(unity::Vector2{d.x * _this->deltaScaler(), d.y * _this->deltaScaler()}, true);
    }
  }

  if (blocked) {
    d = {};
  } else if (!by_keys && !pointer) {
    const auto falloff = config.graphics.system_pan_momentum_falloff;
    d.x *= falloff;
    d.y *= falloff;
    if (!native)
      _this->MoveCamera(unity::Vector2{d.x, d.y}, true);
  }
  _this->SetLastDelta(d);
  _this->SetFarMagRadiusRatioSystemExtended(_this->farMagRadiusRatioSystemNormal());
  return true;
}

// Move left and right rotate the camera around the station.
void OrbitFrameProvider_UpdateInputData_Hook(auto original, OrbitFrameProvider* _this, unity::Camera* primary_camera)
{
  original(_this, primary_camera);
  unity::Key::Poll();

  auto* sections = Hub::Sections();
  if (!ModMovesCamera() || !sections || sections->CurrentSection() != SectionID::Starbase_Exterior
      || unity::Key::IsInputFocused())
    return;

  constexpr float kDegreesPerSecond = 45.0f;
  const float     step              = kDegreesPerSecond * unity::Time::deltaTime();
  if (Shortcut::IsPressed(Action::kMoveLeft))
    _this->SetRotationAngleDelta(_this->RotationAngleDelta() - step);
  if (Shortcut::IsPressed(Action::kMoveRight))
    _this->SetRotationAngleDelta(_this->RotationAngleDelta() + step);
}

// A held touch reports Stationary, which the game's pan ignores; report it as moving so drags keep panning.
TKTouch* TKTouch_populateWithPosition_Hook(auto original, TKTouch* _this, uintptr_t pos, unity::TouchPhase phase)
{
  auto* touch = original(_this, pos, phase);
  if (touch && touch->phase() == unity::TouchPhase::Stationary)
    touch->SetPhase(unity::TouchPhase::Moved);
  return touch;
}

void InstallZoom()
{
  IL2CPP_HOOK(PlanetViewUtils::Class::Method("CameraZoomedEventHandler"),
              PlanetViewUtils_CameraZoomedEventHandler_Hook);
  IL2CPP_HOOK(PlanetViewUtils::Class::Method("get_FlatRenderable"), PlanetViewUtils_get_FlatRenderable_Hook);
  IL2CPP_HOOK(NavigationZoom::Class::Method("Update"), NavigationZoom_Update_Hook);
  // TODO: SetDepth has only ever been detoured on Windows and the reason was not recorded; check its prologue
  // against SPUD's overwrite size before enabling it on macOS.
#if _WIN32
  IL2CPP_HOOK(NavigationZoom::Class::Method("SetDepth"), NavigationZoom_SetDepth_Hook);
#endif
  IL2CPP_HOOK(NavigationZoom::Class::Method("SetViewParameters"), NavigationZoom_SetViewParameters_Hook);
}

void InstallPan()
{
  IL2CPP_HOOK(OrbitFrameProvider::Class::Method("UpdateInputData"), OrbitFrameProvider_UpdateInputData_Hook);
  IL2CPP_HOOK(TKTouch::Class::Method("populateWithPosition"), TKTouch_populateWithPosition_Hook);
  IL2CPP_HOOK(NavigationPan::Class::Method("LateUpdate"), NavigationPan_LateUpdate_Hook);
}
} // namespace

const Feature kZoom{"Zoom", &Config::Patches::zoomhooks, InstallZoom};
const Feature kPan{"Pan", &Config::Patches::panhooks, InstallPan};

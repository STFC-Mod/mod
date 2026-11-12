// The camera in system view and at the station: the extended zoom range, keyboard zoom and its presets, planet
// sprite scaling, keyboard panning, pan momentum and the target camera lock, keyboard rotation around the station,
// held touches that keep panning, and [graphics] interior_zoom_scale inside the station. Fleet label detail, which
// follows the zoom, is in fleet_labels.cc; Haven's camera is in haven.cc.

#include "config.h"
#include "features/features.h"
#include "shortcuts.h"

#include "game/app.h"
#include "game/camera.h"
#include "game/navigation.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/input.h>
#include <unity/ui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace
{
// --- Zoom ---

// Entering a system jumps to default_system_zoom on the next camera update.
bool zoom_to_default = false;

// Presets and default_system_zoom are in zoom units: the camera distance's place in [minimum, maximum] mapped onto
// [0, graphics.zoom], so they keep their meaning when the game moves the range. ZoomAtMouse(absolute) is the inverse.
float CurrentZoom(NavigationZoom* zoom)
{ return (zoom->Distance() - zoom->minimum()) / (zoom->maximum() - zoom->minimum()) * Config::Get().graphics.zoom; }

void StorePreset(const char* label, float& preset, NavigationZoom* zoom)
{
  const auto old = preset;
  preset         = CurrentZoom(zoom);
  spdlog::info("[Zoom] {} set from {} to {}", label, old, preset);
}

struct ZoomPreset {
  Action store;
  Action go;
  float Config::Graphics::* value;
  const char*               label;
};

constexpr ZoomPreset kZoomPresets[] = {
    {Action::kSetZoomPreset1, Action::kZoomPreset1, &Config::Graphics::system_zoom_preset_1, "System Preset 1"},
    {Action::kSetZoomPreset2, Action::kZoomPreset2, &Config::Graphics::system_zoom_preset_2, "System Preset 2"},
    {Action::kSetZoomPreset3, Action::kZoomPreset3, &Config::Graphics::system_zoom_preset_3, "System Preset 3"},
    {Action::kSetZoomPreset4, Action::kZoomPreset4, &Config::Graphics::system_zoom_preset_4, "System Preset 4"},
    {Action::kSetZoomPreset5, Action::kZoomPreset5, &Config::Graphics::system_zoom_preset_5, "System Preset 5"},
};

// Widens the system view's zoom range to the configured maximum and pushes the far clip plane out with it. A radius of
// 0 leaves the far ratios alone and only reapplies the camera settings.
void ApplyZoomRange(NavigationZoom* zoom, float radius)
{
  const auto max_zoom = Config::Get().graphics.zoom;
  // The far ratios are multiples of the system's view radius, so max_zoom / radius puts the extended stop at max_zoom.
  // The normal stop at 0.55 of that is carried over from the original mod.
  if (radius > 0.0f) {
    zoom->SetFarRatioSystemNormal(0.55f * max_zoom / radius);
    zoom->SetFarRatioSystemExtended(max_zoom / radius);
  }
  // At max_zoom the camera still has to reach the far side of the system and its backdrop, hence the 3.75x clip.
  // Clearing to black instead of the skybox hides the void past the backdrop's edge.
  if (auto* camera = zoom->sceneCamera()) {
    camera->SetFarClipPlane(max_zoom * 3.75f);
    camera->SetClearFlags(unity::Camera::ClearFlags::SolidColor);
    camera->SetBackgroundColor({0, 0, 0, 0});
  }
}

// The game resets the range now and then; reapply it around every update. _zoomtotal caches maximum - minimum and
// must follow the widened maximum.
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

// Zooms around the mouse pointer, either by a step of `value` or, with `absolute`, to `value` in zoom units.
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
// ZoomMin goes all the way out (graphics.zoom) and ZoomMax close in. Returns false when a preset was stored, which
// skips the game's update for that frame.
bool KeyboardZoom(NavigationZoom* zoom)
{
  auto& config = Config::Get();
  if (!config.control.hotkeys_enabled || config.control.use_scopely_hotkeys)
    return true;

  for (const auto& preset : kZoomPresets)
    if (Shortcut::IsDown(preset.store)) {
      StorePreset(preset.label, config.graphics.*preset.value, zoom);
      return false;
    }
  if (Shortcut::IsDown(Action::kSetZoomDefault)) {
    StorePreset("System Default", config.graphics.default_system_zoom, zoom);
    return false;
  }

  // An extended shortcut held with a preset wins the jump, but the preset still counts for use_presets_as_default.
  const auto* preset = std::ranges::find_if(kZoomPresets, [](const ZoomPreset& p) { return Shortcut::IsDown(p.go); });
  const bool  from_preset = preset != std::ranges::end(kZoomPresets);
  float       target      = from_preset ? config.graphics.*preset->value : 0.0f;
  if (config.control.hotkeys_extended) {
    if (Shortcut::IsDown(Action::kZoomReset))
      target = config.graphics.default_system_zoom;
    else if (Shortcut::IsDown(Action::kZoomMin))
      target = config.graphics.zoom;
    else if (Shortcut::IsDown(Action::kZoomMax))
      target = 100.0f;
  }

  const float step = config.graphics.keyboard_zoom_speed * unity::Time::deltaTime();
  if (target != 0.0f)
    ZoomAtMouse(zoom, target, true);
  else if (Shortcut::IsPressed(Action::kZoomIn))
    ZoomAtMouse(zoom, step, false);
  else if (Shortcut::IsPressed(Action::kZoomOut))
    ZoomAtMouse(zoom, -step, false);

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
// every zoom, so each transform remembers the size it was scaled to and is left alone while it has exactly that size.
// Planets differ in base size, so this is per transform: one shared value made two planets rescale each other. A
// backdrop that was just created or reloaded has its authored scale again, which never equals the remembered one
// while fr_scale is not 1, so it is scaled again.
void ScaleFlatRenderable(unity::Component* renderable)
{
  // A system has a handful of planets; past this, entries from earlier systems are dropped.
  constexpr size_t                                    kRemembered = 256;
  static std::unordered_map<unity::Transform*, float> scaled;
  const float                                         factor = Config::Get().graphics.fr_scale;
  // Written so a NaN fr_scale, which the reader accepts, skips scaling too.
  if (!renderable || !(factor > 0.0f) || factor == 1.0f)
    return;
  auto* transform = renderable->transform();
  if (!transform)
    return;
  const auto scale = transform->localScale();
  if (const auto it = scaled.find(transform); it != scaled.end() && scale.x == it->second)
    return;
  if (scaled.size() >= kRemembered)
    scaled.clear();
  const auto new_scale = scale * factor;
  transform->SetLocalScale(new_scale);
  scaled.insert_or_assign(transform, new_scale.x);
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
  _this->FlatRenderable();
}

void PlanetViewUtils_CreateBackdrop_Hook(auto original, PlanetViewUtils* _this, Il2CppObject* backdrop,
                                         Il2CppObject* resource, bool using_default)
{
  original(_this, backdrop, resource, using_default);
  if (resource)
    _this->FlatRenderable();
}

// A cached variation backdrop reloads its authored scale without calling CreateBackdrop.
void PlanetViewUtils_Populate_Hook(auto original, PlanetViewUtils* _this, Il2CppObject* system, int64_t node_id,
                                   NodeDepth depth)
{
  original(_this, system, node_id, depth);
  if (depth == NodeDepth::SolarSystem)
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
  constexpr std::array kArrows{unity::KeyCode::LeftArrow, unity::KeyCode::RightArrow, unity::KeyCode::UpArrow,
                               unity::KeyCode::DownArrow};
  const bool           arrow_owned = std::ranges::any_of(kArrows, Shortcut::Owns);
  const bool           keyboard    = config.control.hotkeys_enabled && !config.ui.disable_move_keys && !arrow_owned;

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
    // Opposite bindings cancel; normalize diagonals like the native 2D composite. MoveCamera takes a drag, and
    // dragging left moves the view right, hence the negation.
    const float x = float(Shortcut::IsPressed(Action::kMoveRight)) - float(Shortcut::IsPressed(Action::kMoveLeft));
    const float y = float(Shortcut::IsPressed(Action::kMoveUp)) - float(Shortcut::IsPressed(Action::kMoveDown));
    if (const float length = std::sqrt(x * x + y * y); length > 0.0f) {
      const float speed = _this->keyboardDeltaScaler();
      d                 = {-x / length * speed, -y / length * speed};
      by_keys           = true;
      const float scale = _this->deltaScaler();
      _this->MoveCamera(d * scale, true);
    }
  }

  if (blocked) {
    d = {};
  } else if (!by_keys && !pointer) {
    // Glide: the last drag or key delta shrinks by the falloff every frame.
    const auto falloff = config.graphics.system_pan_momentum_falloff;
    d                  = d * falloff;
    if (!native)
      _this->MoveCamera(d, true);
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
    _this->SetRotationAngleDelta(_this->rotationAngleDelta() - step);
  if (Shortcut::IsPressed(Action::kMoveRight))
    _this->SetRotationAngleDelta(_this->rotationAngleDelta() + step);
}

// A held touch reports Stationary, which the game's pan ignores; report it as moving so drags keep panning.
TKTouch* TKTouch_populateWithPosition_Hook(auto original, TKTouch* _this, uintptr_t pos, unity::TouchPhase phase)
{
  auto* touch = original(_this, pos, phase);
  if (touch && touch->phase() == unity::TouchPhase::Stationary)
    touch->SetPhase(unity::TouchPhase::Moved);
  return touch;
}

// --- Station interior zoom ---
//
// The interior view blends from a fixed close frame out to an orbit pinned at one radius, so its far stop is that
// orbit's radius. Widening the orbit's [min, max] lets the zoom go further. The same radius bounds the exterior station
// camera, so it is widened only inside the interior and put back everywhere else.

// Each radius's own range, as found before widening. Keyed by the radius constraint, not the orbit: the orbit is
// recreated across transitions while its radius persists, so an orbit key left the exterior at a widened radius it
// never restored. The interior has one radius, so this stays tiny; keys are compared only, never dereferenced.
std::unordered_map<FloatConstraint*, std::pair<float, float>> native_interior_radius;

void OrbitFrameProvider_UpdateConstrains_Hook(auto original, OrbitFrameProvider* _this, unity::Camera* primary_camera)
{
  auto* constraint = _this->constraint();
  if (auto* radius = constraint ? constraint->radius() : nullptr) {
    auto*       sections = Hub::Sections();
    const bool  interior = sections && sections->CurrentSection() == SectionID::Starbase_Interior;
    const float scale    = Config::Get().graphics.interior_zoom_scale;
    if (interior && std::isfinite(scale) && scale > 1.0f) {
      // Recorded on first sight and always scaled from there, so doing this every frame cannot compound.
      const auto [low, high] = native_interior_radius.try_emplace(radius, radius->min(), radius->max()).first->second;
      radius->SetMin(low * scale);
      radius->SetMax(high * scale);
    } else if (const auto native = native_interior_radius.find(radius); native != native_interior_radius.end()) {
      // Left wider, the exterior view stays zoomed out and its station labels drop out of range.
      const auto [low, high] = native->second;
      radius->SetMin(low);
      radius->SetMax(high);
    }
  }
  // The game clamps the camera against the widened or restored range.
  original(_this, primary_camera);
}

bool InstallZoom()
{
  il2cpp::HookSet hooks("Zoom");
  IL2CPP_STAGE(hooks, PlanetViewUtils::Class::Method("Populate", 3), PlanetViewUtils_Populate_Hook);
  IL2CPP_STAGE(hooks,
               PlanetViewUtils::Class::Method(
                   "CreateBackdrop", {"Digit.PrimeServer.Models.ITreeNode", "UnityEngine.Object", "System.Boolean"}),
               PlanetViewUtils_CreateBackdrop_Hook);
  IL2CPP_STAGE(hooks,
               PlanetViewUtils::Class::Method("CreateBackdrop", {"Digit.PrimeServer.Models.ITreeNode",
                                                                 "SystemVariationConfigCollection", "System.Boolean"}),
               PlanetViewUtils_CreateBackdrop_Hook);
  IL2CPP_STAGE(hooks, PlanetViewUtils::Class::Method("CameraZoomedEventHandler"),
               PlanetViewUtils_CameraZoomedEventHandler_Hook);
  IL2CPP_STAGE(hooks, PlanetViewUtils::Class::Method("get_FlatRenderable"), PlanetViewUtils_get_FlatRenderable_Hook);
  IL2CPP_STAGE(hooks, NavigationZoom::Class::Method("Update"), NavigationZoom_Update_Hook);
  // TODO: SetDepth has only ever been detoured on Windows and the reason was not recorded; check its prologue
  // against SPUD's overwrite size before enabling it on macOS.
#if _WIN32
  IL2CPP_STAGE(hooks, NavigationZoom::Class::Method("SetDepth"), NavigationZoom_SetDepth_Hook);
#endif
  IL2CPP_STAGE(hooks, NavigationZoom::Class::Method("SetViewParameters"), NavigationZoom_SetViewParameters_Hook);
  return hooks.Commit();
}

bool InstallPan()
{
  il2cpp::HookSet hooks("Pan");
  IL2CPP_STAGE(hooks, OrbitFrameProvider::Class::Method("UpdateInputData"), OrbitFrameProvider_UpdateInputData_Hook);
  IL2CPP_STAGE(hooks, TKTouch::Class::Method("populateWithPosition"), TKTouch_populateWithPosition_Hook);
  IL2CPP_STAGE(hooks, NavigationPan::Class::Method("LateUpdate"), NavigationPan_LateUpdate_Hook);
  return hooks.Commit();
}

bool InstallInteriorZoom()
{
  il2cpp::HookSet hooks("InteriorZoom");
  IL2CPP_STAGE(hooks, OrbitFrameProvider::Class::Method<void(unity::Camera*)>("UpdateConstrains"),
               OrbitFrameProvider_UpdateConstrains_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kZoom{"Zoom", &Config::Patches::zoomhooks, InstallZoom};
const Feature kPan{"Pan", &Config::Patches::panhooks, InstallPan};
const Feature kInteriorZoom{"InteriorZoom", &Config::Patches::interiorzoomhooks, InstallInteriorZoom};

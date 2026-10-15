// Camera movement: keyboard panning and pan momentum in system view, keyboard rotation around the station, and
// held touches that keep panning.

#include "config.h"
#include "patches/input.h"
#include "patches/patches.h"
#include "patches/shortcuts.h"

#include <prime/Hub.h>
#include <prime/navigation.h>
#include <prime/ui.h>
#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spud/detour.h>

#include <cmath>

namespace
{
struct TutorialManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Tutorial", "TutorialManager">;

  bool IsSceneInputAllowed()
  { return Call<"IsSceneInputAllowed", bool>(); }
};

// Whether the mod, not the game, moves the camera with the Move* shortcuts.
bool ModMovesCamera()
{
  const auto& config = Config::Get();
  return config.control.hotkeys_enabled && !config.control.use_scopely_hotkeys && !config.ui.disable_move_keys;
}

bool PointerHeld()
{
  static auto GetMouseButton = il2cpp::ResolveICall<bool(int)>("UnityEngine.Input::GetMouseButton(System.Int32)");
  static auto GetTouchCount  = il2cpp::ResolveICall<int()>("UnityEngine.Input::get_touchCount()");
  return GetMouseButton(0) || GetTouchCount() > 0;
}

// The game's own tutorial and modal/text-input gates for keyboard panning.
bool KeyboardPanAllowed()
{
  auto* manager = Hub::Class::Get<"TutorialManager", TutorialManager*>();
  return manager && manager->IsSceneInputAllowed() && ShortcutsManager::CanUseShortcuts() && !Key::IsInputFocused();
}

// Replaces the game's LateUpdate: the keyboard source is the mod's shortcuts, and the pan keeps gliding after the
// drag or key is released. Each frame moves the camera once: by the keys while one is held, otherwise by the
// glide.
bool NavigationPan_LateUpdate_Hook(auto original, NavigationPan* _this)
{
  Key::Poll();
  const auto& config                     = Config::Get();
  const bool  disable_target_camera_lock = config.graphics.disable_target_camera_lock;
  // Dropping the tracked target before either pan path runs lets the player pan away from it.
  if (disable_target_camera_lock && _this->trackingPOI())
    _this->SetTrackingPOI(nullptr);
  // An arrow held for a CTRL-arrow style shortcut must not pan as well, natively or through the mod.
  const bool arrow_owned = Shortcut::Owns(KeyCode::LeftArrow) || Shortcut::Owns(KeyCode::RightArrow)
                           || Shortcut::Owns(KeyCode::UpArrow) || Shortcut::Owns(KeyCode::DownArrow);
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
    const float x =
        float(Shortcut::IsPressed(GameFunction::MoveRight)) - float(Shortcut::IsPressed(GameFunction::MoveLeft));
    const float y =
        float(Shortcut::IsPressed(GameFunction::MoveUp)) - float(Shortcut::IsPressed(GameFunction::MoveDown));
    if (const float length = std::sqrt(x * x + y * y); length > 0.0f) {
      d.x     = -x / length * _this->keyboardDeltaScaler();
      d.y     = -y / length * _this->keyboardDeltaScaler();
      by_keys = true;
      _this->MoveCamera(vec2{d.x * _this->deltaScaler(), d.y * _this->deltaScaler()}, true);
    }
  }

  if (blocked) {
    d = {};
  } else if (!by_keys && !pointer) {
    const auto falloff = config.graphics.system_pan_momentum_falloff;
    d.x *= falloff;
    d.y *= falloff;
    if (!native)
      _this->MoveCamera(vec2{d.x, d.y}, true);
  }
  _this->SetLastDelta(d);
  _this->SetFarMagRadiusRatioSystemExtended(_this->farMagRadiusRatioSystemNormal());
  return true;
}

// Move left and right rotate the camera around the station.
void OrbitFrameProvider_UpdateInputData_Hook(auto original, OrbitFrameProvider* _this, Camera* primary_camera)
{
  original(_this, primary_camera);
  Key::Poll();

  auto* sections = Hub::get_SectionManager();
  if (!ModMovesCamera() || !sections || sections->CurrentSection() != SectionID::Starbase_Exterior
      || Key::IsInputFocused())
    return;

  static auto     GetDeltaTime      = il2cpp::ResolveICall<float()>("UnityEngine.Time::get_deltaTime()");
  constexpr float kDegreesPerSecond = 45.0f;
  const float     step              = kDegreesPerSecond * GetDeltaTime();
  if (Shortcut::IsPressed(GameFunction::MoveLeft))
    _this->SetRotationAngleDelta(_this->RotationAngleDelta() - step);
  if (Shortcut::IsPressed(GameFunction::MoveRight))
    _this->SetRotationAngleDelta(_this->RotationAngleDelta() + step);
}

// A held touch reports Stationary, which the game's pan ignores; report it as moving so drags keep panning.
TKTouch* TKTouch_populateWithPosition_Hook(auto original, TKTouch* _this, uintptr_t pos, TouchPhase phase)
{
  auto* touch = original(_this, pos, phase);
  if (touch && touch->phase() == TouchPhase::Stationary)
    touch->SetPhase(TouchPhase::Moved);
  return touch;
}
} // namespace

void InstallPanHooks()
{
  if (auto method = OrbitFrameProvider::Class::Method("UpdateInputData"))
    SPUD_STATIC_DETOUR(method.Pointer(), OrbitFrameProvider_UpdateInputData_Hook);
  if (auto method = TKTouch::Class::Method("populateWithPosition"))
    SPUD_STATIC_DETOUR(method.Pointer(), TKTouch_populateWithPosition_Hook);
  if (auto method = NavigationPan::Class::Method("LateUpdate"))
    SPUD_STATIC_DETOUR(method.Pointer(), NavigationPan_LateUpdate_Hook);
}

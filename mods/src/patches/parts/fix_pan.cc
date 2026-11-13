#include "config.h"
#include "errormsg.h"

#include <il2cpp/il2cpp_helper.h>

#include <prime/Hub.h>
#include <prime/NavigationPan.h>
#include <prime/OrbitFrameProvider.h>
#include <prime/TKTouch.h>

#include <patches/key.h>
#include <patches/mapkey.h>

#include <spud/detour.h>
#include <cmath>

namespace
{
// Native LateUpdate uses _deltaScaler for both keyboard movement and coasting.
// Apply our falloff to the cached delta once, and preserve native keyboard speed
// while preventing a second native fade. Restore both fields after the call.
struct NativePanScalers {
  NavigationPan* pan;
  float          delta;
  float          keyboard;

  NativePanScalers(NavigationPan* pan, float speed)
      : pan(pan)
      , delta(pan->_deltaScaler)
      , keyboard(pan->_keyboardDeltaScaler)
  {
    pan->_deltaScaler         = 1.0f;
    pan->_keyboardDeltaScaler = keyboard * delta * speed;
  }

  ~NativePanScalers()
  {
    pan->_keyboardDeltaScaler = keyboard;
    pan->_deltaScaler         = delta;
  }
};
} // namespace

void NavigationPan_MoveCamera_Hook(auto original, NavigationPan* _this, vec2 delta, bool is_momentum)
{
  // Only fresh recognizer input is scaled. MoveCamera caches the effective
  // movement, so scaling coasting again would compound speed every frame.
  if (!is_momentum && _this->_panningStarted) {
    delta.x *= Config::Get().system_pan_speed;
    delta.y *= Config::Get().system_pan_speed;
  }
  original(_this, delta, is_momentum);
}

TKTouch *TKTouch_populateWithPosition_Hook(auto original, TKTouch *_this, uintptr_t pos, TouchPhase phase)
{
  auto r = original(_this, pos, phase);
  if (r->phase == TouchPhase::Stationary) {
    r->phase = TouchPhase::Moved;
  }
  return r;
}

// Retain the native tutorial and modal/text-input gates while replacing the
// keyboard source through the existing LateUpdate hook.
bool KeyboardPanAllowed()
{
  static auto hub          = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.Core", "Hub");
  static auto tutorial     = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Tutorial", "TutorialManager");
  static auto shortcuts    = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameInput", "ShortcutsManager");
  static auto getTutorial  = hub.GetMethod<void*()>("get_TutorialManager", 0);
  static auto sceneAllowed = tutorial.GetMethod<bool(void*)>("IsSceneInputAllowed", 0);
  static auto shortcutsAllowed = shortcuts.GetMethod<bool()>("get_CanUseShortcuts", 0);
  if (!getTutorial || !sceneAllowed || !shortcutsAllowed)
    return false;
  auto* manager = getTutorial();
  return manager && sceneAllowed(manager) && shortcutsAllowed() && !Key::IsInputFocused();
}

bool NavigationPan_LateUpdate_Hook(auto original, NavigationPan* _this)
{
  auto d = _this->_lastDelta;
  const auto& config = Config::Get();
  const bool disable_target_camera_lock = config.disable_target_camera_lock;

  if (disable_target_camera_lock && _this->_trackingPOI) {
    _this->_trackingPOI = nullptr;
  }
  static auto GetMouseButton = il2cpp_resolve_icall_typed<bool(int)>("UnityEngine.Input::GetMouseButton(System.Int32)");
  static auto GetTouchCount  = il2cpp_resolve_icall_typed<int()>("UnityEngine.Input::get_touchCount()");
  // The native recognizer owns held-drag movement, including when the legacy
  // Input API does not report the pointer. Never replay its delta as momentum.
  const bool dragging = GetMouseButton(0) || GetTouchCount() > 0 || _this->_panningStarted;
  bool by_keys        = false;
  bool native_handled = false;

  if (!config.disable_move_keys && config.hotkeys_enabled && !Key::IsDirectionalInputClaimed()) {
    if (config.use_scopely_hotkeys || !config.installHotkeyHooks) {
      native_handled = true;
      if (!dragging) {
        d->x *= config.system_pan_momentum_falloff;
        d->y *= config.system_pan_momentum_falloff;
        NativePanScalers scalers(_this, config.system_pan_speed);
        original(_this);
      }
    } else if (!_this->BlockPan() && !_this->_trackingPOI && !dragging && KeyboardPanAllowed()) {
      // Opposite bindings cancel; normalize diagonals like the native 2D composite.
      float x = float(MapKey::IsPressed(GameFunction::MoveRight)) - float(MapKey::IsPressed(GameFunction::MoveLeft));
      float y = float(MapKey::IsPressed(GameFunction::MoveUp)) - float(MapKey::IsPressed(GameFunction::MoveDown));
      const float length = std::sqrt(x * x + y * y);
      if (length > 0.0f) {
        d->x = -x / length * _this->_keyboardDeltaScaler;
        d->y = -y / length * _this->_keyboardDeltaScaler;
        const float speed = _this->_deltaScaler * config.system_pan_speed;
        _this->MoveCamera(vec2{d->x * speed, d->y * speed}, true);
        by_keys = true;
      }
    }
  }

  if (_this->BlockPan() || (_this->_trackingPOI && !disable_target_camera_lock)) {
    d->x = 0.0f;
    d->y = 0.0f;
  } else if (!dragging && !by_keys && !native_handled) {
    d->x *= config.system_pan_momentum_falloff;
    d->y *= config.system_pan_momentum_falloff;
    _this->MoveCamera(vec2{d->x, d->y}, true);
  }
  _this->_farMagRadiusRatioSystemExtended = _this->_farMagRadiusRatioSystemNormal;
  return true;
}

void OrbitFrameProvider_UpdateInputData_Hook(auto original, OrbitFrameProvider* _this, Camera* primary_camera)
{
  original(_this, primary_camera);

  auto section_manager = Hub::get_SectionManager();
  if (!Config::Get().hotkeys_enabled || Config::Get().use_scopely_hotkeys || Config::Get().disable_move_keys
      || !section_manager || section_manager->CurrentSection != SectionID::Starbase_Exterior || Key::IsInputFocused()) {
    return;
  }

  static auto     GetDeltaTime            = il2cpp_resolve_icall_typed<float()>("UnityEngine.Time::get_deltaTime()");
  constexpr float keyboard_rotation_speed = 45.0f;
  const auto      frame_delta             = keyboard_rotation_speed * GetDeltaTime();

  if (MapKey::IsPressed(GameFunction::MoveLeft)) {
    _this->RotationAngleDelta() -= frame_delta;
  }
  if (MapKey::IsPressed(GameFunction::MoveRight)) {
    _this->RotationAngleDelta() += frame_delta;
  }
}

void InstallPanHooks()
{
  if (auto& orbit_helper = OrbitFrameProvider::get_class_helper(); !orbit_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Client.CameraController", "OrbitFrameProvider");
  } else if (const auto ptr = orbit_helper.GetMethod("UpdateInputData"); ptr == nullptr) {
    ErrorMsg::MissingMethod("OrbitFrameProvider", "UpdateInputData");
  } else {
    SPUD_STATIC_DETOUR(ptr, OrbitFrameProvider_UpdateInputData_Hook);
  }

  if (auto touchHelper = il2cpp_get_class_helper("TouchKit", "", "TKTouch"); !touchHelper.isValidHelper()) {
    ErrorMsg::MissingHelper("<global>", "TKTouch");
  } else {
    if (const auto ptr = touchHelper.GetMethod("populateWithPosition"); ptr == nullptr) {
      ErrorMsg::MissingMethod("TKTouch", "populateWithPosition");
    } else {
      SPUD_STATIC_DETOUR(ptr, TKTouch_populateWithPosition_Hook);
    }
  }

  if (auto navHelper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "NavigationPan");
      !navHelper.isValidHelper()) {
    ErrorMsg::MissingHelper("Navigation", "NavigationPan");
  } else {
    if (const auto ptr = navHelper.GetMethod("MoveCamera"); ptr == nullptr) {
      ErrorMsg::MissingMethod("NavigationPan", "MoveCamera");
    } else {
      SPUD_STATIC_DETOUR(ptr, NavigationPan_MoveCamera_Hook);
    }
    if (const auto ptr = navHelper.GetMethod("LateUpdate"); ptr == nullptr) {
      ErrorMsg::MissingMethod("NavigationPan", "LateUpdate");
    } else {
      SPUD_STATIC_DETOUR(ptr, NavigationPan_LateUpdate_Hook);
    }
  }
}

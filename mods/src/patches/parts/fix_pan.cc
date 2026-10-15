#include "config.h"

#include <il2cpp/il2cpp.h>

#include <prime/unity.h>
#include <prime/Hub.h>
#include <prime/navigation.h>
#include <prime/ui.h>

#include "patches/input.h"
#include "patches/shortcuts.h"

#include <spud/detour.h>
#include <cmath>

TKTouch *TKTouch_populateWithPosition_Hook(auto original, TKTouch *_this, uintptr_t pos, TouchPhase phase)
{
  auto r = original(_this, pos, phase);
  if (r->phase() == TouchPhase::Stationary) {
    r->SetPhase(TouchPhase::Moved);
  }
  return r;
}

namespace
{
struct TutorialManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Tutorial", "TutorialManager">;

  bool IsSceneInputAllowed()
  { return Call<"IsSceneInputAllowed", bool>(); }
};
} // namespace

// Retain the native tutorial and modal/text-input gates while replacing the
// keyboard source through the existing LateUpdate hook, without new detours.
bool KeyboardPanAllowed()
{
  auto* manager = Hub::Class::Get<"TutorialManager", TutorialManager*>();
  return manager && manager->IsSceneInputAllowed() && ShortcutsManager::CanUseShortcuts() && !Key::IsInputFocused();
}

bool NavigationPan_LateUpdate_Hook(auto original, NavigationPan* _this)
{
  static auto GetMouseButton = il2cpp::ResolveICall<bool(int)>("UnityEngine.Input::GetMouseButton(System.Int32)");
  static auto GetTouchCount = il2cpp::ResolveICall<int()>("UnityEngine.Input::get_touchCount()");
  Key::Poll();
  const auto& config = Config::Get();
  auto        d      = _this->lastDelta();
  const bool  disable_target_camera_lock = config.graphics.disable_target_camera_lock;

  if (disable_target_camera_lock && _this->trackingPOI()) {
    _this->SetTrackingPOI(nullptr);
  }
  // An arrow held for a CTRL-arrow style shortcut must not pan as well, natively or through the mod.
  const bool arrow_owned = Shortcut::Owns(KeyCode::LeftArrow) || Shortcut::Owns(KeyCode::RightArrow)
                           || Shortcut::Owns(KeyCode::UpArrow) || Shortcut::Owns(KeyCode::DownArrow);

  // The native recognizer owns held-drag movement, including when the legacy
  // Input API does not report the pointer. Never replay its delta as momentum.
  const bool dragging = GetMouseButton(0) || GetTouchCount() > 0 || _this->panningStarted();
  bool by_keys        = false;
  bool native_handled = false;

  if (config.control.hotkeys_enabled && !config.ui.disable_move_keys && !arrow_owned) {
    if (config.control.use_scopely_hotkeys || !config.patches.hotkeyhooks) {
      original(_this);
      d              = _this->lastDelta();
      native_handled = true;
    } else if (!_this->BlockPan() && !_this->trackingPOI() && !dragging && KeyboardPanAllowed()) {
      // Opposite bindings cancel; normalize diagonals like the native 2D composite.
      float x = float(Shortcut::IsPressed(GameFunction::MoveRight)) - float(Shortcut::IsPressed(GameFunction::MoveLeft));
      float y = float(Shortcut::IsPressed(GameFunction::MoveUp)) - float(Shortcut::IsPressed(GameFunction::MoveDown));
      const float length = std::sqrt(x * x + y * y);
      if (length > 0.0f) {
        d.x = -x / length * _this->keyboardDeltaScaler();
        d.y = -y / length * _this->keyboardDeltaScaler();
        _this->SetLastDelta(d);
        _this->MoveCamera(vec2{d.x * _this->deltaScaler(), d.y * _this->deltaScaler()}, true);
        by_keys = true;
      }
    }
  }

  if (_this->BlockPan() || (_this->trackingPOI() && !disable_target_camera_lock)) {
    d.x = 0.0f;
    d.y = 0.0f;
  } else if (!dragging && !by_keys) {
    d.x               = d.x * Config::Get().graphics.system_pan_momentum_falloff;
    d.y               = d.y * Config::Get().graphics.system_pan_momentum_falloff;
    _this->SetLastDelta(d);
    if (!native_handled) {
      _this->MoveCamera(vec2{d.x, d.y}, true);
    }
  }
  _this->SetLastDelta(d);
  _this->SetFarMagRadiusRatioSystemExtended(_this->farMagRadiusRatioSystemNormal());
  return true;
}

void OrbitFrameProvider_UpdateInputData_Hook(auto original, OrbitFrameProvider* _this, Camera* primary_camera)
{
  original(_this, primary_camera);
  Key::Poll();

  auto section_manager = Hub::get_SectionManager();
  if (!Config::Get().control.hotkeys_enabled || Config::Get().control.use_scopely_hotkeys || Config::Get().ui.disable_move_keys
      || !section_manager || section_manager->CurrentSection() != SectionID::Starbase_Exterior || Key::IsInputFocused()) {
    return;
  }

  static auto     GetDeltaTime            = il2cpp::ResolveICall<float()>("UnityEngine.Time::get_deltaTime()");
  constexpr float keyboard_rotation_speed = 45.0f;
  const auto      frame_delta             = keyboard_rotation_speed * GetDeltaTime();

  if (Shortcut::IsPressed(GameFunction::MoveLeft)) {
    _this->SetRotationAngleDelta(_this->RotationAngleDelta() - frame_delta);
  }
  if (Shortcut::IsPressed(GameFunction::MoveRight)) {
    _this->SetRotationAngleDelta(_this->RotationAngleDelta() + frame_delta);
  }
}

void InstallPanHooks()
{
  if (auto method = OrbitFrameProvider::Class::Method("UpdateInputData"))
    SPUD_STATIC_DETOUR(method.Pointer(), OrbitFrameProvider_UpdateInputData_Hook);

  if (auto method = TKTouch::Class::Method("populateWithPosition"))
    SPUD_STATIC_DETOUR(method.Pointer(), TKTouch_populateWithPosition_Hook);

  if (auto method = NavigationPan::Class::Method("LateUpdate"))
    SPUD_STATIC_DETOUR(method.Pointer(), NavigationPan_LateUpdate_Hook);
}

#pragma once

// The game's camera controller (Digit.Client.CameraController): frame providers, the orbits and blends between them,
// and the constraints that bound them. Used by the system, station interior and Haven cameras alike.

#include <il2cpp/il2cpp.h>
#include <unity/core.h>

// Constraint/FloatData: an optional [min, max] range.
struct FloatConstraint : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "Constraint/FloatData">;

  bool enabled()
  { return Field<"_enabled", bool>(); }
  float min()
  { return Field<"_min", float>(); }
  float max()
  { return Field<"_max", float>(); }
  void SetMin(float value)
  { SetField<"_min">(value); }
  void SetMax(float value)
  { SetField<"_max">(value); }
};

struct OrbitConstraint : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "OrbitConstraint">;

  FloatConstraint* radius()
  { return Field<"_radius", FloatConstraint*>(); }
};

struct TargetController : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "AbstractTargetController">;
};

struct CameraFrame : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "CameraFrame">;

  unity::Vector3 position()
  { return Field<"_position", unity::Vector3>(); }
  unity::Quaternion rotation()
  { return Field<"_rotation", unity::Quaternion>(); }
  float fov()
  { return Field<"_FOV", float>(); }
  float farPlane()
  { return Field<"_farPlane", float>(); }
  bool orthographic()
  { return Field<"_orthographic", bool>(); }
  void SetPosition(unity::Vector3 value)
  { SetField<"_position">(value); }
  void SetFarPlane(float value)
  { SetField<"_farPlane">(value); }
};

struct EditableCameraFrame : CameraFrame {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "EditableCameraFrame">;
};

struct FrameProvider : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "FrameProvider">;
};

// An orbit around a pivot, at a radius its constraint bounds.
struct AbstractOrbitFrameProvider : FrameProvider {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "AbstractOrbitFrameProvider">;

  OrbitConstraint* constraint()
  { return Field<"_constraint", OrbitConstraint*>(); }
  float radius()
  { return Field<"_radius", float>(); }
  TargetController* orbitPivot()
  { return Field<"_orbitPivot", TargetController*>(); }
  TargetController* lookTarget()
  { return Field<"_lookTarget", TargetController*>(); }
  float rotationAngleDelta()
  { return Field<"_rotationAngleDelta", float>(); }
  void SetRotationAngleDelta(float value)
  { SetField<"_rotationAngleDelta">(value); }
};

// The system view's camera, and the far end of the station interior's blend.
struct OrbitFrameProvider : AbstractOrbitFrameProvider {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "OrbitFrameProvider">;
};

struct PlanetaryOrbitFrameProvider : OrbitFrameProvider {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "PlanetaryOrbitFrameProvider">;
};

// Blends two providers' frames by a ratio the zoom drives, within soft limits.
struct BlendFrameProvider : FrameProvider {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "BlendFrameProvider">;

  FrameProvider* sourceFrameProvider()
  { return Field<"_sourceFrameProvider", FrameProvider*>(); }
  FrameProvider* targetFrameProvider()
  { return Field<"_targetFrameProvider", FrameProvider*>(); }
  unity::AnimationCurve* blendCurve()
  { return Field<"_blendCurve", unity::AnimationCurve*>(); }
  float softLimitMin()
  { return Field<"_softLimitMin", float>(); }
  float softLimitMax()
  { return Field<"_softLimitMax", float>(); }
  float blendRatio()
  { return Field<"_blendRatio", float>(); }
  CameraFrame* sourceFrame()
  { return Field<"_sourceFrame", CameraFrame*>(); }
  CameraFrame* targetFrame()
  { return Field<"_targetFrame", CameraFrame*>(); }
  EditableCameraFrame* blendedCameraFrame()
  { return Field<"_blendedCameraFrame", EditableCameraFrame*>(); }
};

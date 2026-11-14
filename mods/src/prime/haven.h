#pragma once

// Haven's camera providers, blend frames and help history.
#include "prime/ui.h"

#include <il2cpp/il2cpp.h>
#include <unity/core.h>

struct HavenRadiusConstraint : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "Constraint/FloatData">;
  bool Enabled()
  { return Field<"_enabled", bool>(); }
  float Minimum()
  { return Field<"_min", float>(); }
  float Maximum()
  { return Field<"_max", float>(); }
};

struct HavenOrbitConstraint : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "OrbitConstraint">;
  HavenRadiusConstraint* Radius()
  { return Field<"_radius", HavenRadiusConstraint*>(); }
};

struct HavenTargetController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "AbstractTargetController">;
};

struct HavenOrbitProvider : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "AbstractOrbitFrameProvider">;
  HavenOrbitConstraint* Constraint()
  { return Field<"_constraint", HavenOrbitConstraint*>(); }
  float Radius()
  { return Field<"_radius", float>(); }
  HavenTargetController* Pivot()
  { return Field<"_orbitPivot", HavenTargetController*>(); }
  HavenTargetController* LookTarget()
  { return Field<"_lookTarget", HavenTargetController*>(); }
};

struct HavenPlanetaryProvider : HavenOrbitProvider {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "PlanetaryOrbitFrameProvider">;
};

struct HavenCameraFrame : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "CameraFrame">;
  unity::Vector3 Position()
  { return Field<"_position", unity::Vector3>(); }
  unity::Quaternion Rotation()
  { return Field<"_rotation", unity::Quaternion>(); }
  float Fov()
  { return Field<"_FOV", float>(); }
  float FarClip()
  { return Field<"_farPlane", float>(); }
  bool Orthographic()
  { return Field<"_orthographic", bool>(); }
  void SetPosition(unity::Vector3 value)
  { SetField<"_position">(value); }
  void SetFarClip(float value)
  { SetField<"_farPlane">(value); }
};

struct HavenEditableCameraFrame : HavenCameraFrame {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "EditableCameraFrame">;
};

struct HavenFrameProvider : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "FrameProvider">;
};

struct HavenBlendProvider : HavenFrameProvider {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "BlendFrameProvider">;
  HavenFrameProvider* Source()
  { return Field<"_sourceFrameProvider", HavenFrameProvider*>(); }
  HavenFrameProvider* Target()
  { return Field<"_targetFrameProvider", HavenFrameProvider*>(); }
  unity::AnimationCurve* Curve()
  { return Field<"_blendCurve", unity::AnimationCurve*>(); }
  float Minimum()
  { return Field<"_softLimitMin", float>(); }
  float Maximum()
  { return Field<"_softLimitMax", float>(); }
  float Ratio()
  { return Field<"_blendRatio", float>(); }
  HavenCameraFrame* SourceFrame()
  { return Field<"_sourceFrame", HavenCameraFrame*>(); }
  HavenCameraFrame* TargetFrame()
  { return Field<"_targetFrame", HavenCameraFrame*>(); }
  HavenEditableCameraFrame* ResultFrame()
  { return Field<"_blendedCameraFrame", HavenEditableCameraFrame*>(); }
};

struct PlanetaryHelpHistoryContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlanetaryBase", "PlanetaryHelpHistoryContext">;
};

// Validate the complete write path before installing the camera hook. A missing field must retain native output.
inline bool HavenCameraBindingsReady()
{
  return HavenBlendProvider::Class::HasField<"_sourceFrameProvider", HavenFrameProvider*>()
         && HavenBlendProvider::Class::HasField<"_targetFrameProvider", HavenFrameProvider*>()
         && HavenBlendProvider::Class::HasField<"_blendCurve", unity::AnimationCurve*>()
         && HavenBlendProvider::Class::HasField<"_softLimitMin", float>()
         && HavenBlendProvider::Class::HasField<"_softLimitMax", float>()
         && HavenBlendProvider::Class::HasField<"_blendRatio", float>()
         && HavenBlendProvider::Class::HasField<"_sourceFrame", HavenCameraFrame*>()
         && HavenBlendProvider::Class::HasField<"_targetFrame", HavenCameraFrame*>()
         && HavenBlendProvider::Class::HasField<"_blendedCameraFrame", HavenEditableCameraFrame*>()
         && HavenOrbitProvider::Class::HasField<"_constraint", HavenOrbitConstraint*>()
         && HavenOrbitProvider::Class::HasField<"_radius", float>()
         && HavenOrbitProvider::Class::HasField<"_orbitPivot", HavenTargetController*>()
         && HavenOrbitProvider::Class::HasField<"_lookTarget", HavenTargetController*>()
         && HavenOrbitConstraint::Class::HasField<"_radius", HavenRadiusConstraint*>()
         && HavenRadiusConstraint::Class::HasField<"_enabled", bool>()
         && HavenRadiusConstraint::Class::HasField<"_min", float>()
         && HavenRadiusConstraint::Class::HasField<"_max", float>()
         && HavenCameraFrame::Class::HasField<"_position", unity::Vector3>()
         && HavenCameraFrame::Class::HasField<"_rotation", unity::Quaternion>()
         && HavenCameraFrame::Class::HasField<"_FOV", float>()
         && HavenCameraFrame::Class::HasField<"_farPlane", float>()
         && HavenCameraFrame::Class::HasField<"_orthographic", bool>()
         && static_cast<bool>(unity::AnimationCurve::Class::Method<float(float)>("Evaluate"));
}

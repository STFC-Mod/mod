#pragma once

#include <il2cpp/il2cpp.h>

struct OrbitFrameProvider : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "OrbitFrameProvider">;

  // Declared on AbstractOrbitFrameProvider.
  float RotationAngleDelta()
  { return Field<"_rotationAngleDelta", float>(); }
  void SetRotationAngleDelta(float value)
  { SetField<"_rotationAngleDelta">(value); }
};

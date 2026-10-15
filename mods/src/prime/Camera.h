#pragma once

#include "Color.h"

#include <il2cpp/il2cpp.h>

struct Camera : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Camera">;

  float farClipPlane()
  { return Get<"farClipPlane", float>(); }
  void SetFarClipPlane(float value)
  { Set<"farClipPlane">(value); }
  float nearClipPlane()
  { return Get<"nearClipPlane", float>(); }
  void SetNearClipPlane(float value)
  { Set<"nearClipPlane">(value); }
  // UnityEngine.CameraClearFlags
  int32_t clearFlags()
  { return Get<"clearFlags", int32_t>(); }
  void SetClearFlags(int32_t value)
  { Set<"clearFlags">(value); }
  color backgroundColor()
  { return Get<"backgroundColor", color>(); }
  void SetBackgroundColor(color value)
  { Set<"backgroundColor">(value); }
};

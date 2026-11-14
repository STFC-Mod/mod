#pragma once

#include "Vector2.h"

#include <il2cpp/il2cpp.h>

struct CanvasScaler : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.UI", "CanvasScaler">;

  // Writes Canvas.scaleFactor directly. The scaleFactor property is only applied on the scaler's next Update.
  void SetScaleFactor(float value)
  { Call<"SetScaleFactor">(value); }

  float scaleFactor()
  { return Get<"scaleFactor", float>(); }
  // Named after the managed setter because SetScaleFactor is the managed method above.
  void set_scaleFactor(float value)
  { Set<"scaleFactor">(value); }
  vec2 referenceResolution()
  { return Get<"referenceResolution", vec2>(); }
  void SetReferenceResolution(vec2 value)
  { Set<"referenceResolution">(value); }
};

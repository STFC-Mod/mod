#pragma once

#include <il2cpp/il2cpp.h>

struct Canvas : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UIModule", "UnityEngine", "Canvas">;

  float scaleFactor()
  { return Get<"scaleFactor", float>(); }
  void SetScaleFactor(float value)
  { Set<"scaleFactor">(value); }
};

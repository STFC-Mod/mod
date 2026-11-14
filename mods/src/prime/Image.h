#pragma once

#include "Color.h"
#include "Component.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct Sprite : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Sprite">;

  float pixelsPerUnit()
  { return Get<"pixelsPerUnit", float>(); }
};

// UnityEngine.UI.Image; color and raycastTarget come from Graphic.
struct Image : Component {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.UI", "Image">;

  enum class Type : int32_t {
    Simple = 0,
    Sliced = 1,
    Tiled  = 2,
    Filled = 3,
  };

  Sprite* sprite()
  { return Get<"sprite", Sprite*>(); }
  void SetSprite(Sprite* value)
  { Set<"sprite">(value); }
  Sprite* overrideSprite()
  { return Get<"overrideSprite", Sprite*>(); }
  void SetOverrideSprite(Sprite* value)
  { Set<"overrideSprite">(value); }
  void SetType(Type value)
  { Set<"type">(value); }
  void SetPreserveAspect(bool value)
  { Set<"preserveAspect">(value); }
  void SetPixelsPerUnitMultiplier(float value)
  { Set<"pixelsPerUnitMultiplier">(value); }
  void SetColor(color value)
  { Set<"color">(value); }
  void SetRaycastTarget(bool value)
  { Set<"raycastTarget">(value); }
  void SetVerticesDirty()
  { Call<"SetVerticesDirty">(); }
};

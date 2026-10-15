#pragma once

#include "Transform.h"
#include "Vector2.h"

#include <il2cpp/il2cpp.h>

struct RectTransform : Transform {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "RectTransform">;

  Vector2 anchorMin()
  { return Get<"anchorMin", Vector2>(); }
  Vector2 anchorMax()
  { return Get<"anchorMax", Vector2>(); }
  Vector2 offsetMin()
  { return Get<"offsetMin", Vector2>(); }
  Vector2 offsetMax()
  { return Get<"offsetMax", Vector2>(); }
  void SetOffsetMin(Vector2 value)
  { Set<"offsetMin">(value); }
  void SetOffsetMax(Vector2 value)
  { Set<"offsetMax">(value); }
  void SetAnchorMin(Vector2 value)
  { Set<"anchorMin">(value); }
  void SetAnchorMax(Vector2 value)
  { Set<"anchorMax">(value); }
  void SetPivot(Vector2 value)
  { Set<"pivot">(value); }
  void SetSizeDelta(Vector2 value)
  { Set<"sizeDelta">(value); }
  void SetAnchoredPosition(Vector2 value)
  { Set<"anchoredPosition">(value); }
};

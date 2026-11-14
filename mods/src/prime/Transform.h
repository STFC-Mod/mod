#pragma once

#include "GameObject.h"
#include "Vector3.h"

#include <il2cpp/il2cpp.h>

struct Transform : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Transform">;

  int32_t childCount()
  { return Get<"childCount", int32_t>(); }
  Transform* GetChild(int32_t index)
  { return Call<"GetChild", Transform*>(index); }
  Transform* parent()
  { return Get<"parent", Transform*>(); }
  GameObject* gameObject()
  { return Get<"gameObject", GameObject*>(); }
  Vector3 localScale()
  { return Get<"localScale", Vector3>(); }
  void SetLocalScale(Vector3 value)
  { Set<"localScale">(value); }
  void SetLocalEulerAngles(Vector3 value)
  { Set<"localEulerAngles">(value); }
  void SetParent(Transform* parent, bool worldPositionStays)
  { Call<"SetParent">(parent, worldPositionStays); }
  void SetAsFirstSibling()
  { Call<"SetAsFirstSibling">(); }
  // Finds a descendant by slash-separated path, e.g. "Hint/Background".
  Transform* Find(Il2CppString* path)
  { return Call<"Find", Transform*>(path); }
};

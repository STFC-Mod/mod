#pragma once

#include "GameObject.h"
#include "Transform.h"

#include <il2cpp/il2cpp.h>

// UnityEngine.Component, the base of every MonoBehaviour. Use As<Component>() on other bindings.
struct Component : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Component">;

  Transform* transform()
  { return Get<"transform", Transform*>(); }
  GameObject* gameObject()
  { return Get<"gameObject", GameObject*>(); }
};

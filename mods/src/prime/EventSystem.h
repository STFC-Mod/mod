#pragma once

#include "GameObject.h"

#include <il2cpp/il2cpp.h>

struct EventSystem : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.EventSystems", "EventSystem">;

  static EventSystem* current()
  { return Class::Get<"current", EventSystem*>(); }

  GameObject* currentSelectedGameObject()
  { return Get<"currentSelectedGameObject", GameObject*>(); }
  void SetSelectedGameObject(GameObject* object)
  { Call<"SetSelectedGameObject">(object); }
};

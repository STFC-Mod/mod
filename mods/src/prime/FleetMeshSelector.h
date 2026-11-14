#pragma once

#include "GameObject.h"

#include <il2cpp/il2cpp.h>

struct FleetMeshSelector : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "FleetMeshSelector">;

  GameObject* LoadedObject()
  { return Get<"LoadedObject", GameObject*>(); }
};

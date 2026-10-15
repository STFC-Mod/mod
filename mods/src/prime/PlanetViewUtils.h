#pragma once

#include "PopulatedSystemData.h"
#include "Transform.h"

#include <il2cpp/il2cpp.h>

struct PlanetViewUtils : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "PlanetViewUtils">;

  // Managed FlatRenderable. Calling this also runs any detour on get_FlatRenderable.
  void* GetFlatRenderable()
  { return Get<"FlatRenderable", void*>(); }

  PopulatedSystemData* popData()
  { return Field<"_popData", PopulatedSystemData*>(); }
  Transform* activeSystemParent()
  { return Field<"_activeSystemParent", Transform*>(); }
};

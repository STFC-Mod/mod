#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct PopulatedSystemData : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "PopulatedSystemData">;

  void* backdrop()
  { return Field<"_backdrop", void*>(); }
  int64_t systemId()
  { return Field<"_systemId", int64_t>(); }
};

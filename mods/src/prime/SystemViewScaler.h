#pragma once

#include <il2cpp/il2cpp.h>

struct SystemViewScaler : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "SystemViewScaler">;

  float baseSystemRadius()
  { return Field<"_baseSystemRadius", float>(); }
  void SetBaseSystemRadius(float value)
  { SetField<"_baseSystemRadius">(value); }
};

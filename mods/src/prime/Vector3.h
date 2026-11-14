#pragma once

#include <il2cpp/il2cpp.h>

struct Vector3 {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Vector3">;

  float x;
  float y;
  float z;

  static Vector3 zero()
  { return Class::Field<"zeroVector", Vector3>(); }
};

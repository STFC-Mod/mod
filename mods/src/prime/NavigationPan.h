#pragma once

#include "Vector2.h"

#include <il2cpp/il2cpp.h>

#pragma pack(push, 1)
struct vec3 {
  float x, y, z;
};
#pragma pack(pop)

struct NavigationPan : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationPan">;

  void MoveCamera(vec2 delta, bool isMomentum = false)
  { Call<"MoveCamera">(delta, isMomentum); }

  static bool BlockPan()
  { return Class::Field<"BlockPan", bool>(); }

  Vector2 lastDelta()
  { return Field<"_lastDelta", Vector2>(); }
  void SetLastDelta(Vector2 value)
  { SetField<"_lastDelta">(value); }
  bool panningStarted()
  { return Field<"_panningStarted", bool>(); }
  float deltaScaler()
  { return Field<"_deltaScaler", float>(); }
  float keyboardDeltaScaler()
  { return Field<"_keyboardDeltaScaler", float>(); }
  // Managed POI; only checked for null or cleared.
  void* trackingPOI()
  { return Field<"_trackingPOI", void*>(); }
  void SetTrackingPOI(void* value)
  { SetField<"_trackingPOI">(value); }

  float farMagRadiusRatioSystemNormal()
  { return Field<"_farMagRadiusRatioSystemNormal", float>(); }
  void SetFarMagRadiusRatioSystemNormal(float value)
  { SetField<"_farMagRadiusRatioSystemNormal">(value); }
  float farMagRadiusRatioSystemExtended()
  { return Field<"_farMagRadiusRatioSystemExtended", float>(); }
  void SetFarMagRadiusRatioSystemExtended(float value)
  { SetField<"_farMagRadiusRatioSystemExtended">(value); }
  float nearMagRadiusRatioSystem()
  { return Field<"_nearMagRadiusRatioSystemNormal", float>(); }
};

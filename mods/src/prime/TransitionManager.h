#pragma once

#include "BlurController.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct TransitionManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.LoadingScreen", "TransitionManager">;

  // Public field named BlurController; the S prefix keeps the type name visible in this scope.
  BlurController* SBlurController()
  { return Field<"BlurController", BlurController*>(); }
  int32_t waitFrames()
  { return Field<"_waitFrames", int32_t>(); }
  void SetWaitFrames(int32_t value)
  { SetField<"_waitFrames">(value); }
};

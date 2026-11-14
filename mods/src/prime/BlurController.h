#pragma once

#include <il2cpp/il2cpp.h>

struct BlurController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.LoadingScreen", "BlurController">;

  float blurTime()
  { return Field<"_blurTime", float>(); }
  void SetBlurTime(float value)
  { SetField<"_blurTime">(value); }
  int32_t waitFrames()
  { return Field<"_waitFrames", int32_t>(); }
  void SetWaitFrames(int32_t value)
  { SetField<"_waitFrames">(value); }

  void ForceCompletion()
  { Call<"ForceCompletion">(); }
};

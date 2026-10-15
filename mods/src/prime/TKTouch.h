#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class TouchPhase : int32_t {
  Began      = 0,
  Moved      = 1,
  Stationary = 2,
  Ended      = 3,
  Canceled   = 4,
};

struct TKTouch : il2cpp::Object {
  using Class = il2cpp::Class<"TouchKit", "", "TKTouch">;

  TouchPhase phase()
  { return Field<"phase", TouchPhase>(); }
  void SetPhase(TouchPhase phase)
  { SetField<"phase">(phase); }
};

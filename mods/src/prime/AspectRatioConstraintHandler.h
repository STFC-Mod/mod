#pragma once

#if _WIN32

#include <il2cpp/il2cpp.h>

struct AspectRatioConstraintHandler {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Utils", "AspectRatioConstraintHandler">;

  static WNDPROC _unityWndProc()
  { return reinterpret_cast<WNDPROC>(Class::Field<"_unityWndProc", intptr_t>()); }
};

#endif

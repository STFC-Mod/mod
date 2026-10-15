#pragma once

#include <il2cpp/il2cpp.h>

// Digit.Client.UI.Widget`1. Members resolve on each widget's runtime class.
template <typename T> struct Widget : il2cpp::Object {
  T* Context()
  { return Get<"Context", T*>(); }
  bool enabled()
  { return Get<"enabled", bool>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  bool IsActive()
  { return Call<"IsActive", bool>(); }
};

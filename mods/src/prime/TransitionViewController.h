#pragma once

#include <il2cpp/il2cpp.h>

struct TransitionViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.LoadingScreen", "TransitionViewController">;

  // Image
  void* staticOverride()
  { return Field<"_staticOverride", void*>(); }
  // Animator
  void* animator()
  { return Field<"_animator", void*>(); }
  // SmartScroller
  void* scroller()
  { return Field<"_scroller", void*>(); }
};

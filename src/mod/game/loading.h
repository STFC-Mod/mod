#pragma once

// The login screen, the transition screen shown between sections, and their loading tips and slide shows.

#include <il2cpp/il2cpp.h>
#include <unity/ui.h>

struct LoginSequence : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Login", "LoginSequence">;

  unity::Component* mainCanvas()
  { return Field<"_mainCanvas", unity::Component*>(); }
};

struct LoadingTipViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.LoadingScreen", "LoadingTipViewController">;

  unity::TMP_Text* textMeshPro()
  { return Field<"_textMeshPro", unity::TMP_Text*>(); }
};

struct TransitionViewController : unity::Component {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.LoadingScreen", "TransitionViewController">;

  unity::Image* staticOverride()
  { return Field<"_staticOverride", unity::Image*>(); }
  // The canvas Animator; Behaviour is enough to toggle it.
  unity::Behaviour* animator()
  { return Field<"_animator", unity::Behaviour*>(); }
};

struct SlideShowViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.SlideShow", "SlideShowViewController">;

  unity::Image* image()
  { return Field<"_image", unity::Image*>(); }
};

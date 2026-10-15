#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class VisibilityState : int32_t // TypeDefIndex: 13743
{
  Unknown = 0,
  Show    = 1,
  Hide    = 2,
  Hidden  = 3,
  Visible = 4
};

struct VisibilityController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "VisibilityController">;

  // bool selects Show(bool)/Hide(bool) over the int entry-point overloads.
  void Show(bool instant = false)
  { Call<"Show">(instant); }
  void Hide(bool instant = false)
  { Call<"Hide">(instant); }

  VisibilityState state()
  { return Field<"_state", VisibilityState>(); }
  VisibilityState State()
  { return Get<"VisibilityState", VisibilityState>(); }
};

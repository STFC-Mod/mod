#pragma once

#include <il2cpp/il2cpp.h>

struct GenericButtonContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "GenericButtonContext">;

  bool Interactable()
  { return Get<"Interactable", bool>(); }
  void SetInteractable(bool value)
  { Set<"Interactable">(value); }
};

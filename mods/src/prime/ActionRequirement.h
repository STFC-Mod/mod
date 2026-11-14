#pragma once

#include <il2cpp/il2cpp.h>

struct ActionRequirement : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ActionRequirement">;

  bool IsMet()
  { return Get<"IsMet", bool>(); }
  bool CheckIsMet()
  { return Call<"CheckIsMet", bool>(); }
};

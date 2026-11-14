#pragma once

#include "ClientModifierType.h"

#include <il2cpp/il2cpp.h>

struct IBuffData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit", "IBuffData">;

  ClientModifierType ModifierType()
  { return Get<"ModifierType", ClientModifierType>(); }
};

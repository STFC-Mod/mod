#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

// Interface binding; the getters dispatch to the receiver's implementation.
struct IBuffComparer : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "IBuffComparer">;

  int64_t get_HullId()
  { return Get<"HullID", int64_t>(); }
  int64_t get_FactionId()
  { return Get<"FactionID", int64_t>(); }
};

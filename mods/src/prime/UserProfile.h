#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct UserProfile : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "UserProfile">;

  int64_t LocaId()
  { return Field<"_locaId", int64_t>(); }
  Il2CppString* Name()
  { return Field<"name_", Il2CppString*>(); }
  Il2CppString* UserId()
  { return Get<"UserId", Il2CppString*>(); }
  int32_t Level()
  { return Get<"Level", int32_t>(); }
};

#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class HullType : int32_t {
  Any          = -1,
  Destroyer    = 0,
  Survey       = 1,
  Explorer     = 2,
  Battleship   = 3,
  Defense      = 4,
  ArmadaTarget = 5
};

struct IdRefs : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "IdRefs">;

  Il2CppString* locaStringId()
  { return Field<"_locaStringId", Il2CppString*>(); }
  int64_t locaId()
  { return Field<"locaId_", int64_t>(); }
};

struct HullSpec : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "HullSpec">;

  int64_t Id()
  { return Field<"id_", int64_t>(); }
  Il2CppString* Name()
  { return Field<"name_", Il2CppString*>(); }
  Il2CppString* IdStr()
  { return Field<"idStr_", Il2CppString*>(); }
  HullType Type()
  { return Get<"Type", HullType>(); }
  IdRefs* idRefs()
  { return Field<"idRefs_", IdRefs*>(); }
};

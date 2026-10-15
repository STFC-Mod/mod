#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class BattleType : int32_t {
  Fleet                       = 0,
  Base                        = 1,
  PassiveMarauder             = 2,
  NpcInstantiated             = 3,
  DockingPoint                = 4,
  ActiveMarauder_MarauderInit = 5,
  ActiveMarauder_PlayerInit   = 6,
  ArmadaBase                  = 7,
  ArmadaMarauder              = 8,
  PveDockingPoint             = 9,
  ArmadaAsb                   = 10,
  ArmadaMta                   = 11,
  Hazard                      = 12,
  PveCuttingBeam              = 13,
  PvpCuttingBeam              = 14,
  PveChainShot                = 15,
  PvpChainShot                = 16
};

enum class BattleResultType : int32_t {
  Defeat         = 0,
  Victory        = 1,
  PartialVictory = 2
};

enum class FleetDataType : int32_t {
  DeployedFleet = 0,
  Starbase      = 1,
  Armada        = 2
};

struct BattleResultHeader : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BattleResultHeader">;

  Il2CppObject* get_PlayerUserProfile()
  { return Get<"PlayerUserProfile", Il2CppObject*>(); }
  Il2CppObject* get_EnemyUserProfile()
  { return Get<"EnemyUserProfile", Il2CppObject*>(); }
  Il2CppObject* get_SpecService()
  { return Field<"_specService", Il2CppObject*>(); }
  int32_t get_BattleType()
  { return Get<"BattleType", int32_t>(); }

  int64_t PlayerShipHullId()
  { return Get<"PlayerShipHullId", int64_t>(); }
  int64_t EnemyShipHullId()
  { return Get<"EnemyShipHullId", int64_t>(); }
};

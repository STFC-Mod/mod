#pragma once

#include <il2cpp/il2cpp.h>

enum ToastState {
  All                       = -1,
  Standard                  = 0,
  FactionWarning            = 1,
  FactionLevelUp            = 2,
  FactionLevelDown          = 3,
  FactionDiscovered         = 4,
  IncomingAttack            = 5,
  IncomingAttackFaction     = 6,
  FleetBattle               = 7,
  StationBattle             = 8,
  StationVictory            = 9,
  Victory                   = 10,
  Defeat                    = 11,
  StationDefeat             = 12,
  Tournament                = 14,
  ArmadaCreated             = 15,
  ArmadaCanceled            = 16,
  ArmadaIncomingAttack      = 17,
  ArmadaBattleWon           = 18,
  ArmadaBattleLost          = 19,
  DiplomacyUpdated          = 20,
  JoinedTakeover            = 21,
  CompetitorJoinedTakeover  = 22,
  AbandonedTerritory        = 23,
  TakeoverVictory           = 24,
  TakeoverDefeat            = 25,
  TreasuryProgress          = 26,
  TreasuryFull              = 27,
  Achievement               = 28,
  AssaultVictory            = 29,
  AssaultDefeat             = 30,
  ChallengeComplete         = 31,
  ChallengeFailed           = 32,
  StrikeHit                 = 33,
  StrikeDefeat              = 34,
  WarchestProgress          = 35,
  WarchestFull              = 36,
  PartialVictory            = 37,
  ArenaTimeLeft             = 38,
  ChainedEventScored        = 39,
  FleetPresetApplied        = 40,
  SurgeWarmUpEnded              = 41,
  SurgeHostileGroupDefeated     = 42,
  SurgeTimeLeft                 = 43,
  QueueForLeaseActivated        = 44,
  QueueForLeaseExpired          = 45,
  PermanentQueuePurchased       = 46,
  OutpostStartedOrEnded         = 47,
  CrossAllianceArmadaVictory    = 48,
  CrossAllianceArmadaDefeat     = 49,
  CrossAllianceArmadaPartialVictory = 50,
  FactionWeeklyEventsProgress   = 51,
  FactionWeeklyEventsComplete   = 52,
  ArmadaPlayerBlocked           = 53,
  ArmadaPlayerUnblocked         = 54,
  DynamicCrisisUpdate           = 55,
  DynamicCrisisFailed           = 56,
  DynamicCrisisCompleted        = 57,
  GalacticAnomalySystemEntered  = 58,
};

struct Toast : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "Toast">;

  il2cpp::Array<Il2CppObject*>* get_TextParameters()
  { return Get<"TextParameters", il2cpp::Array<Il2CppObject*>*>(); }
  // LocaleTextContext
  void* get_TextLocaleTextContext()
  { return Get<"TextLocaleTextContext", void*>(); }
  Il2CppObject* get_Data()
  { return Get<"Data", Il2CppObject*>(); }
  int get_State()
  { return Get<"State", ToastState>(); }
};

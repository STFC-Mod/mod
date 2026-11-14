#pragma once
#include "MonoSingleton.h"
#include "FleetPlayerData.h"

struct ActivatedAbilityManager : MonoSingleton<ActivatedAbilityManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ActivatedAbilities", "ActivatedAbilityManager">;
  static constexpr int32_t RevealHostileVgerAbilityType = 86002;
  int32_t GetActivatedAbilityType(int64_t abilityId)
  { return Call<"GetActivatedAbilityType", int32_t>(abilityId); }
  bool CanUseAbility(FleetPlayerData* fleet, int64_t abilityId)
  { return Call<"CanUseAbility", bool>(fleet, abilityId); }
};

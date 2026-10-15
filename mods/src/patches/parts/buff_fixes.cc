#include "config.h"
#include "prime_types.h"

#include <il2cpp/il2cpp.h>
#include <prime/IBuffComparer.h>
#include <prime/IBuffData.h>

#include <spud/detour.h>

static bool BuffService_IsBuffConditionMet(auto original, void* _this, BuffCondition currentCondition,
                                           IBuffComparer *buffComparer, IBuffData *buffToCompare,
                                           bool excludeFactionBuffs, bool isAllianceLoyalty)
{
  switch (currentCondition) {
    case BuffCondition::CondSelfAtStation:
      return false;

    default:
      break;
  }

  return original(_this, currentCondition, buffComparer, buffToCompare, excludeFactionBuffs, isAllianceLoyalty);
}

void InstallBuffFixHooks()
{
  using BuffService = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "BuffService">;

  if (Config::Get().use_out_of_dock_power) {
    if (auto method = BuffService::Method("IsBuffConditionMet"))
      SPUD_STATIC_DETOUR(method.Pointer(), BuffService_IsBuffConditionMet);
  }
}

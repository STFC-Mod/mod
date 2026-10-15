// use_out_of_dock_power: the ship power shown in the dock leaves out buffs that only apply while docked at the
// station, so it matches the power out in space.

#include "config.h"
#include "patches/patches.h"

#include <prime/server.h>

#include <il2cpp/il2cpp.h>
#include <spud/detour.h>

namespace
{
bool BuffService_IsBuffConditionMet(auto original, void* _this, BuffCondition currentCondition,
                                    IBuffComparer* buffComparer, IBuffData* buffToCompare, bool excludeFactionBuffs,
                                    bool isAllianceLoyalty)
{
  if (currentCondition == BuffCondition::CondSelfAtStation && Config::Get().buffs.use_out_of_dock_power)
    return false;
  return original(_this, currentCondition, buffComparer, buffToCompare, excludeFactionBuffs, isAllianceLoyalty);
}
} // namespace

void InstallBuffFixHooks()
{
  using BuffService = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "BuffService">;
  if (auto method = BuffService::Method("IsBuffConditionMet"))
    SPUD_STATIC_DETOUR(method.Pointer(), BuffService_IsBuffConditionMet);
}

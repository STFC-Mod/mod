// Buffs: use_out_of_dock_power, so the ship power shown in the dock matches the power out in space, and a guard
// against a buff list the game cannot handle.

#include "config.h"
#include "features/features.h"

#include "game/server.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>

#include <algorithm>

namespace
{
// Leaves out the buffs that only apply while docked at the station.
bool BuffService_IsBuffConditionMet_Hook(auto original, il2cpp::Object* _this, BuffCondition condition,
                                         IBuffComparer* comparer, IBuffData* buff, bool exclude_faction_buffs,
                                         bool is_alliance_loyalty)
{
  if (condition == BuffCondition::CondSelfAtStation && Config::Get().buffs.use_out_of_dock_power)
    return false;
  return original(_this, condition, comparer, buff, exclude_faction_buffs, is_alliance_loyalty);
}

// List<IBuffData> in and out, read untyped so a drifted IBuffData binding cannot make every buff look null. A list
// with a null buff crashes the game, so it is answered with no buffs instead.
il2cpp::List<il2cpp::Object*>* BuffService_ExtractBuffsOfType_Hook(auto original, ClientModifierType modifier,
                                                                   il2cpp::List<il2cpp::Object*>* list)
{ return std::ranges::contains(il2cpp::Items(list), nullptr) ? nullptr : original(modifier, list); }

bool InstallOutOfDockPower()
{
  il2cpp::HookSet hooks("OutOfDockPower");
  IL2CPP_STAGE(hooks, BuffService::Method("IsBuffConditionMet"), BuffService_IsBuffConditionMet_Hook);
  return hooks.Commit();
}

bool InstallBuffCrashGuard()
{
  il2cpp::HookSet hooks("BuffCrashGuard");
  IL2CPP_STAGE(hooks, BuffService::Method("ExtractBuffsOfType"), BuffService_ExtractBuffsOfType_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kOutOfDockPower{"OutOfDockPower", &Config::Patches::bufffixhooks, InstallOutOfDockPower};
const Feature kBuffCrashGuard{"BuffCrashGuard", &Config::Patches::tempcrashfixes, InstallBuffCrashGuard};

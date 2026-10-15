#include "config.h"
#include "patches/patches.h"

#include <il2cpp/hook.h>

#include "prime/fleet.h"
#include "prime/ui.h"

#include <spdlog/spdlog.h>

namespace
{
// Only the U.S.S. Excelsior's Decloaking Deflector is auto-confirmed. It is identified by its
// ActivatedAbilityType, RevealHostileVger, which no other free ability uses.
bool IsExcelsiorDecloakAbility(int64_t abilityId)
{
  auto* manager = ActivatedAbilityManager::Instance();
  if (manager == nullptr) {
    return false;
  }

  return manager->GetActivatedAbilityType(abilityId) == ActivatedAbilityManager::RevealHostileVgerAbilityType;
}

// Digit.Client.UI.MessageUtility.ShowFreeAbilityConfirmationPopup(long abilityId, TimeSpan cooldown,
// Action action). TimeSpan is a single 8-byte value type, so it occupies one argument slot.
void MessageUtility_ShowFreeAbilityConfirmationPopup_Hook(auto original, int64_t abilityId, int64_t cooldownTicks,
                                                          Il2CppDelegate* action)
{
  if (Config::Get().ui.auto_confirm_reveal_ability && IsExcelsiorDecloakAbility(abilityId)
      && MessageUtility::InvokeActivationAction(action)) {
    spdlog::debug("RevealConfirmation: auto-confirmed the Excelsior's decloaking ability {}", abilityId);
    return;
  }

  original(abilityId, cooldownTicks, action);
}
} // namespace

namespace
{
void InstallRevealConfirmationHooks()
{
  IL2CPP_DETOUR(MessageUtility::RevealConfirmationTarget(), MessageUtility_ShowFreeAbilityConfirmationPopup_Hook,
                "MessageUtility.ShowFreeAbilityConfirmationPopup");
}
} // namespace

const Feature kRevealConfirm{"RevealConfirm", &Config::Patches::revealconfirmhooks, InstallRevealConfirmationHooks};

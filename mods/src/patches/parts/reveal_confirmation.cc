#include "config.h"
#include "errormsg.h"

#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>

#include <prime/ActivatedAbilityManager.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

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

bool InvokeActivationAction(Il2CppDelegate* action)
{
  if (action == nullptr || action->method == nullptr) {
    return false;
  }

  Il2CppException* exception = nullptr;
  il2cpp_runtime_invoke(action->method, action->target, nullptr, &exception);

  if (exception != nullptr) {
    spdlog::warn("RevealConfirmation: failed to invoke the activation action");
    return false;
  }

  return true;
}

// Digit.Client.UI.MessageUtility.ShowFreeAbilityConfirmationPopup(long abilityId, TimeSpan cooldown,
// Action action). TimeSpan is a single 8-byte value type, so it occupies one argument slot.
void MessageUtility_ShowFreeAbilityConfirmationPopup_Hook(auto original, int64_t abilityId, int64_t cooldownTicks,
                                                          Il2CppDelegate* action)
{
  if (Config::Get().auto_confirm_reveal_ability && IsExcelsiorDecloakAbility(abilityId)
      && InvokeActivationAction(action)) {
    spdlog::debug("RevealConfirmation: auto-confirmed the Excelsior's decloaking ability {}", abilityId);
    return;
  }

  original(abilityId, cooldownTicks, action);
}
} // namespace

void InstallRevealConfirmationHooks()
{
  auto message_utility_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.UI", "MessageUtility");
  if (!message_utility_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Client.UI", "MessageUtility");
    return;
  }

  const auto* show =
      method_contract::Resolve(message_utility_helper.get_cls(), "ShowFreeAbilityConfirmationPopup", true,
                               "System.Void", {"System.Int64", "System.TimeSpan", "System.Action"});
  if (!show || !show->methodPointer || show->has_full_generic_sharing_signature) {
    ErrorMsg::MissingMethod("MessageUtility", "ShowFreeAbilityConfirmationPopup");
    return;
  }

  if (!SPUD_STATIC_DETOUR(show->methodPointer, MessageUtility_ShowFreeAbilityConfirmationPopup_Hook)) {
    spdlog::warn("RevealConfirmation: hook installation failed; the confirmation popup stays untouched");
  }
}

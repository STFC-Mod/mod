// Confirmation popups the game can skip: [ui] auto_confirm_ft_upgrade, auto_confirm_alliance_contribution and
// auto_confirm_reveal_ability. Each answers its popup's callback directly instead of showing the popup.

#include "config.h"
#include "features/features.h"

#include "game/fleet.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/core.h>

#include <string>
#include <string_view>

namespace
{
// Runs a popup's callback the way its button would. False when the callback threw: the caller then shows the
// popup, so the action is never lost silently.
template <typename... A> bool AnswerConfirmation(const char* feature, Il2CppDelegate* callback, A... args)
{
  try {
    il2cpp::InvokeBound(callback, args...);
    return true;
  } catch (...) {
    spdlog::warn("[{}] the confirmation callback threw; showing the popup", feature);
    return false;
  }
}

// --- Message box confirmations ---

// The closure `owner`.RequestAction passes as its confirmation callback, e.g.
// "Digit.Prime.ForbiddenTech.ForbiddenTechManager/<>c__DisplayClass12_0.<RequestAction>b__0". `owner` must be a whole
// class name, so another class whose name ends in it does not match.
bool IsRequestActionCallback(const Il2CppDelegate* callback, std::string_view owner)
{
  if (!callback || !callback->target || !callback->method || callback->method->parameters_count != 1)
    return false;
  const auto name  = il2cpp::Method(callback->method).Name();
  const auto found = name.find(std::string(owner) + "/");
  return found != std::string::npos && (found == 0 || name[found - 1] == '.') && name.ends_with(".<RequestAction>b__0");
}

bool ConfirmForbiddenTechUpgrade(Il2CppDelegate* callback)
{
  return Config::Get().ui.auto_confirm_ft_upgrade && IsRequestActionCallback(callback, "ForbiddenTechManager")
         && AnswerConfirmation("ForbiddenTechConfirm", callback, MessageBoxResult::Confirm);
}

bool ConfirmAllianceContribution(Il2CppDelegate* callback)
{
  return Config::Get().ui.auto_confirm_alliance_contribution
         && IsRequestActionCallback(callback, "AllianceContributionViewController")
         && AnswerConfirmation("ForbiddenTechConfirm", callback, MessageBoxResult::Yes);
}

void GameMessageBox_Show_Hook(auto original, MessageBoxContext* context)
{
  if (context && ConfirmForbiddenTechUpgrade(context->OnSelection()))
    return;
  original(context);
}

// The overload that takes its callback directly.
void GameMessageBox_ShowWithCallback_Hook(auto original, MessageBoxContext* context, Il2CppDelegate* callback)
{
  if (ConfirmForbiddenTechUpgrade(callback) || ConfirmAllianceContribution(callback))
    return;
  original(context, callback);
}

// --- Reveal ability ---

// The U.S.S. Excelsior's Decloaking Deflector is the only free ability of this type.
bool IsExcelsiorDecloak(int64_t ability_id)
{
  auto* manager = ActivatedAbilityManager::Instance();
  return manager && manager->GetActivatedAbilityType(ability_id) == ActivatedAbilityManager::kRevealHostileVger;
}

void MessageUtility_ShowFreeAbilityConfirmationPopup_Hook(auto original, int64_t ability_id, unity::TimeSpan cooldown,
                                                          Il2CppDelegate* action)
{
  if (Config::Get().ui.auto_confirm_reveal_ability && action && action->method && IsExcelsiorDecloak(ability_id)
      && AnswerConfirmation("RevealConfirm", action))
    return;
  original(ability_id, cooldown, action);
}

bool InstallForbiddenTechConfirm()
{
  il2cpp::HookSet hooks("ForbiddenTechConfirm");
  IL2CPP_STAGE(hooks, GameMessageBox::Method("Show", 1), GameMessageBox_Show_Hook);
  IL2CPP_STAGE(hooks, GameMessageBox::Method("Show", 2), GameMessageBox_ShowWithCallback_Hook);
  return hooks.Commit();
}

bool InstallRevealConfirm()
{
  il2cpp::HookSet hooks("RevealConfirm");
  IL2CPP_STAGE(
      hooks,
      MessageUtility::StaticMethod<void(int64_t, unity::TimeSpan, Il2CppDelegate*)>("ShowFreeAbilityConfirmationPopup"),
      MessageUtility_ShowFreeAbilityConfirmationPopup_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kForbiddenTechConfirm{"ForbiddenTechConfirm", &Config::Patches::forbiddentechconfirmhooks,
                                    InstallForbiddenTechConfirm};
const Feature kRevealConfirm{"RevealConfirm", &Config::Patches::revealconfirmhooks, InstallRevealConfirm};

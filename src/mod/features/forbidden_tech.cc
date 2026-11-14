// auto_confirm_ft_upgrade: answers the forbidden tech upgrade confirmation with Confirm instead of showing it.

#include "config.h"
#include "features/features.h"

#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <string>

namespace
{
// The closure ForbiddenTechManager.RequestAction passes as the confirmation callback.
bool IsForbiddenTechConfirmation(const Il2CppDelegate* callback)
{
  if (!callback || !callback->target || !callback->method || callback->method->parameters_count != 1)
    return false;
  // "Digit.Prime.ForbiddenTech.ForbiddenTechManager/<>c__DisplayClass12_0.<RequestAction>b__0", or similar.
  const auto name  = il2cpp::Method(callback->method).Name();
  const auto owner = name.find("ForbiddenTechManager/");
  return owner != std::string::npos && (owner == 0 || name[owner - 1] == '.') && name.ends_with(".<RequestAction>b__0");
}

bool IsAllianceContributionMaxLevelWarning(const Il2CppDelegate* callback)
{
  if (!callback || !callback->target || !callback->method || callback->method->parameters_count != 1)
    return false;
  const auto name  = il2cpp::Method(callback->method).Name();
  const auto owner = name.find("AllianceContributionViewController/");
  return owner != std::string::npos && (owner == 0 || name[owner - 1] == '.') && name.ends_with(".<RequestAction>b__0");
}

bool ConfirmForbiddenTechUpgrade(Il2CppDelegate* callback)
{
  if (!Config::Get().ui.auto_confirm_ft_upgrade || !IsForbiddenTechConfirmation(callback))
    return false;

  // The closure was identified above, so call its method directly. If it throws, show the dialog instead so the
  // upgrade is never lost silently.
  try {
    il2cpp::InvokeBound(callback, MessageBoxResult::Confirm);
  } catch (...) {
    spdlog::warn("ForbiddenTechConfirmation: confirmation callback threw; showing the dialog");
    return false;
  }
  return true;
}

bool ConfirmAllianceContribution(Il2CppDelegate* callback)
{
  if (!Config::Get().ui.auto_confirm_alliance_contribution || !IsAllianceContributionMaxLevelWarning(callback)) {
    return false;
  }

  try {
    il2cpp::InvokeBound(callback, MessageBoxResult::Yes);
  } catch (...) {
    spdlog::warn("AllianceContributionConfirmation: confirmation callback threw; showing the dialog");
    return false;
  }

  spdlog::debug("AllianceContributionConfirmation: auto-confirmed the max-level contribution warning");
  return true;
}

void MessageBox_Show_Hook(auto original, MessageBoxContext* context)
{
  if (!context || !ConfirmForbiddenTechUpgrade(context->OnSelection())) {
    original(context);
  }
}

void MessageBox_ShowWithCallback_Hook(auto original, MessageBoxContext* context, Il2CppDelegate* callback)
{
  if (!ConfirmForbiddenTechUpgrade(callback) && !ConfirmAllianceContribution(callback)) {
    original(context, callback);
  }
}

void InstallForbiddenTechConfirm()
{
  IL2CPP_HOOK(GameMessageBox::Method("Show", 1), MessageBox_Show_Hook);
  IL2CPP_HOOK(GameMessageBox::Method("Show", 2), MessageBox_ShowWithCallback_Hook);
}
} // namespace

const Feature kForbiddenTechConfirm{"ForbiddenTechConfirm", &Config::Patches::forbiddentechconfirmhooks,
                                    InstallForbiddenTechConfirm};

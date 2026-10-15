#include "config.h"

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <cstring>

namespace
{
constexpr int32_t ConfirmButtonResult = 1;
constexpr int32_t YesButtonResult     = 3;

using MessageBox = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "MessageBox">;

struct MessageBoxContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "MessageBoxContext">;

  // Action<MessageBox.ButtonResult>
  Il2CppDelegate* OnSelection()
  { return Field<"OnSelection", Il2CppDelegate*>(); }
};

bool IsForbiddenTechConfirmation(const Il2CppDelegate* callback)
{
  if (callback == nullptr || callback->target == nullptr || callback->method == nullptr) {
    return false;
  }

  const auto* target_class = callback->target->klass;
  const auto* owner_class  = target_class == nullptr ? nullptr : target_class->declaringType;

  return owner_class != nullptr && strcmp(owner_class->name, "ForbiddenTechManager") == 0 &&
         strcmp(callback->method->name, "<RequestAction>b__0") == 0;
}

bool IsAllianceContributionMaxLevelWarning(const Il2CppDelegate* callback)
{
  if (callback == nullptr || callback->target == nullptr || callback->method == nullptr) {
    return false;
  }

  const auto* target_class = callback->target->klass;
  const auto* owner_class  = target_class == nullptr ? nullptr : target_class->declaringType;

  return owner_class != nullptr && strcmp(owner_class->name, "AllianceContributionViewController") == 0
         && strcmp(callback->method->name, "<RequestAction>b__0") == 0;
}

bool ConfirmForbiddenTechUpgrade(Il2CppDelegate* callback)
{
  if (!Config::Get().ui.auto_confirm_ft_upgrade || !IsForbiddenTechConfirmation(callback)) {
    return false;
  }

  // The closure was identified above, so call its method directly. If it throws, show the dialog
  // instead so the upgrade is never lost silently.
  using Closure = void (*)(Il2CppObject*, int32_t, const MethodInfo*);
  if (callback->method->parameters_count != 1)
    return false;
  try {
    reinterpret_cast<Closure>(callback->method->methodPointer)(callback->target, ConfirmButtonResult,
                                                               callback->method);
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

  int32_t          result    = YesButtonResult;
  void*            args[]    = {&result};
  Il2CppException* exception = nullptr;
  il2cpp_runtime_invoke(callback->method, callback->target, args, &exception);

  if (exception != nullptr) {
    spdlog::warn("AllianceContributionConfirmation: failed to invoke the confirmation callback");
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
} // namespace

void InstallForbiddenTechConfirmationHooks()
{
  if (auto m = MessageBox::Method("Show", 1))
    SPUD_STATIC_DETOUR(m.Pointer(), MessageBox_Show_Hook);
  if (auto m = MessageBox::Method("Show", 2))
    SPUD_STATIC_DETOUR(m.Pointer(), MessageBox_ShowWithCallback_Hook);
}

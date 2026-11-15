#include "errormsg.h"
#include "config.h"

#include <il2cpp/il2cpp_helper.h>
#include <spud/detour.h>
#include <spdlog/spdlog.h>

#include <cstring>

namespace
{
constexpr int32_t ConfirmButtonResult = 1;
constexpr int32_t YesButtonResult     = 3;

const FieldInfo* on_selection_field = nullptr;

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
  if (!Config::Get().auto_confirm_ft_upgrade || !IsForbiddenTechConfirmation(callback)) {
    return false;
  }

  int32_t          result    = ConfirmButtonResult;
  void*            args[]    = {&result};
  Il2CppException* exception = nullptr;
  il2cpp_runtime_invoke(callback->method, callback->target, args, &exception);

  if (exception != nullptr) {
    spdlog::warn("ForbiddenTechConfirmation: failed to invoke the confirmation callback");
    return false;
  }

  return true;
}

bool ConfirmAllianceContribution(Il2CppDelegate* callback)
{
  if (!Config::Get().auto_confirm_alliance_contribution || !IsAllianceContributionMaxLevelWarning(callback)) {
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

Il2CppDelegate* GetSelectionCallback(void* context)
{
  if (context == nullptr || on_selection_field == nullptr) {
    return nullptr;
  }

  return *reinterpret_cast<Il2CppDelegate**>(reinterpret_cast<char*>(context) + on_selection_field->offset);
}

void MessageBox_Show_Hook(auto original, void* context)
{
  if (!ConfirmForbiddenTechUpgrade(GetSelectionCallback(context))) {
    original(context);
  }
}

void MessageBox_ShowWithCallback_Hook(auto original, void* context, Il2CppDelegate* callback)
{
  if (!ConfirmForbiddenTechUpgrade(callback) && !ConfirmAllianceContribution(callback)) {
    original(context, callback);
  }
}
} // namespace

void InstallForbiddenTechConfirmationHooks()
{
  auto message_box_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.UI", "MessageBox");
  if (!message_box_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Client.UI", "MessageBox");
    return;
  }

  auto context_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.UI", "MessageBoxContext");
  if (!context_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Client.UI", "MessageBoxContext");
    return;
  }

  on_selection_field = il2cpp_class_get_field_from_name(context_helper.get_cls(), "OnSelection");
  if (on_selection_field == nullptr) {
    spdlog::error("Unable to find field 'MessageBoxContext->OnSelection'");
    return;
  }

  const auto show = message_box_helper.GetMethod("Show", 1);
  if (show == nullptr) {
    ErrorMsg::MissingMethod("MessageBox", "Show");
  } else {
    SPUD_STATIC_DETOUR(show, MessageBox_Show_Hook);
  }

  const auto show_with_callback = message_box_helper.GetMethod("Show", 2);
  if (show_with_callback == nullptr) {
    ErrorMsg::MissingMethod("MessageBox", "Show");
  } else {
    SPUD_STATIC_DETOUR(show_with_callback, MessageBox_ShowWithCallback_Hook);
  }
}

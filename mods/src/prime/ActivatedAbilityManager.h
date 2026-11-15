#pragma once

#include "errormsg.h"
#include <il2cpp/il2cpp_helper.h>

#include "MonoSingleton.h"

// Digit.Prime.ActivatedAbilities.ActivatedAbilityManager : MonoSingleton<ActivatedAbilityManager>
struct ActivatedAbilityManager : MonoSingleton<ActivatedAbilityManager> {
  friend struct MonoSingleton<ActivatedAbilityManager>;

public:
  // Digit.PrimeServer.Models.ActivatedAbilityType.RevealHostileVger — the type of the
  // U.S.S. Excelsior's Decloaking Deflector, the only reveal ability this patch targets.
  static constexpr int32_t RevealHostileVgerAbilityType = 86002;

  int32_t GetActivatedAbilityType(int64_t abilityId)
  {
    static auto method =
        get_class_helper().GetMethod<int32_t(ActivatedAbilityManager*, int64_t)>("GetActivatedAbilityType", 1);
    static auto warn = true;
    if (method) {
      return method(this, abilityId);
    } else if (warn) {
      warn = false;
      ErrorMsg::MissingMethod("ActivatedAbilityManager", "GetActivatedAbilityType");
    }
    return 0;
  }

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.ActivatedAbilities", "ActivatedAbilityManager");
    static auto class_warn = true;
    if (!class_helper.isValidHelper()) {
      if (class_warn) {
        class_warn = false;
        ErrorMsg::MissingHelper("Digit.Prime.ActivatedAbilities", "ActivatedAbilityManager");
      }
    }
    return class_helper;
  }
};

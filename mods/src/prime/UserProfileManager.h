#pragma once

#include "MonoSingleton.h"
#include "UserProfile.h"

#include <il2cpp/il2cpp_helper.h>

struct UserProfileManager : MonoSingleton<UserProfileManager> {

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PlayerProfile", "UserProfileManager");
    return class_helper;
  }

  [[nodiscard]] UserProfile* GetLocalUserProfile()
  {
    static auto method = get_class_helper().GetInvokeMethod<UserProfile*>("GetLocalUserProfile");
    return method.Invoke(this);
  }
};

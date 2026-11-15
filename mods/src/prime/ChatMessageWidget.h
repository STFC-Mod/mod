#pragma once

#include "UserProfileWidget.h"

#include <il2cpp/il2cpp_helper.h>

struct ChatMessageWidget {
  __declspec(property(get = __get__userProfileWidget)) UserProfileWidget* _userProfileWidget;

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Chat", "ChatMessageWidget");
    return class_helper;
  }

  UserProfileWidget* __get__userProfileWidget()
  {
    static auto field = get_class_helper().GetField("_userProfileWidget").offset();
    return *(UserProfileWidget**)((uintptr_t)this + field);
  }
};

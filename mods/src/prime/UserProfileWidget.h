#pragma once

#include <il2cpp/il2cpp_helper.h>

struct PlayerTitleWidget;

struct UserProfileWidget {
  __declspec(property(get = __get__playerTitleWidget)) PlayerTitleWidget* _playerTitleWidget;

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PlayerProfile", "UserProfileWidget");
    return class_helper;
  }

  PlayerTitleWidget* __get__playerTitleWidget()
  {
    static auto field = get_class_helper().GetField("_playerTitleWidget").offset();
    return *(PlayerTitleWidget**)((uintptr_t)this + field);
  }
};

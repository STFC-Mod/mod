#pragma once

#include "SelectableList.h"

#include <il2cpp/il2cpp.h>

struct TabBar : SelectableList {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TabBar">;

  // GenericButtonContext[]
  il2cpp::Array<il2cpp::Object*>* defaultOptions()
  { return Field<"_defaultOptions", il2cpp::Array<il2cpp::Object*>*>(); }
};

struct TabBarViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TabBarViewController">;

  TabBar* tabBar()
  { return Field<"_tabBar", TabBar*>(); }
};

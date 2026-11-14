#pragma once

#include "Hub.h"
#include "NavigationManager.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct NavigationSectionManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationSectionManager">;

  static void ChangeNavigationSection(SectionID sectionId, int64_t systemId = -1)
  { Class::Call<"ChangeNavigationSection">(sectionId, systemId); }

  static NavigationSectionManager* Instance()
  { return Class::Get<"Instance", NavigationSectionManager*>(); }

  // Not NavigationManager(): that would hide the type inside this struct.
  NavigationManager* SNavigationManager()
  { return Get<"NavigationManager", NavigationManager*>(); }
};

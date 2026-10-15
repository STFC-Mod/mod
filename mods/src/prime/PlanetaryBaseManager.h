#pragma once

#include "Hub.h"
#include "MonoSingleton.h"

#include <il2cpp/il2cpp.h>

// Older clients lack this class; there the binding logs it once and ViewOwnHaven does nothing.
struct PlanetaryBaseManager : MonoSingleton<PlanetaryBaseManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlanetaryBase", "PlanetaryBaseManager">;

  static void ViewOwnHaven()
  {
    auto* manager  = Instance();
    auto* sections = Hub::get_SectionManager();
    if (!manager || !sections)
      return;

    // Preserve the game's Iconian Gateway prerequisite and its locked-building message.
    if (!manager->Call<"IsPlanetaryBaseCenterReady", bool>(true))
      return;
    // PlanetarySectionContext is reserved for visiting another player's Haven. Passing no context makes the
    // PlanetarySectionDirector initialize the complete local Haven state from the service cache.
    sections->TriggerSectionChange(SectionID::Starbase_Planetary, nullptr, false, false, true);
  }
};

#pragma once

#include "Hub.h"
#include "MonoSingleton.h"
#include <spdlog/spdlog.h>

struct PlanetaryBaseManager : MonoSingleton<PlanetaryBaseManager> {
  friend struct MonoSingleton<PlanetaryBaseManager>;

public:
  static void ViewOwnHaven()
  {
    static auto& manager_class = get_class_helper();
    static auto  is_ready =
        manager_class.GetMethod<bool(PlanetaryBaseManager*, bool)>("IsPlanetaryBaseCenterReady", 1);
    if (!manager_class.get_cls() || !is_ready) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        spdlog::warn("[ShowHaven] Planetary navigation is unavailable in this client");
      }
      return;
    }

    auto* manager  = Instance();
    auto* sections = Hub::get_SectionManager();
    if (!manager || !sections) {
      return;
    }

    // Preserve the game's Iconian Gateway prerequisite and its locked-building message.
    if (!is_ready(manager, true)) {
      return;
    }
    // PlanetarySectionContext is reserved for visiting another player's Haven. Passing no context makes the
    // PlanetarySectionDirector initialize the complete local Haven state from the service cache.
    sections->TriggerSectionChange(SectionID::Starbase_Planetary, nullptr, false, false, true);
  }

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PlanetaryBase", "PlanetaryBaseManager");
    return helper;
  }
};

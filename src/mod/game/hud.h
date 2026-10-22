#pragma once

// The HUD around the space views: the mission buttons, the faction and system panel, the alliance drawer, and the
// galactic anomaly the viewed system may have.

#include "fleet.h"
#include "ui.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

// The Q-Trials, field training, outposts and missions buttons.
struct MissionsHudViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "MissionsHudViewController">;

  // The buttons are a mix of Button and GenericButtonWidget fields; only their GameObject matters to the mod.
  template <il2cpp::Name Field> unity::Component* ButtonField()
  { return this->template Field<Field, unity::Component*>(); }
};

struct FactionInformation : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "FactionInformation">;

  // TextLocalizer components, one per system name label.
  il2cpp::List<unity::Component*>* systemName()
  { return Field<"_systemName", il2cpp::List<unity::Component*>*>(); }
};

// Galactic anomalies and navigation view data are read on their runtime classes: the mod only needs a few members,
// and these classes have moved between client builds.

// Digit.PrimeServer.Models.GalacticAnomaly
struct GalacticAnomaly : il2cpp::Object {
  bool IsActive()
  { return Get<"IsActive", bool>(); }
  TimerDataContext* EndTimeTimerDataContext()
  { return Get<"EndTimeTimerDataContext", TimerDataContext*>(); }
};

struct NavigationViewData : il2cpp::Object {
  NodeAddress* ViewingAddress()
  { return Get<"ViewingAddress", NodeAddress*>(); }
};

struct HudNavigationViewController : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation.UI", "HudNavigationViewController">;
  // The data context its canvas is registered with, for ScreenManager::View.
  using Context = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationViewData">;

  unity::Component* galacticAnomaliesButtonWidget()
  { return Field<"_galacticAnomaliesButtonWidget", unity::Component*>(); }
  FactionInformation* factionInformationHelper()
  { return Field<"_factionInformationHelper", FactionInformation*>(); }
  bool CanShowGalacticAnomaly()
  { return Call<"CanShowGalacticAnomaly", bool>(); }
  NavigationViewData* CanvasContext()
  { return Get<"CanvasContext", NavigationViewData*>(); }
};

// The upper-right drawer with Help All, Engage and Claim.
struct HudAllianceAndNewsViewController : il2cpp::Object {
  using Class   = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "HudAllianceAndNewsViewController">;
  using Context = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "HudAllianceAndNewsViewControllerContext">;

  unity::Component* chestPromotion()
  { return Field<"_chestPromotion", unity::Component*>(); }
};

struct GalacticAnomaliesManager : MonoSingleton<GalacticAnomaliesManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GalacticAnomalies", "GalacticAnomaliesManager">;

  // The singleton MonoSingleton`1 already holds, or null when there is none or it was destroyed. Instance would
  // search the scene, or create a replacement, when there is none.
  static GalacticAnomaliesManager* Current()
  {
    auto* manager = Existing();
    return unity::Alive(manager) ? manager : nullptr;
  }

  GalacticAnomaly* GetSystemGalacticAnomalies(int64_t system)
  { return Call<"GetSystemGalacticAnomalies", GalacticAnomaly*>(system); }
};

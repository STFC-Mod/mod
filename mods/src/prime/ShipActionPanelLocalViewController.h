#pragma once
#include "GenericButtonWidget.h"

struct AbilityTimeSpan { int64_t ticks; };
struct AbilityTimerDataContext : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "TimerDataContext">;
  AbilityTimeSpan RemainingTime()
  { return Get<"RemainingTime", AbilityTimeSpan>(); }
};
struct ShipActionPanelLocalViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipActionPanelLocalViewController">;
  il2cpp::Array<int64_t>* abilityIDs()
  { return Field<"_abilityIDs", il2cpp::Array<int64_t>*>(); }
  il2cpp::Array<GenericButtonWidget*>* buttons()
  { return Field<"_buttons", il2cpp::Array<GenericButtonWidget*>*>(); }
  AbilityTimerDataContext* GetRemainingCooldown(int64_t abilityId)
  { return Call<"GetRemainingCooldown", AbilityTimerDataContext*>(abilityId); }
  bool HasShipAnActiveAbilityWithActiveEffect()
  { return Call<"HasShipAnActiveAbilityWithActiveEffect", bool>(); }
};

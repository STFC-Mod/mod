#pragma once

#include "FleetPlayerData.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct ShipManagementViewContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementViewContext">;

  FleetPlayerData* CurrentFleet()
  { return Get<"CurrentFleet", FleetPlayerData*>(); }
  void Sort()
  { Call<"Sort">(); }
  int32_t CurrentIndex()
  { return Get<"CurrentIndex", int32_t>(); }
  void SetCurrentIndex(int32_t index)
  { Set<"CurrentIndex">(index); }
  void SetLastMovementWasToIncrement(bool increment)
  { Set<"LastMovementWasToIncrement">(increment); }
  int32_t ListCount()
  { return Get<"ListCount", int32_t>(); }
  il2cpp::List<FleetPlayerData*>* SortedIdleShips()
  { return Field<"SortedIdleShips", il2cpp::List<FleetPlayerData*>*>(); }
};

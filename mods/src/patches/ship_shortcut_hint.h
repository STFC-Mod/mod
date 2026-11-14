#pragma once

#include "shortcuts.h"

#include <cstddef>

// Native ship actions identify FleetPlayerData::Index. Mod selection passes a position in FleetBarContext::FleetData
// to RequestSelect instead; locked docks can leave gaps in the native indices.
template <typename FleetIndexAt>
Action ShipShortcutHintAction(Action native_action, std::size_t fleet_count, FleetIndexAt fleet_index_at)
{
  if (native_action < Action::kSelectShip1 || native_action > Action::kSelectShip8)
    return native_action;

  const auto     fleet_index    = static_cast<int>(native_action) - static_cast<int>(Action::kSelectShip1);
  constexpr auto shortcut_count = static_cast<int>(Action::kSelectShip8) - static_cast<int>(Action::kSelectShip1) + 1;
  for (std::size_t position = 0; position < fleet_count && position < shortcut_count; ++position) {
    if (fleet_index_at(position) == fleet_index)
      return static_cast<Action>(static_cast<int>(Action::kSelectShip1) + position);
  }
  return Action::kCount;
}

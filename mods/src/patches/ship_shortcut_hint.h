#pragma once

#include "shortcuts.h"

#include <cstddef>

// Native ship actions identify FleetPlayerData::Index. Mod selection passes a position in FleetBarContext::FleetData
// to RequestSelect instead; locked docks can leave gaps in the native indices.
template <typename FleetIndexAt>
GameFunction ShipShortcutHintAction(GameFunction native_action, std::size_t fleet_count, FleetIndexAt fleet_index_at)
{
  if (native_action < GameFunction::SelectShip1 || native_action > GameFunction::SelectShip8)
    return native_action;

  const auto     fleet_index    = native_action - GameFunction::SelectShip1;
  constexpr auto shortcut_count = GameFunction::SelectShip8 - GameFunction::SelectShip1 + 1;
  for (std::size_t position = 0; position < fleet_count && position < shortcut_count; ++position) {
    if (fleet_index_at(position) == fleet_index)
      return static_cast<GameFunction>(GameFunction::SelectShip1 + position);
  }
  return GameFunction::Max;
}

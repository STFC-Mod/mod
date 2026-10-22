#pragma once

#include "game/fleet.h"

namespace fleet_watch
{
struct Snapshot {
  int        slot     = -1;
  int64_t    fleet_id = 0;
  FleetState state    = FleetState::Unknown;
};

struct Transition {
  Snapshot         before;
  Snapshot         after;
  FleetPlayerData* fleet = nullptr;
};

using TransitionCallback = void (*)(const Transition& transition);
using BaselineCallback   = void (*)();

struct Subscription {
  TransitionCallback on_transition = nullptr;
  // Optional. Runs on the game thread each time a stable baseline is established while the fleet service is available
  // (after login, and again after any fleet-slot topology change). Transitions are only dispatched after a baseline, so
  // a consumer that needs the complete fleet list reads FleetsManager here.
  BaselineCallback on_baseline = nullptr;
};

// Registers a process-lifetime fleet-state observer. With no subscriptions, Fleet Watch performs no scans or IL2CPP
// work.
// Register during patch installation. Callbacks run synchronously on the game thread, and Transition::fleet is valid
// only for the duration of the callback.
bool Subscribe(Subscription subscription);

// Runs once a frame from the subscriber's frame work (fleet sync, the only subscriber).
void Tick();
} // namespace fleet_watch

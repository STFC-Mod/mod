#pragma once

struct FleetPlayerData;
enum class InstantWarpConfirmation;

// Shared by the confirmation action and its preview on the system card.
InstantWarpConfirmation ResolveInstantWarpConfirmation(FleetPlayerData* fleet);
void                    InstallWarpActionLabel();

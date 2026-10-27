#pragma once

namespace mod_settings
{
class BooleanSetting;
}
// Game-thread preference adapter for the native confirmation settings UI.
mod_settings::BooleanSetting& FleetCommanderConfirmationSetting();
void                          InvalidateFleetCommanderConfirmationSession();

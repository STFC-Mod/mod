#pragma once
#include "choice_setting.h"

namespace mod_settings
{
// Shared mutation path for the shortcut and selection control.
// Changes live behavior immediately; persistence remains best-effort and reports
// failures through the existing runtime writer. Call on the game UI thread.
void           CycleWarpMode();
ChoiceSetting& WarpModeSetting();
} // namespace mod_settings

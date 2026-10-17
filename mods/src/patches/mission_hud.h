#pragma once
namespace mission_hud
{
bool Available();
bool CanChange();
// Game-thread only. Reevaluate native visibility and apply current preferences.
void Refresh();
} // namespace mission_hud

#pragma once

#include "FleetPlayerData.h"
#include "MonoSingleton.h"

#include <il2cpp/il2cpp.h>

struct ActionQueueManager : MonoSingleton<ActionQueueManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager">;

  void ClearQueue(FleetPlayerData* playerData)
  { Call<"ClearQueue">(playerData); }
  bool IsQueueFull(FleetPlayerData* playerData)
  { return Call<"IsQueueFull", bool>(playerData); }
  bool IsFleetInQueue(FleetPlayerData* playerData)
  { return Call<"IsFleetInQueue", bool>(playerData); }
  bool IsQueueUnlocked()
  { return Call<"IsQueueUnlocked", bool>(); }

  bool CanAddToQueue(FleetPlayerData* playerData)
  { return IsQueueUnlocked() && !IsQueueFull(playerData); }

  void AddToQueue(int64_t targetId)
  { Call<"AddActionToQueue">(targetId); }
};

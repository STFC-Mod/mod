#pragma once

#include "ObjectViewerBaseWidget.h"
#include "ScanEngageButtonsWidget.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

// MiningObjectViewerWidget.OccupiedState, a private nested enum.
enum class OccupiedState : int32_t {
  NotOccupied         = 0,
  LocalPlayerOccupied = 1,
  OtherPlayerOccupied = 2
};

struct MiningObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "MiningObjectViewerWidget">;

  void MineClicked()
  { Call<"MineClicked">(); }

  OccupiedState occupiedState()
  { return Field<"_occupiedState", OccupiedState>(); }
  ScanEngageButtonsWidget* scanEngageButtonsWidget()
  { return Field<"_scanEngageButtonsWidget", ScanEngageButtonsWidget*>(); }
};

#pragma once

#include "ChatManager.h"

#include <il2cpp/il2cpp.h>

struct SwipeScroller : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SwipeScroller">;

  int32_t currentContentIndex()
  { return Field<"_currentContentIndex", int32_t>(); }
  void FocusOnInstantly(int32_t index)
  { Call<"FocusOnInstantly">(index); }
};

struct ChatPreviewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Chat", "ChatPreviewController">;

  // ImageSelector[]
  il2cpp::Array<il2cpp::Object*>* panelIndicators()
  { return Field<"_panelIndicators", il2cpp::Array<il2cpp::Object*>*>(); }
  SwipeScroller* swipeScroller()
  { return Field<"_swipeScroller", SwipeScroller*>(); }
  ChatChannelCategory focusedPanel()
  { return Field<"_focusedPanel", ChatChannelCategory>(); }
  void SetFocusedPanel(ChatChannelCategory value)
  { SetField<"_focusedPanel">(value); }
  // Set by the game after its visibility refresh for a section change.
  bool isSectionChangedCompleted()
  { return Field<"_isSectionChangedCompleted", bool>(); }
};

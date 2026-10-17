#pragma once

#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "row_style.h"
#include "settings/native_view_state.h"
#include "value_widgets.h"
#include <array>

namespace mod_settings::native
{
// Shared only by value widgets and their styling implementation. Pages and
// commands query busy state without borrowing or mutating these lifetime records.
// Stable weak records, sized once from registered controls before pages can bind: no page, context, delegate, or
// account is retained by UI bookkeeping. Records are released on native unbind and reclaimed on next bind if Unity
// destroys a widget without sending that notification.
struct ValueWidget {
  RowTint                        tint, checkTint;
  Il2CppGCHandle                 choiceBackground = nullptr, choiceOverrideBefore = nullptr, choiceCheck = nullptr;
  bool                           choiceStyled = false, pressed = false;
  Il2CppGCHandle                 widget = nullptr, context = nullptr, label = nullptr;
  Il2CppGCHandle                 selectionControl = nullptr;
  std::array<Il2CppGCHandle, 2>  indicators{};
  std::array<bool, 2>            activeBefore{};
  bool                           overridden          = false;
  bool                           hidden              = false;
  bool                           disabled            = false;
  bool                           interactableBefore  = false;
  bool                           rendering           = false;
  bool                           binding             = false;
  bool                           requesting          = false;
  bool                           clearing            = false;
  bool                           preserveNextRefresh = false;
  std::optional<NativeViewState> state;
};

} // namespace mod_settings::native

#endif

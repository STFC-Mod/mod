#pragma once

#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "interop.h"
#include "prime/Color.h"
#include <optional>
#include <string>

namespace mod_settings::native
{
struct ValueWidget;
struct RowTint {
  Il2CppGCHandle image = nullptr;
  color          before{};
};

// Scoped overrides only. Native release/rebind restores the previous appearance.
void          RestoreTint(RowTint& tint);
Il2CppObject* RowImage(Il2CppObject* widget, const char* child = "BG");
void          TintImage(RowTint& tint, Il2CppObject* image, color value, bool multiply = true);
void          TintRow(RowTint& tint, Il2CppObject* widget, color multiplier);
void          RememberChoiceSprite(Il2CppObject* background);
bool          HasPressedChoiceSprite();
void          ClearChoiceStyle(ValueWidget& view);
void          TryStyleChoice(ValueWidget& view);
void          ClearRowText(Il2CppObject* owner);
void          SetRowText(Il2CppObject* owner, Il2CppObject* label, const std::string& text, bool heading = false,
                         std::optional<bool> expanded = {});
} // namespace mod_settings::native

#endif

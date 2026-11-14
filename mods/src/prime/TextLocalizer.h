#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

// Digit.Client.UI.TextLocalizer: the label component that renders localized, parameterized text.
struct TextLocalizer : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TextLocalizer">;

  // Declared on GraphicLocalizer<,>.
  Il2CppString* identifier()
  { return Field<"m_identifier", Il2CppString*>(); }
  // The backing field, not the SignificantDecimals property.
  int32_t significantDecimals()
  { return Field<"m_significantDecimals", int32_t>(); }
  void SetSignificantDecimals(int32_t value)
  { SetField<"m_significantDecimals">(value); }
  bool textOverride()
  { return Field<"_textOverride", bool>(); }
  // Declared on GraphicLocalizer<,>: the rendered graphic, resolved lazily even on inactive labels.
  il2cpp::Object* Target()
  { return Get<"Target", il2cpp::Object*>(); }

  Il2CppString* CurrentText()
  { return Call<"CurrentText", Il2CppString*>(); }
  void ClearTextOverride()
  { Call<"ClearTextOverride">(); }
  void OverrideLocalizedText(Il2CppString* text)
  { Call<"OverrideLocalizedText">(text); }
};

struct ColourTextLocalizer : TextLocalizer {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ColourTextLocalizer">;
};

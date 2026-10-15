#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

// Identifier + category pair that the localizer turns into display text.
// Construct with il2cpp::New<LocaleTextContext>(identifier, category).
struct LocaleTextContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "LocaleTextContext">;

  // Fills the {N} slots of the identifier itself, e.g. "ship_name_{0}".
  void ApplyIdentifierParameters(il2cpp::Array<Il2CppObject*>* parameters)
  { Call<"ApplyIdentifierParameters">(parameters); }
};

struct LocaleUtilities {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "LocaleUtilities">;

  static Il2CppString* Localize(LocaleTextContext* context, bool localizeTextParams = false,
                                bool invariantCulture = false)
  { return Class::Call<"Localize", Il2CppString*>(context, localizeTextParams, invariantCulture); }
};

struct Localizer {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "Localizer">;

  // textCase is TextLocalizer.TextCase; 0 leaves the text as translated.
  static Il2CppString* Localise(LocaleTextContext* context, bool forceCommas = false, int32_t textCase = 0)
  { return Class::Call<"Localise", Il2CppString*>(context, forceCommas, textCase); }
};

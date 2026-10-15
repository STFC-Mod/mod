#pragma once

#include "LocaleTextContext.h"
#include "MonoSingleton.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct LanguageManager : MonoSingleton<LanguageManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "LanguageManager">;

  bool TryGetTranslation(Il2CppString* category, int32_t key, Il2CppString** translatedText)
  {
    if (!translatedText)
      return false;
    return Call<"TryGetTranslation", bool(Il2CppString*, int32_t, Il2CppString*&)>(category, key, *translatedText);
  }

  bool Localize(Il2CppString*& translation, LocaleTextContext* context)
  { return Call<"Localize", bool(Il2CppString*&, LocaleTextContext*)>(translation, context); }

  static void ClearCache()
  { Class::Call<"ClearCache">(); }
};

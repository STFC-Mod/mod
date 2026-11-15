#include "config.h"
#include "errormsg.h"

#include <il2cpp/il2cpp_helper.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

// Overrides m_significantDecimals on cargo ColourTextLocalizer instances
// (identifier "shared_x_of_y_x_coloured"). The field is set permanently in
// SetLocalTextParameters because Localize(), which reads it, runs later.

static constexpr char  kCargoIdentifier[]   = "shared_x_of_y_x_coloured";
static constexpr size_t kCargoIdentifierLen = sizeof(kCargoIdentifier) - 1;

void ColourTextLocalizer_SetLocalTextParameters_Hook(auto original, void* _this, bool parseID, void* args)
{
  if (!_this || !Config::Get().format_cargo_values) {
    original(_this, parseID, args);
    return;
  }

  int desired_decimals = Config::Get().cargo_significant_decimals;
  if (desired_decimals < 0) desired_decimals = 0;
  if (desired_decimals > 6) desired_decimals = 6;

  static auto text_localizer_h =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.UI", "TextLocalizer");
  static auto identifier_field = text_localizer_h.GetField("m_identifier");
  static auto sig_decimals_field = text_localizer_h.GetField("m_significantDecimals");

  if (!identifier_field.isValidHelper() || !sig_decimals_field.isValidHelper()) {
    original(_this, parseID, args);
    return;
  }

  auto* identifier_ptr = *reinterpret_cast<Il2CppString**>((char*)_this + identifier_field.offset());
  if (!identifier_ptr) {
    original(_this, parseID, args);
    return;
  }

  // Fast path: compare length then raw UTF-16 chars (ASCII, so low byte only).
  // Avoids std::string allocation — SetLocalTextParameters fires for every
  // ColourTextLocalizer in the UI, not just cargo.
  if ((size_t)identifier_ptr->length != kCargoIdentifierLen) {
    original(_this, parseID, args);
    return;
  }
  const Il2CppChar* chars = identifier_ptr->chars;
  for (size_t i = 0; i < kCargoIdentifierLen; ++i) {
    if ((char)chars[i] != kCargoIdentifier[i]) {
      original(_this, parseID, args);
      return;
    }
  }

  auto* sig_decimals_ptr = reinterpret_cast<int*>((char*)_this + sig_decimals_field.offset());
  if (*sig_decimals_ptr != desired_decimals) {
    *sig_decimals_ptr = desired_decimals;
  }

  original(_this, parseID, args);
}

void InstallCargoFormatHooks()
{
  auto colour_text_localizer_h =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.UI", "ColourTextLocalizer");
  if (!colour_text_localizer_h.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Client.UI", "ColourTextLocalizer");
    return;
  }

  auto ptr = colour_text_localizer_h.GetMethod("SetLocalTextParameters");
  if (!ptr) {
    ErrorMsg::MissingMethod("ColourTextLocalizer", "SetLocalTextParameters");
    return;
  }

  SPUD_STATIC_DETOUR(ptr, ColourTextLocalizer_SetLocalTextParameters_Hook);
  int applied = Config::Get().cargo_significant_decimals;
  if (applied < 0) applied = 0;
  if (applied > 6) applied = 6;
  spdlog::info("Cargo format: significant decimals = {}", applied);
}

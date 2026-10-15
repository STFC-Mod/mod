#include "config.h"
#include "prime/ui.h"

#include <spdlog/spdlog.h>
#include <spud/detour.h>

// Overrides m_significantDecimals on cargo ColourTextLocalizer instances
// (identifier "shared_x_of_y_x_coloured"). The field is set permanently in
// SetLocalTextParameters because Localize(), which reads it, runs later.

static constexpr char  kCargoIdentifier[]   = "shared_x_of_y_x_coloured";
static constexpr size_t kCargoIdentifierLen = sizeof(kCargoIdentifier) - 1;

void ColourTextLocalizer_SetLocalTextParameters_Hook(auto original, ColourTextLocalizer* _this, bool parseID, void* args)
{
  if (!_this || !Config::Get().format_cargo_values) {
    original(_this, parseID, args);
    return;
  }

  int desired_decimals = Config::Get().cargo_significant_decimals;
  if (desired_decimals < 0) desired_decimals = 0;
  if (desired_decimals > 6) desired_decimals = 6;

  auto* identifier_ptr = _this->identifier();
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

  _this->SetSignificantDecimals(desired_decimals);

  original(_this, parseID, args);
}

void InstallCargoFormatHooks()
{
  auto method = ColourTextLocalizer::Class::Method("SetLocalTextParameters");
  if (!method) {
    return;
  }

  SPUD_STATIC_DETOUR(method.Pointer(), ColourTextLocalizer_SetLocalTextParameters_Hook);
  int applied = Config::Get().cargo_significant_decimals;
  if (applied < 0) applied = 0;
  if (applied > 6) applied = 6;
  spdlog::info("Cargo format: significant decimals = {}", applied);
}

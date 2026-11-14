// Cargo labels: [ui] format_cargo_values and cargo_significant_decimals, the precision of abbreviated totals (1.25M),
// and on Windows [ui] instant_cargo_counter, which shows the real total at once instead of the count-up animation.

#include "config.h"
#include "features/features.h"

#include "game/server.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string_view>

namespace
{
// The localization identifier of the "X of Y" cargo label.
constexpr std::string_view kCargoIdentifier = "shared_x_of_y_x_coloured";

// --- Cargo format ---

// Localize() reads m_significantDecimals after SetLocalTextParameters, so setting it first covers the label's
// next render.
void ColourTextLocalizer_SetLocalTextParameters_Hook(auto original, ColourTextLocalizer* _this, bool parse_id,
                                                     void* args)
{
  const auto& ui = Config::Get().ui;
  if (ui.format_cargo_values && _this->IdentifierIs(kCargoIdentifier))
    _this->SetSignificantDecimals(std::clamp(ui.cargo_significant_decimals, 0, 6));
  original(_this, parse_id, args);
}

// --- Instant cargo counter ---
//
// TODO: Windows only. Enabling it on macOS needs SetWidgetData's prologue checked against SPUD's detour size.
#if _WIN32
bool IsCargoText(LegacyComparableProgressBar* bar)
{
  auto* label = bar->currentValue_X_of_Y();
  if (!label || !label->IdentifierIs(kCargoIdentifier))
    return false;
  auto* context = bar->context();
  if (context && context->klass == CargoProgressData::Raw())
    return true;
  static bool reported = false;
  if (!reported) {
    reported = true;
    spdlog::warn("[InstantCargo] the cargo label shows a {}, not cargo; keeping the game's count-up",
                 context ? il2cpp::ClassName(context->klass) : "null");
  }
  return false;
}

void LegacyComparableProgressBar_SetWidgetData_Hook(auto original, LegacyComparableProgressBar* _this)
{
  if (!Config::Get().ui.instant_cargo_counter || !IsCargoText(_this)) {
    original(_this);
    return;
  }
  // Render the current total through the game's own formatting with the prefab's snap flag set, then restore
  // the flag so reward updates and interpolation carry on normally.
  const bool previous = _this->snapToValue();
  _this->SetSnapToValue(true);
  original(_this);
  _this->SetSnapToValue(previous);
}

bool InstallInstantCargo()
{
  auto method = LegacyComparableProgressBar::Class::Method("SetWidgetData", 0);
  // The lookup walks parents; only detour the bar's own override, never the shared Widget`1.SetWidgetData.
  if (method && method.Info()->klass != LegacyComparableProgressBar::Class::Raw()) {
    spdlog::warn("[InstantCargo] the progress bar no longer overrides SetWidgetData; not hooking the shared one");
    return false;
  }
  il2cpp::HookSet hooks("InstantCargo");
  IL2CPP_STAGE(hooks, method, LegacyComparableProgressBar_SetWidgetData_Hook);
  return hooks.Commit();
}
#endif

bool InstallCargoFormat()
{
  il2cpp::HookSet hooks("CargoFormat");
  IL2CPP_STAGE(hooks, ColourTextLocalizer::Class::Method("SetLocalTextParameters"),
               ColourTextLocalizer_SetLocalTextParameters_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kCargoFormat{"CargoFormat", &Config::Patches::cargoformathooks, InstallCargoFormat};
#if _WIN32
const Feature kInstantCargo{"InstantCargo", &Config::Patches::instantcargocounterhooks, InstallInstantCargo};
#endif

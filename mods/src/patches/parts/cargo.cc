// Cargo labels: the precision of abbreviated totals (1.25M) and showing the real total at once instead of the
// count-up animation.

#include "config.h"
#include "patches/patches.h"

#include <prime/ui.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>

namespace
{
// The localization identifier of the "X of Y" cargo label.
constexpr std::string_view kCargoIdentifier = "shared_x_of_y_x_coloured";

int SignificantDecimals()
{ return std::clamp(Config::Get().ui.cargo_significant_decimals, 0, 6); }

// m_significantDecimals is read by Localize(), which runs after SetLocalTextParameters, so set it here for good.
void ColourTextLocalizer_SetLocalTextParameters_Hook(auto original, ColourTextLocalizer* _this, bool parseID,
                                                     void* args)
{
  if (_this && Config::Get().ui.format_cargo_values && _this->IdentifierIs(kCargoIdentifier))
    _this->SetSignificantDecimals(SignificantDecimals());
  original(_this, parseID, args);
}

#if defined(_WIN32) && defined(_M_X64)
using CargoProgressData =
    il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CargoProgressData">;

// Fields below are declared on LegacyProgressBar and Widget`1; lookups walk the parents.
struct LegacyComparableProgressBar : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Stats", "LegacyComparableProgressBar">;

  TextLocalizer* currentValue_X_of_Y()
  { return Field<"_currentValue_X_of_Y", TextLocalizer*>(); }
  // IProgressData
  il2cpp::Object* context()
  { return Field<"m_context", il2cpp::Object*>(); }
  bool snapToValue()
  { return Field<"_snapToValue", bool>(); }
  void SetSnapToValue(bool value)
  { SetField<"_snapToValue">(value); }
};

bool IsCargoText(LegacyComparableProgressBar* self)
{
  auto* label = self->currentValue_X_of_Y();
  if (!label || !label->IdentifierIs(kCargoIdentifier))
    return false;
  auto* context = self->context();
  if (!context || context->klass != CargoProgressData::Raw()) {
    static bool reported = false;
    if (!reported) {
      reported = true;
      spdlog::warn("[InstantCargoText] cargo label has unexpected context {}; native text retained",
                   context ? il2cpp_class_get_name(context->klass) : "null");
    }
    return false;
  }
  return true;
}

void SetWidgetData_Hook(auto original, LegacyComparableProgressBar* self)
{
  if (!Config::Get().ui.instant_cargo_counter || !self || !IsCargoText(self)) {
    original(self);
    return;
  }
  // Render the current total through the game's own formatting with the prefab's snap flag set, then restore
  // the flag so reward updates and interpolation carry on normally.
  const bool previous = self->snapToValue();
  self->SetSnapToValue(true);
  original(self);
  self->SetSnapToValue(previous);
}
#endif
} // namespace

void InstallCargoFormatHooks()
{
  if (auto method = ColourTextLocalizer::Class::Method("SetLocalTextParameters")) {
    SPUD_STATIC_DETOUR(method.Pointer(), ColourTextLocalizer_SetLocalTextParameters_Hook);
    spdlog::info("Cargo format: significant decimals = {}", SignificantDecimals());
  }
}

void InstallInstantCargoCounterHooks()
{
  // TODO: Windows x64 only. Enabling it on macOS needs SetWidgetData's prologue checked against SPUD's detour size.
#if defined(_WIN32) && defined(_M_X64)
  auto method = LegacyComparableProgressBar::Class::Method("SetWidgetData", 0);
  // The lookup walks parents; only detour the bar's own override, never the shared Widget`1.SetWidgetData.
  if (!method || method.Info()->klass != LegacyComparableProgressBar::Class::Raw())
    return;
  const bool installed = SPUD_STATIC_DETOUR(method.Pointer(), SetWidgetData_Hook) != nullptr;
  spdlog::info("[InstantCargoText] native text hook {}", installed ? "installed" : "unavailable");
#else
  spdlog::info("[InstantCargoText] native text snap is not supported on this platform");
#endif
}

// Show actual cargo totals through the native text renderer, without its count-up animation.
#include "config.h"
#include "prime/TextLocalizer.h"

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>
#if defined(_WIN32) && defined(_M_X64)
namespace
{
using CargoProgressData = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CargoProgressData">;

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
  auto*          label      = self->currentValue_X_of_Y();
  auto*          identifier = label ? label->identifier() : nullptr;
  constexpr char name[]     = "shared_x_of_y_x_coloured";
  if (!identifier || il2cpp_string_length(identifier) != sizeof(name) - 1)
    return false;
  auto* chars = il2cpp_string_chars(identifier);
  for (size_t i = 0; i < sizeof(name) - 1; ++i)
    if (chars[i] != name[i])
      return false;
  auto* context = self->context();
  if (!context || context->klass != CargoProgressData::Raw()) {
    static bool reported{};
    if (!reported) {
      reported = true;
      spdlog::warn("[InstantCargoText] cargo label has unexpected context {}; native text retained",
                   context ? il2cpp_class_get_name(context->klass) : "null");
    }
    return false;
  }
  return true;
}
struct SnapScope {
  LegacyComparableProgressBar* object;
  bool                         previous;
  explicit SnapScope(LegacyComparableProgressBar* self)
      : object(self)
      , previous(self->snapToValue())
  { object->SetSnapToValue(true); }
  ~SnapScope()
  { object->SetSnapToValue(previous); }
};
void SetWidgetData_Hook(auto original, LegacyComparableProgressBar* self)
{
  if (!Config::Get().instant_cargo_counter || !self || !IsCargoText(self)) {
    original(self);
    return;
  }
  // Render the current model total using the game's own formatting/colour path.
  // Restore the prefab flag before returning; reward updates and interpolation lifecycle continue normally.
  SnapScope scope(self);
  original(self);
  static bool reported{};
  if (!reported) {
    reported = true;
    spdlog::info("[InstantCargoText] matched cargo text; native snap active");
  }
}
} // namespace
#endif
void InstallInstantCargoCounterHooks()
{
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

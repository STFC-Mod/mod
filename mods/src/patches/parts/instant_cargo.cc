// Show actual cargo totals through the native text renderer, without its count-up animation.
#include "config.h"
#include <il2cpp-tabledefs.h>
#include <il2cpp/il2cpp_helper.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>
#if defined(_WIN32) && defined(_M_X64)
#include <cstring>
namespace
{
FieldInfo *  textField{}, *contextField{}, *identifierField{}, *snapField{};
Il2CppClass* cargoDataClass{};
bool         textReady{};
// Metadata names and storage are checked before reading game-owned fields.
bool IsType(const Il2CppType* type, const char* name)
{
  if (!type || type->byref)
    return false;
  auto*      actual  = il2cpp_type_get_name(type);
  const bool matches = actual && std::strcmp(actual, name) == 0;
  il2cpp_free(actual);
  return matches;
}

FieldInfo* Field(Il2CppClass* cls, const char* name, const char* type, size_t bytes)
{
  for (auto* parent = cls; parent; parent = il2cpp_class_get_parent(parent)) {
    auto* field = il2cpp_class_get_field_from_name(parent, name);
    if (field && !(il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC) && field->type && !field->type->byref
        && field->offset >= sizeof(Il2CppObject) && field->offset + bytes <= il2cpp_class_instance_size(cls)
        && IsType(field->type, type))
      return field;
  }
  return nullptr;
}
template <typename T> T Read(void* self, FieldInfo* field)
{
  T value{};
  if (self && field)
    il2cpp_field_get_value(static_cast<Il2CppObject*>(self), field, &value);
  return value;
}
bool IsCargoText(void* self)
{
  auto*          label      = Read<Il2CppObject*>(self, textField);
  auto*          identifier = Read<Il2CppString*>(label, identifierField);
  constexpr char name[]     = "shared_x_of_y_x_coloured";
  if (!identifier || il2cpp_string_length(identifier) != sizeof(name) - 1)
    return false;
  auto* chars = il2cpp_string_chars(identifier);
  for (size_t i = 0; i < sizeof(name) - 1; ++i)
    if (chars[i] != name[i])
      return false;
  auto* context = Read<Il2CppObject*>(self, contextField);
  if (!context || il2cpp_object_get_class(context) != cargoDataClass) {
    static bool reported{};
    if (!reported) {
      reported = true;
      spdlog::warn("[InstantCargoText] cargo label has unexpected context {}; native text retained",
                   context ? il2cpp_class_get_name(il2cpp_object_get_class(context)) : "null");
    }
    return false;
  }
  return true;
}
struct SnapScope {
  Il2CppObject* object;
  bool          previous;
  explicit SnapScope(void* self)
      : object(static_cast<Il2CppObject*>(self))
      , previous(Read<bool>(self, snapField))
  {
    bool snap = true;
    il2cpp_field_set_value(object, snapField, &snap);
  }
  ~SnapScope()
  { il2cpp_field_set_value(object, snapField, &previous); }
};
void SetWidgetData_Hook(auto original, void* self)
{
  if (!textReady || !self || !Config::Get().instant_cargo_counter || !IsCargoText(self)) {
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
  auto bar  = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Stats", "LegacyComparableProgressBar");
  auto text = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.UI", "TextLocalizer");
  auto data = il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CargoProgressData");
  auto* cls = bar.get_cls();
  cargoDataClass  = data.get_cls();
  textField       = Field(cls, "_currentValue_X_of_Y", "Digit.Client.UI.TextLocalizer", sizeof(void*));
  contextField    = Field(cls, "m_context", "Digit.PrimeServer.Models.IProgressData", sizeof(void*));
  identifierField = Field(text.get_cls(), "m_identifier", "System.String", sizeof(void*));
  snapField       = Field(cls, "_snapToValue", "System.Boolean", sizeof(bool));
  auto* method    = bar.GetMethodInfo("SetWidgetData", 0);
  if (!cargoDataClass || !textField || !contextField || !identifierField || !snapField || !method
      || method->klass != cls || !method->methodPointer || method->is_generic || method->is_inflated
      || (method->flags & METHOD_ATTRIBUTE_STATIC) || method->parameters_count != 0
      || !IsType(method->return_type, "System.Void")) {
    spdlog::warn("[InstantCargoText] required metadata unavailable; native text retained");
    return;
  }
  textReady = SPUD_STATIC_DETOUR(method->methodPointer, SetWidgetData_Hook) != nullptr;
  spdlog::info("[InstantCargoText] native text hook {}", textReady ? "installed" : "unavailable");
#else
  spdlog::info("[InstantCargoText] native text snap is not supported on this platform");
#endif
}

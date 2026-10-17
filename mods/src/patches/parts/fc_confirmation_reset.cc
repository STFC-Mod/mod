#include "fc_confirmation_reset.h"
#include "settings/boolean_settings.h"
#include <il2cpp-tabledefs.h>
#include <il2cpp/il2cpp_helper.h>

namespace
{
using namespace mod_settings;
constexpr const char* PreferenceKey = "options/hide_fcaa_use_confirmation";

bool ReferenceField(const FieldInfo* field)
{
  if (!field || field->type->byref)
    return false;
  const auto type = field->type->type;
  return type == IL2CPP_TYPE_CLASS || type == IL2CPP_TYPE_GENERICINST || type == IL2CPP_TYPE_OBJECT;
}
Il2CppObject* ExistingSingleton(IL2CppClassHelper& helper)
{
  auto  parent           = helper.GetParent("MonoSingleton`1");
  auto* cls              = parent.get_cls();
  auto* instanceField    = cls ? il2cpp_class_get_field_from_name(cls, "s_instance") : nullptr;
  auto* initializedField = cls ? il2cpp_class_get_field_from_name(cls, "s_initialized") : nullptr;
  if (!ReferenceField(instanceField) || !(instanceField->type->attrs & FIELD_ATTRIBUTE_STATIC) || !initializedField
      || initializedField->type->type != IL2CPP_TYPE_BOOLEAN
      || !(initializedField->type->attrs & FIELD_ATTRIBUTE_STATIC))
    return nullptr;
  bool initialized = false;
  il2cpp_field_static_get_value(initializedField, &initialized);
  if (!initialized)
    return nullptr;
  Il2CppObject* instance = nullptr;
  il2cpp_field_static_get_value(instanceField, &instance);
  return instance && il2cpp_class_is_assignable_from(helper.get_cls(), instance->klass) ? instance : nullptr;
}
bool BoolField(const FieldInfo* field)
{
  return field && field->type->type == IL2CPP_TYPE_BOOLEAN && !field->type->byref
         && !(field->type->attrs & FIELD_ATTRIBUTE_STATIC);
}
bool InstanceMethod(const MethodInfo* method, int count, int result)
{
  return method && method->parameters_count == count && method->return_type->type == result
         && !method->return_type->byref && !(method->flags & METHOD_ATTRIBUTE_STATIC);
}

struct Binding {
  IL2CppClassHelper prefs =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PersistentPrefs", "PersistentPrefsManager");
  IL2CppClassHelper fc =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.FleetCommander", "FleetCommanderManager");
  const MethodInfo* get = prefs.GetMethodInfo("GetBool", 3);
  const MethodInfo* set = fc.GetMethodInfo("set_HideFleetCommanderAbilityUseConfirmationHidden", 1);
  FieldInfo*        loaded =
      prefs.get_cls() ? il2cpp_class_get_field_from_name(prefs.get_cls(), "_isCloudFileLoaded") : nullptr;
  FieldInfo* loading = prefs.get_cls() ? il2cpp_class_get_field_from_name(prefs.get_cls(), "_isLoading") : nullptr;
  FieldInfo* saved   = prefs.get_cls() ? il2cpp_class_get_field_from_name(prefs.get_cls(), "_savedData") : nullptr;
  // Two bounded weak roots track storage replacement without retaining account data.
  // Cleared on invalidation while IL2CPP is live, not in a process-exit destructor.
  Il2CppGCHandle owner = nullptr, data = nullptr, key = nullptr;
  std::uint64_t  generation = 1;
  bool           supported() const
  {
    return BoolField(loaded) && BoolField(loading) && ReferenceField(saved)
           && !(saved->type->attrs & FIELD_ATTRIBUTE_STATIC) && InstanceMethod(get, 3, IL2CPP_TYPE_BOOLEAN)
           && get->parameters[0]->type == IL2CPP_TYPE_STRING && !get->parameters[0]->byref
           && get->parameters[1]->type == IL2CPP_TYPE_BOOLEAN && !get->parameters[1]->byref
           && get->parameters[2]->type == IL2CPP_TYPE_BOOLEAN && !get->parameters[2]->byref
           && InstanceMethod(set, 1, IL2CPP_TYPE_VOID) && set->parameters[0]->type == IL2CPP_TYPE_BOOLEAN
           && !set->parameters[0]->byref;
  }
  void invalidate()
  {
    if (owner)
      il2cpp_gchandle_free(owner);
    if (data)
      il2cpp_gchandle_free(data);
    owner = data = nullptr;
    ++generation;
  }
  Il2CppObject* ready()
  {
    auto*         p        = ExistingSingleton(prefs);
    bool          isLoaded = false, isLoading = true;
    Il2CppObject* state = nullptr;
    if (p) {
      il2cpp_field_get_value(p, loaded, &isLoaded);
      il2cpp_field_get_value(p, loading, &isLoading);
      il2cpp_field_get_value(p, saved, &state);
    }
    if (!p || !state || !isLoaded || isLoading) {
      if (owner || data)
        invalidate();
      return nullptr;
    }
    if (!owner || !data || il2cpp_gchandle_get_target(owner) != p || il2cpp_gchandle_get_target(data) != state) {
      invalidate();
      owner = il2cpp_gchandle_new_weakref(p, false);
      data  = il2cpp_gchandle_new_weakref(state, false);
      if (!owner || !data) {
        invalidate();
        return nullptr;
      }
    }
    return p;
  }
};
Binding& Backend()
{
  static Binding binding;
  return binding;
}

ReadResult ReadConfirmation()
{
  auto& b = Backend();
  if (!b.supported())
    return {Availability::Unsupported};
  auto* prefs = b.ready();
  if (!prefs)
    return {};
  if (!b.key)
    b.key = il2cpp_gchandle_new(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(PreferenceKey)), false);
  if (!b.key)
    return {};
  // FC's native property uses GetBool(key,false,true). Display reads must not insert
  // a missing preference: preserve its default but explicitly pass shouldAddKey=false.
  bool             defaultHidden = false, addKey = false;
  void*            args[]    = {il2cpp_gchandle_get_target(b.key), &defaultHidden, &addKey};
  Il2CppException* exception = nullptr;
  auto*            value     = il2cpp_runtime_invoke(b.get, prefs, args, &exception);
  if (exception || !value)
    return {};
  return ReadResult::Known(!*static_cast<bool*>(il2cpp_object_unbox(value)), b.generation);
}
ApplyResult WriteConfirmation(bool enabled, std::uint64_t expectedGeneration)
{
  auto& b = Backend();
  if (!b.supported() || !b.ready() || b.generation != expectedGeneration)
    return ApplyResult::Rejected;
  auto* manager = ExistingSingleton(b.fc);
  if (!manager)
    return ApplyResult::Rejected;
  bool             hidden    = !enabled;
  void*            args[]    = {&hidden};
  Il2CppException* exception = nullptr;
  il2cpp_runtime_invoke(b.set, manager, args, &exception);
  return exception ? ApplyResult::Unverified : ApplyResult::Applied;
}
} // namespace

mod_settings::BooleanSetting& FleetCommanderConfirmationSetting()
{
  static mod_settings::BooleanSetting setting({"community_mod.fc_ability_confirmation",
                                               "[MOD] Confirm Fleet Commander abilities", ReadConfirmation,
                                               WriteConfirmation});
  return setting;
}

void InvalidateFleetCommanderConfirmationSession()
{
  // The native UI calls this before preference-session boundaries.
  FleetCommanderConfirmationSetting().InvalidateSession();
  Backend().invalidate();
}

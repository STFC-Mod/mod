#pragma once
// Alliance Games-only checked runtime operations. No replacement binding engine.
#include <array>
#include <cstring>
#include <il2cpp/method_contract.h>
#include <il2cpp/runtime.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace alliance_store_game
{
using Object = Il2CppObject;
template <class T> struct Result {
  bool     succeeded = false;
  T        value{};
  explicit operator bool() const
  { return succeeded; }
};
template <> struct Result<void> {
  bool     succeeded = false;
  explicit operator bool() const
  { return succeeded; }
};
class Handle
{
public:
  Handle()                         = default;
  Handle(const Handle&)            = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr))
  {
  }
  Handle& operator=(Handle&& other) noexcept
  {
    if (this != &other) {
      Reset();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  ~Handle()
  { Reset(); }
  static Handle Strong(Object* value)
  { return Make(value, false); }
  static Handle Weak(Object* value)
  { return Make(value, true); }
  Object* Get() const
  { return handle_ ? il2cpp_gchandle_get_target(handle_) : nullptr; }
  template <class T> T* Get() const
  { return reinterpret_cast<T*>(Get()); }
  explicit operator bool() const
  { return handle_ != nullptr; }
  void Reset()
  {
    if (handle_)
      il2cpp_gchandle_free(std::exchange(handle_, nullptr));
  }

private:
  static Handle Make(Object* value, bool weak)
  {
    Handle result;
    if (value)
      result.handle_ = weak ? il2cpp_gchandle_new_weakref(value, false) : il2cpp_gchandle_new(value, false);
    if (result.Get() != value)
      result.Reset();
    return result;
  }
  Il2CppGCHandle handle_ = nullptr;
};
struct Roots {
  std::vector<Handle> values;
  bool                healthy = true;
  Object*             Keep(Object* value)
  {
    if (!value)
      return nullptr;
    auto root = Handle::Strong(value);
    if (!root || root.Get() != value) {
      healthy = false;
      return nullptr;
    }
    values.push_back(std::move(root));
    return value;
  }
};
inline Il2CppClass* Class(const char* space, const char* name, const char* assembly = "Assembly-CSharp")
{ return il2cpp_get_class_helper(assembly, space, name).get_cls(); }
inline Il2CppClass* TooltipClass()
{ return Class("Digit.Prime.Inventories", "InventoryTooltipTriggerWidget"); }
inline Il2CppClass* TriggerClass()
{ return Class("Digit.Client.Tooltip", "TooltipTrigger"); }
inline Il2CppClass* ActionClass()
{ return Class("Digit.Client.Tooltip", "TooltipActionTrigger"); }
inline bool Reference(const Il2CppType* type)
{
  auto* cls = type && !type->byref ? il2cpp_class_from_type(type) : nullptr;
  return cls && !il2cpp_class_is_valuetype(cls)
         && (type->type == IL2CPP_TYPE_CLASS || type->type == IL2CPP_TYPE_OBJECT || type->type == IL2CPP_TYPE_STRING
             || type->type == IL2CPP_TYPE_GENERICINST || type->type == IL2CPP_TYPE_SZARRAY
             || type->type == IL2CPP_TYPE_ARRAY);
}
struct Vector2 {
  float x, y;
};
struct OverridableBool {
  bool serialized, current, modified;
};
static_assert(sizeof(OverridableBool) == 3);
template <class T> bool ValueType(const Il2CppType* type)
{
  if (!type || type->byref)
    return false;
  if constexpr (std::is_same_v<T, void>)
    return type->type == IL2CPP_TYPE_VOID;
  else if constexpr (std::is_pointer_v<T>)
    return Reference(type);
  else if constexpr (std::is_same_v<T, bool>)
    return type->type == IL2CPP_TYPE_BOOLEAN;
  else if constexpr (std::is_same_v<T, int32_t>)
    return type->type == IL2CPP_TYPE_I4;
  else if constexpr (std::is_same_v<T, int64_t>)
    return type->type == IL2CPP_TYPE_I8;
  else if constexpr (std::is_same_v<T, float>)
    return type->type == IL2CPP_TYPE_R4;
  else {
    const char* name =
        std::is_same_v<T, Vector2> ? "UnityEngine.Vector2" : "Digit.Client.Serialization.OverridableBool";
    uint32_t alignment = 0;
    auto*    cls       = il2cpp_class_from_type(type);
    return method_contract::Type(type, name) && cls && il2cpp_class_is_valuetype(cls)
           && il2cpp_class_value_size(cls, &alignment) == sizeof(T);
  }
}
struct Argument {
  void*       value;
  const char* scalar = nullptr;
};
inline Argument Ref(Object* value)
{ return {value}; }
template <class T> Argument Scalar(T& value, const char* name)
{ return {&value, name}; }
// Inherited/closed List and Widget accessors retain their concrete runtime metadata.
template <class T> bool Signature(const MethodInfo* method, bool is_static, std::initializer_list<Argument> args)
{
  if (!method || method->is_generic || !method->invoker_method
      || bool(method->flags & METHOD_ATTRIBUTE_STATIC) != is_static || method->parameters_count != args.size()
      || !ValueType<T>(method->return_type))
    return false;
  size_t index = 0;
  for (const auto& arg : args) {
    const auto* type = method->parameters ? method->parameters[index++] : nullptr;
    if (arg.scalar) {
      if (!method_contract::Type(type, arg.scalar))
        return false;
    } else {
      auto* value = static_cast<Object*>(arg.value);
      if (!Reference(type) || (value && !il2cpp_class_is_assignable_from(il2cpp_class_from_type(type), value->klass)))
        return false;
    }
  }
  return true;
}
template <class T>
Result<T> Call(Object* object, const char* name, std::initializer_list<Argument> args = {},
               Il2CppClass* static_owner = nullptr)
{
  const MethodInfo* selected = nullptr;
  for (auto* cls = object ? object->klass : static_owner; cls && !selected; cls = il2cpp_class_get_parent(cls)) {
    void* iterator = nullptr;
    while (auto* method = il2cpp_class_get_methods(cls, &iterator)) {
      if (std::strcmp(method->name, name) || !Signature<T>(method, !object, args))
        continue;
      if (selected)
        return {};
      selected = method;
    }
  }
  if (!selected)
    return {};
  if (object)
    selected = il2cpp_object_get_virtual_method(object, selected);
  if (!Signature<T>(selected, !object, args))
    return {};
  std::vector<void*> parameters;
  for (const auto& arg : args)
    parameters.push_back(arg.value);
  Object* result = nullptr;
  if (!Il2CppRuntime::TryInvoke(selected, object, parameters.empty() ? nullptr : parameters.data(), &result))
    return {};
  if constexpr (std::is_same_v<T, void>)
    return {true};
  else if constexpr (std::is_pointer_v<T>) {
    if (result && !il2cpp_class_is_assignable_from(il2cpp_class_from_type(selected->return_type), result->klass))
      return {};
    return {true, reinterpret_cast<T>(result)};
  } else {
    auto box = Handle::Strong(result);
    if (!box || !box.Get() || !ValueType<T>(il2cpp_class_get_type(box.Get()->klass)))
      return {};
    auto* data = il2cpp_object_unbox(box.Get());
    if (!data)
      return {};
    T value;
    std::memcpy(&value, data, sizeof(T));
    return {true, value};
  }
}
inline Result<bool> Alive(Object* object)
{
  return object ? Call<bool>(nullptr, "op_Implicit", {Ref(object)},
                             Class("UnityEngine", "Object", "UnityEngine.CoreModule"))
                : Result<bool>{true, false};
}
template <class T> struct Field {
  FieldInfo* field = nullptr;
  bool       Resolve(Il2CppClass* cls, const char* name, const char* expected = nullptr)
  {
    field = nullptr;
    for (; cls && !field; cls = il2cpp_class_get_parent(cls))
      field = il2cpp_class_get_field_from_name(cls, name);
    if (!field || (il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC) || field->offset < sizeof(Object)
        || !field->parent || static_cast<size_t>(field->offset) + sizeof(T) > il2cpp_class_instance_size(field->parent))
      return false;
    if constexpr (std::is_same_v<T, int32_t>) {
      auto* type = il2cpp_class_from_type(field->type);
      if (!ValueType<T>(field->type)
          && !(type && il2cpp_class_is_enum(type) && ValueType<T>(il2cpp_class_enum_basetype(type))))
        return false;
    } else if (!ValueType<T>(field->type))
      return false;
    return !expected || method_contract::Type(field->type, expected);
  }
  T Read(Object* object) const
  {
    T value{};
    if (field && object && il2cpp_class_is_assignable_from(field->parent, object->klass))
      il2cpp_field_get_value(object, field, &value);
    return value;
  }
  bool Write(Object* object, T value) const
  {
    if (!field || !object || !il2cpp_class_is_assignable_from(field->parent, object->klass))
      return false;
    if constexpr (std::is_pointer_v<T>) {
      if (value && !il2cpp_class_is_assignable_from(il2cpp_class_from_type(field->type), value->klass))
        return false;
      il2cpp_field_set_value(object, field, value); // Reference itself; runtime write barrier.
    } else
      il2cpp_field_set_value(object, field, &value);
    const auto observed = Read(object);
    return std::memcmp(&observed, &value, sizeof(T)) == 0;
  }
};
struct Fields {
  Field<Object*> trigger, action, search, locale, settings, header, description, target, description_override, loader;
  Field<Object*> scroller, spawned, bundle_tooltip, bundle_owner, spawning, initializing;
  Field<OverridableBool> show_header, show_description;
  Field<bool>            override_locale, interactable, regenerating;
  Field<int32_t>         anchor;
  Field<Vector2>         size, offset;
  bool                   Resolve()
  {
    auto* widget       = TooltipClass();
    auto* trigger_type = TriggerClass();
    auto* shop         = Class("Digit.Prime.Shop", "ShopListScrollerViewController");
    auto* smart        = Class("Digit.Client.UI", "SmartScrollerBase");
    auto* bundle       = Class("Digit.Prime.Shop", "BundleDataWidget");
    bool  ok           = true;
    ok &= trigger.Resolve(widget, "_tooltipTrigger", "Digit.Client.Tooltip.TooltipTrigger");
    ok &= action.Resolve(widget, "_tooltipActionTrigger", "Digit.Client.Tooltip.TooltipActionTrigger");
    ok &= search.Resolve(widget, "_searchActionLocaleSetting", "Digit.Prime.Actions.ActionLocaleSetting");
    ok &= locale.Resolve(widget, "_actionLocaleSetting", "Digit.Prime.Actions.ActionLocaleSetting");
    ok &= settings.Resolve(widget, "_settings", "Digit.Prime.Actions.ActionLocaleSettingsList");
    ok &= show_header.Resolve(trigger_type, "_showHeader");
    ok &= show_description.Resolve(trigger_type, "_showDescription");
    ok &= header.Resolve(trigger_type, "_header", "Digit.Client.UI.LocaleTextContext");
    ok &= description.Resolve(trigger_type, "_description", "Digit.Client.UI.LocaleTextContext");
    ok &= override_locale.Resolve(trigger_type, "_overrideLocale");
    ok &= anchor.Resolve(trigger_type, "_anchorOverride");
    ok &= size.Resolve(trigger_type, "_sizeOverride");
    ok &= offset.Resolve(trigger_type, "_positionOverrideOffset");
    ok &= target.Resolve(trigger_type, "_positionOverrideTarget", "UnityEngine.Transform");
    ok &= interactable.Resolve(trigger_type, "_isInteractable");
    ok &= description_override.Resolve(trigger_type, "<DescriptionOverrideText>k__BackingField", "System.String");
    ok &= loader.Resolve(trigger_type, "_loader", "Digit.Client.Tooltip.TooltipLoadAndShow");
    ok &= scroller.Resolve(shop, "_smartScroller");
    ok &= spawned.Resolve(smart, "_spawnedItems");
    ok &= bundle_tooltip.Resolve(bundle, "_invTooltip", "Digit.Prime.Inventories.InventoryTooltipTriggerWidget");
    ok &= bundle_owner.Resolve(bundle, "_scrollerController", "Digit.Prime.Shop.ShopListScrollerViewController");
    ok &= regenerating.Resolve(smart, "_regeneratingContent");
    ok &= spawning.Resolve(smart, "_spawnerCoroutine");
    ok &= initializing.Resolve(smart, "_dataHeldWhilstInitializing");
    return ok;
  }
};
inline Fields& Members()
{
  static Fields fields;
  return fields;
}
inline bool IsStore(Object* object)
{
  auto* type = Class("Digit.Prime.Alliances.Games", "AllianceGamesStoreScrollerViewController");
  return object && type && il2cpp_class_is_assignable_from(type, object->klass);
}
inline Object* TypeObject(Il2CppClass* type)
{ return type ? reinterpret_cast<Object*>(il2cpp_type_get_object(il2cpp_class_get_type(type))) : nullptr; }
inline Result<bool> Eligible(Object* item)
{
  Roots roots;
  if (!roots.Keep(item))
    return {};
  const auto resource = Call<Object*>(item, "get_ResourceSpec");
  auto*      spec     = resource ? roots.Keep(resource.value) : nullptr;
  const auto id       = Call<int64_t>(spec, "get_Id");
  if (!id || id.value <= 0)
    return {};
  const auto observed    = Call<Object*>(nullptr, "get_ResearchManager", {}, Class("Digit.Client.Core", "Hub"));
  auto*      manager     = observed ? roots.Keep(observed.value) : nullptr;
  auto       resource_id = id.value;
  int32_t    tree        = 0;
  return manager ? Call<bool>(
                       manager, "IsResourceRequiredByVisibleResearches",
                       {Scalar(resource_id, "System.Int64"), Scalar(tree, "Digit.PrimeServer.Models.ResearchTreeType")})
                 : Result<bool>{};
}
inline bool Enabled(Object* trigger, bool value)
{
  static const auto* method = method_contract::Resolve(Class("UnityEngine", "Behaviour", "UnityEngine.CoreModule"),
                                                       "set_enabled", false, "System.Void", {"System.Boolean"});
  void*              args[]{&value};
  return trigger && Il2CppRuntime::TryInvoke(method, trigger, args);
}
inline Result<Vector2> NormalizedPosition(Object* smart)
{ return Call<Vector2>(smart, "get_NormalizedPosition"); }
inline bool NotifyPosition(Object* smart, Vector2 value)
{ return Call<void>(smart, "OnValueChangedCallback", {Scalar(value, "UnityEngine.Vector2")}).succeeded; }
inline Result<int32_t> CurrencyCount(Object* context, Roots& roots)
{
  const auto observed   = Call<Object*>(context, "get_StoreCurrency");
  auto*      currencies = observed ? roots.Keep(observed.value) : nullptr;
  const auto soft       = Call<Object*>(currencies, "get_SoftResources");
  return soft && roots.Keep(soft.value) ? Call<int32_t>(soft.value, "get_Count") : Result<int32_t>{};
}
inline Object* Component(Object* source, const char* name, Il2CppClass* type, Roots& roots, bool children = false)
{
  auto* reflected = roots.Keep(TypeObject(type));
  if (!reflected)
    return nullptr;
  bool       inactive = true;
  const auto found    = children ? Call<Object*>(source, name, {Ref(reflected), Scalar(inactive, "System.Boolean")})
                                 : Call<Object*>(source, name, {Ref(reflected)});
  return found ? roots.Keep(found.value) : nullptr;
}
inline Object* ActionComponent(Object* widget, bool create, Roots& roots)
{
  const auto observed = Call<Object*>(widget, "get_gameObject");
  auto*      object   = observed ? roots.Keep(observed.value) : nullptr;
  if (!object)
    return nullptr;
  auto* reflected = roots.Keep(TypeObject(ActionClass()));
  if (!reflected)
    return nullptr;
  const auto found = Call<Object*>(object, "GetComponent", {Ref(reflected)});
  if (!found)
    return nullptr;
  if (found.value)
    return roots.Keep(found.value);
  if (!create)
    return nullptr;
  const auto added = Call<Object*>(object, "AddComponent", {Ref(reflected)});
  return added ? roots.Keep(added.value) : nullptr;
}
inline Object* PlainComponent(Object* widget, Roots& roots)
{
  auto* value = Component(widget, "GetComponent", TriggerClass(), roots);
  return value && value->klass == TriggerClass() ? value : nullptr;
}
inline Object* Template(Roots& roots)
{
  auto*       type     = roots.Keep(TypeObject(TooltipClass()));
  const auto  found    = type ? Call<Object*>(nullptr, "FindObjectsOfTypeAll", {Ref(type)},
                                              Class("UnityEngine", "Resources", "UnityEngine.CoreModule"))
                              : Result<Object*>{};
  auto*       array    = found ? roots.Keep(found.value) : nullptr;
  const auto* metadata = array ? il2cpp_class_get_type(array->klass) : nullptr;
  if (!metadata || metadata->type != IL2CPP_TYPE_SZARRAY || !array->klass->element_class
      || il2cpp_class_is_valuetype(array->klass->element_class))
    return nullptr;
  auto* items = reinterpret_cast<Il2CppArraySize*>(array);
  for (il2cpp_array_size_t index = 0; index < items->max_length; ++index) {
    auto* widget = reinterpret_cast<Object**>(items->vector)[index];
    if (widget && il2cpp_class_is_assignable_from(TooltipClass(), widget->klass) && Members().action.Read(widget)
        && Members().search.Read(widget))
      return roots.Keep(widget);
  }
  return nullptr;
}
inline bool CopyAppearance(Object* plain, Object* action, Roots& roots)
{
  auto& f    = Members();
  auto  copy = [&]<class T>(const Field<T>& field) {
    const auto value = field.Read(plain);
    if constexpr (std::is_pointer_v<T>)
      if (value && !roots.Keep(value))
        return false;
    return field.Write(action, value);
  };
  return copy(f.show_header) && copy(f.header) && copy(f.show_description) && copy(f.description)
         && copy(f.override_locale) && copy(f.anchor) && copy(f.size) && copy(f.target) && copy(f.offset)
         && copy(f.interactable) && copy(f.description_override) && copy(f.loader);
}
} // namespace alliance_store_game

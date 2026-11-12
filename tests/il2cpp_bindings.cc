// Typed bindings against a controlled managed runtime: lookup, signature checks, native calls, fields, dispatch,
// failure reporting, hook sets and strings, plus the Unity keyboard and focus snapshots built on them.

#include "il2cpp_test_api.h"
#include <il2cpp/hook.h>
#include <unity/input.h>
#include <unity/ui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
void Require(bool condition, const char* what)
{
  if (!condition)
    throw std::runtime_error(what);
}

int                                                       input_frame = 0, held_reads = 0, down_reads = 0;
bool                                                      any_held = false, any_down = false;
std::array<bool, std::to_underlying(unity::KeyCode::Max)> held_keys{}, down_keys{};

void KeyboardSnapshot()
{
  using namespace unity;
  Key::Poll();
  Require(!Key::Pressed(KeyCode::A) && !Key::Down(KeyCode::A), "idle keyboard reads false");
  Require(held_reads == 0 && down_reads == 0, "idle keyboard avoids key queries");

  ++input_frame;
  any_held = any_down                       = true;
  held_keys[std::to_underlying(KeyCode::A)] = true;
  down_keys[std::to_underlying(KeyCode::A)] = true;
  Key::Poll();
  for (int i = 0; i < 100; ++i) {
    Key::Poll();
    Require(Key::Pressed(KeyCode::A) && Key::Down(KeyCode::A), "held and down snapshot");
    Require(!Key::Pressed(KeyCode::B) && !Key::Down(KeyCode::B), "negative key queries are cached too");
  }
  Require(held_reads == 2 && down_reads == 2, "each key is queried once per state per frame");
  ++input_frame;
  held_keys[std::to_underlying(KeyCode::A)] = false;
  down_keys[std::to_underlying(KeyCode::A)] = false;
  Key::Poll();
  Require(!Key::Pressed(KeyCode::A) && !Key::Down(KeyCode::A), "new frames refresh the snapshot");
  Require(held_reads == 3 && down_reads == 3, "new frame queries keys again");
  ++input_frame;
  any_held = any_down = false;
  Key::Poll();
  Require(!Key::Pressed(KeyCode::A) && !Key::Down(KeyCode::A), "return to idle clears held keys");
  Require(held_reads == 3 && down_reads == 3, "idle frame stays query-free");
}

struct FakeClass {
  // MSVC cannot embed Il2CppClass because its vtable is a zero-length trailing array.
  std::unique_ptr<Il2CppClass>   metadata = std::make_unique<Il2CppClass>();
  Il2CppClass&                   klass    = *metadata;
  Il2CppType                     byref{};
  std::vector<const MethodInfo*> methods;
  std::vector<FieldInfo>         fields;
  std::vector<PropertyInfo>      properties;
  std::vector<Il2CppClass*>      nested;
  int32_t                        value_size = 0;
  bool                           is_enum    = false;
};

std::deque<FakeClass>                              classes;
std::deque<MethodInfo>                             methods;
std::deque<std::vector<const Il2CppType*>>         parameter_lists;
std::deque<Il2CppType>                             field_types;
std::unordered_map<const Il2CppClass*, FakeClass*> registry;
std::vector<std::string>                           errors;
std::vector<const Il2CppClass*>                    initialized;
// Method table walks started, one per class visited.
int method_lookups = 0;

FakeClass& Define(const char* namespaze, const char* name, Il2CppTypeEnum type, FakeClass* parent = nullptr,
                  int32_t value_size = 0)
{
  auto& fake                      = classes.emplace_back();
  fake.klass.name                 = name;
  fake.klass.namespaze            = namespaze;
  fake.klass.parent               = parent ? &parent->klass : nullptr;
  fake.klass.byval_arg.type       = type;
  fake.klass.byval_arg.data.dummy = &fake.klass;
  fake.klass.byval_arg.valuetype  = value_size > 0;
  fake.byref                      = fake.klass.byval_arg;
  fake.byref.byref                = 1;
  fake.value_size                 = value_size;
  registry[&fake.klass]           = &fake;
  return fake;
}

const Il2CppType* T(FakeClass& fake)
{ return &fake.klass.byval_arg; }
const Il2CppType* Ref(FakeClass& fake)
{ return &fake.byref; }

MethodInfo& AddMethod(FakeClass& owner, const char* name, void* native, const Il2CppType* result,
                      std::vector<const Il2CppType*> params, uint16_t flags = METHOD_ATTRIBUTE_PUBLIC)
{
  auto& list                  = parameter_lists.emplace_back(std::move(params));
  auto& method                = methods.emplace_back();
  method.name                 = name;
  method.methodPointer        = reinterpret_cast<Il2CppMethodPointer>(native);
  method.virtualMethodPointer = method.methodPointer;
  method.klass                = &owner.klass;
  method.return_type          = result;
  method.parameters           = list.data();
  method.parameters_count     = static_cast<uint8_t>(list.size());
  method.flags                = flags;
  owner.methods.push_back(&method);
  owner.klass.methods      = owner.methods.data();
  owner.klass.method_count = static_cast<uint16_t>(owner.methods.size());
  return method;
}

void AddField(FakeClass& owner, const char* name, FakeClass& type, int32_t offset, bool is_static = false)
{
  auto& field_type = field_types.emplace_back(type.klass.byval_arg);
  if (is_static)
    field_type.attrs |= FIELD_ATTRIBUTE_STATIC;
  owner.fields.push_back({name, &field_type, &owner.klass, offset, 0});
}

void AddProperty(FakeClass& owner, const char* name, const MethodInfo* get, const MethodInfo* set = nullptr)
{ owner.properties.push_back({&owner.klass, name, get, set, 0, 0}); }

FakeClass& Fake(const Il2CppClass* klass)
{ return *registry.at(klass); }

// Managed object storage for tests. Offsets mirror a real object: header, then fields.
struct TestObject {
  Il2CppObject  header;
  int32_t       count;
  Il2CppObject* target;
};

struct Vec3 {
  float x, y, z;
};

// Runtime classes.
FakeClass& void_class    = Define("System", "Void", IL2CPP_TYPE_VOID);
FakeClass& object_class  = Define("System", "Object", IL2CPP_TYPE_OBJECT);
FakeClass& int_class     = Define("System", "Int32", IL2CPP_TYPE_I4, nullptr, 4);
FakeClass& bool_class    = Define("System", "Boolean", IL2CPP_TYPE_BOOLEAN, nullptr, 1);
FakeClass& float_class   = Define("System", "Single", IL2CPP_TYPE_R4, nullptr, 4);
FakeClass& string_class  = Define("System", "String", IL2CPP_TYPE_STRING, &object_class);
FakeClass& type_class    = Define("System", "Type", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& vec3_class    = Define("UnityEngine", "Vector3", IL2CPP_TYPE_VALUETYPE, nullptr, sizeof(Vec3));
FakeClass& state_class   = Define("Game", "State", IL2CPP_TYPE_VALUETYPE, nullptr, 4);
FakeClass& base_class    = Define("Game", "Base", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& derived_class = Define("Game", "Derived", IL2CPP_TYPE_CLASS, &base_class);
FakeClass& other_class   = Define("Game", "Other", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& outer_class   = Define("Game", "Outer", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& inner_class   = Define("", "Inner", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& list_a_class  = Define("Game", "ListA", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& list_b_class  = Define("Game", "ListB", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& delegate_base = Define("System", "Delegate", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& action_class  = Define("System", "Action", IL2CPP_TYPE_CLASS, &delegate_base);
FakeClass& intptr_class  = Define("System", "IntPtr", IL2CPP_TYPE_I, nullptr, sizeof(void*));

FakeClass& event_system_class = Define("UnityEngine.EventSystems", "EventSystem", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& game_object_class  = Define("UnityEngine", "GameObject", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& input_field_class  = Define("TMPro", "TMP_InputField", IL2CPP_TYPE_CLASS, &object_class);

unity::EventSystem    event_system{};
unity::GameObject     selected_object{};
unity::TMP_InputField input_field{};
unity::GameObject*    selected         = &selected_object;
bool                  has_event_system = true, has_input_field = true, redirect_selection = false;
int                   event_system_reads = 0;

unity::EventSystem* EventSystem_get_current(const MethodInfo*)
{
  ++event_system_reads;
  return has_event_system ? &event_system : nullptr;
}
unity::GameObject* EventSystem_get_selected(unity::EventSystem*, const MethodInfo*)
{ return selected; }
void EventSystem_set_selected(unity::EventSystem*, unity::GameObject* value, const MethodInfo*)
{ selected = redirect_selection ? &selected_object : value; }
unity::TMP_InputField* GameObject_get_component(unity::GameObject*, Il2CppObject*, const MethodInfo*)
{ return has_input_field ? &input_field : nullptr; }
bool InputField_get_focused(unity::TMP_InputField*, const MethodInfo*)
{ return true; }

void InputFocus()
{
  using namespace unity;
  ++input_frame;
  Key::Poll();
  Require(Key::IsInputFocused(), "selected text field has focus");
  const int reads = event_system_reads;
  Require(Key::IsInputFocused() && event_system_reads == reads, "focus is cached within a frame");
  Key::ClearInputFocus();
  Require(event_system_reads == reads + 1, "clearing focus reads the event system once");
  Require(selected == nullptr && !Key::IsInputFocused(), "clearing selection invalidates focus within the frame");

  selected           = &selected_object;
  redirect_selection = true;
  Key::ClearInputFocus();
  Require(Key::IsInputFocused(), "selection callbacks can focus another field when focus is cleared");
  redirect_selection = false;

  has_input_field = false;
  ++input_frame;
  Key::Poll();
  Require(!Key::IsInputFocused(), "a selected object without a text field is not text focus");
  Key::ClearInputFocus();
  Require(selected == &selected_object, "clearing text focus preserves a selected non-text object");

  has_event_system = false;
  ++input_frame;
  Key::Poll();
  Require(!Key::IsInputFocused(), "no event system has no text focus");
  Key::ClearInputFocus();
}

TestObject     base_object{}, derived_object{}, other_object{}, list_a{}, list_b{}, allocated{};
Il2CppObject   string_object{}, type_object{};
Il2CppDelegate delegate_object{};

// Generated code stand-ins. Each records what the binding passed.
const Il2CppObject* seen_self   = nullptr;
const MethodInfo*   seen_method = nullptr;
int32_t             seen_int    = 0;
bool                seen_bool   = false;
std::string         seen_overload;
int                 native_calls = 0;
int32_t             static_flag  = 0;

int32_t Base_get_Count(Il2CppObject* self, const MethodInfo* method)
{
  seen_self   = self;
  seen_method = method;
  ++native_calls;
  return reinterpret_cast<TestObject*>(self)->count;
}
void Base_set_Count(Il2CppObject* self, int32_t value, const MethodInfo*)
{ reinterpret_cast<TestObject*>(self)->count = value; }
void Base_Select_Int(Il2CppObject*, int32_t value, const MethodInfo*)
{
  seen_overload = "int";
  seen_int      = value;
}
void Base_Select_Bool(Il2CppObject*, bool value, const MethodInfo*)
{
  seen_overload = "bool";
  seen_bool     = value;
}
bool Base_Find_String(Il2CppObject*, Il2CppString*, const MethodInfo*)
{
  seen_overload = "string";
  return true;
}
bool Base_Find_Type(Il2CppObject*, Il2CppReflectionType*, const MethodInfo*)
{
  seen_overload = "type";
  return true;
}
bool Base_TryGet(Il2CppObject*, int32_t key, Il2CppObject** result, const MethodInfo*)
{
  *result = key == 1 ? &other_object.header : nullptr;
  return *result != nullptr;
}
Vec3 Base_get_Position(Il2CppObject*, const MethodInfo*)
{ return {1.f, 2.f, 3.f}; }
int32_t Base_get_State(Il2CppObject*, const MethodInfo*)
{ return 2; }
const char* Base_Describe(Il2CppObject*, const MethodInfo*)
{ return "base"; }
const char* Derived_Describe(Il2CppObject*, const MethodInfo*)
{ return "derived"; }
int32_t Base_Twice(int32_t value, const MethodInfo*)
{ return value * 2; }
Il2CppObject* Base_get_Instance(const MethodInfo*)
{ return &base_object.header; }
void Base_ctor(Il2CppObject* self, int32_t count, const MethodInfo*)
{ reinterpret_cast<TestObject*>(self)->count = count; }
int32_t ListA_get_Count(Il2CppObject*, const MethodInfo*)
{ return 2; }
int32_t ListB_get_Count(Il2CppObject*, const MethodInfo*)
{ return 3; }
Il2CppObject* List_get_Item(Il2CppObject* self, int32_t index, const MethodInfo*)
{ return index == 0 ? self : &other_object.header; }
void List_set_Item(Il2CppObject* self, int32_t index, Il2CppObject* value, const MethodInfo* method)
{
  seen_self                                   = self;
  seen_int                                    = index;
  seen_method                                 = method;
  reinterpret_cast<TestObject*>(self)->target = value;
}
Il2CppException thrown{};
int32_t         Base_Explode(Il2CppObject*, const MethodInfo*)
{ throw Il2CppExceptionWrapper(&thrown); }
void Never(Il2CppObject*, const MethodInfo*)
{ throw std::runtime_error("shared generic method was called natively"); }

const MethodInfo* delegate_target = nullptr;
void              DelegateInvoker(Il2CppMethodPointer, const MethodInfo*, void* self, void** args, void*)
{
  // Delegate(object target, IntPtr method): the second argument points at the MethodInfo address.
  auto* delegate   = static_cast<Il2CppDelegate*>(self);
  delegate_target  = *static_cast<const MethodInfo**>(args[1]);
  delegate->method = delegate_target;
}
void NativeTarget() {}

MethodInfo* base_describe     = nullptr;
MethodInfo* shared_method     = nullptr;
int32_t     shared_result     = 41;
int         boxed_invocations = 0;

void BuildRuntime()
{
  auto& get_count = AddMethod(base_class, "get_Count", reinterpret_cast<void*>(&Base_get_Count), T(int_class), {});
  auto& set_count =
      AddMethod(base_class, "set_Count", reinterpret_cast<void*>(&Base_set_Count), T(void_class), {T(int_class)});
  AddProperty(base_class, "Count", &get_count, &set_count);
  AddMethod(base_class, "Select", reinterpret_cast<void*>(&Base_Select_Int), T(void_class), {T(int_class)});
  AddMethod(base_class, "Select", reinterpret_cast<void*>(&Base_Select_Bool), T(void_class), {T(bool_class)});
  AddMethod(base_class, "Find", reinterpret_cast<void*>(&Base_Find_String), T(bool_class), {T(string_class)});
  AddMethod(base_class, "Find", reinterpret_cast<void*>(&Base_Find_Type), T(bool_class), {T(type_class)});
  AddMethod(base_class, "TryGet", reinterpret_cast<void*>(&Base_TryGet), T(bool_class),
            {T(int_class), Ref(object_class)});
  auto& get_position =
      AddMethod(base_class, "get_Position", reinterpret_cast<void*>(&Base_get_Position), T(vec3_class), {});
  AddProperty(base_class, "Position", &get_position);
  auto& get_state = AddMethod(base_class, "get_State", reinterpret_cast<void*>(&Base_get_State), T(state_class), {});
  AddProperty(base_class, "State", &get_state);
  base_describe = &AddMethod(base_class, "Describe", reinterpret_cast<void*>(&Base_Describe), T(object_class), {},
                             METHOD_ATTRIBUTE_PUBLIC | METHOD_ATTRIBUTE_VIRTUAL);
  AddMethod(derived_class, "Describe", reinterpret_cast<void*>(&Derived_Describe), T(object_class), {},
            METHOD_ATTRIBUTE_PUBLIC | METHOD_ATTRIBUTE_VIRTUAL);
  AddMethod(base_class, "Twice", reinterpret_cast<void*>(&Base_Twice), T(int_class), {T(int_class)},
            METHOD_ATTRIBUTE_PUBLIC | METHOD_ATTRIBUTE_STATIC);
  auto& get_instance = AddMethod(base_class, "get_Instance", reinterpret_cast<void*>(&Base_get_Instance), T(base_class),
                                 {}, METHOD_ATTRIBUTE_PUBLIC | METHOD_ATTRIBUTE_STATIC);
  AddProperty(base_class, "Instance", &get_instance);
  AddMethod(base_class, ".ctor", reinterpret_cast<void*>(&Base_ctor), T(void_class), {T(int_class)});
  AddMethod(base_class, "Explode", reinterpret_cast<void*>(&Base_Explode), T(int_class), {});
  shared_method = &AddMethod(base_class, "Shared", reinterpret_cast<void*>(&Never), T(int_class), {});
  shared_method->has_full_generic_sharing_signature = 1;
  AddField(base_class, "_count", int_class, offsetof(TestObject, count));
  AddField(base_class, "_target", base_class, offsetof(TestObject, target));
  AddField(base_class, "s_flag", int_class, 0, true);

  AddMethod(list_a_class, "get_Count", reinterpret_cast<void*>(&ListA_get_Count), T(int_class), {});
  AddMethod(list_a_class, "get_Item", reinterpret_cast<void*>(&List_get_Item), T(object_class), {T(int_class)});
  AddMethod(list_a_class, "set_Item", reinterpret_cast<void*>(&List_set_Item), T(void_class),
            {T(int_class), T(object_class)});
  AddMethod(list_b_class, "get_Count", reinterpret_cast<void*>(&ListB_get_Count), T(int_class), {});
  AddMethod(list_b_class, "get_Item", reinterpret_cast<void*>(&List_get_Item), T(object_class), {T(int_class)});
  AddMethod(list_b_class, "set_Item", reinterpret_cast<void*>(&List_set_Item), T(void_class),
            {T(int_class), T(object_class)});

  auto& ctor          = AddMethod(action_class, ".ctor", nullptr, T(void_class), {T(object_class), T(intptr_class)});
  ctor.invoker_method = &DelegateInvoker;
  AddMethod(base_class, "OnEvent", reinterpret_cast<void*>(&NativeTarget), T(void_class), {},
            METHOD_ATTRIBUTE_PUBLIC | METHOD_ATTRIBUTE_STATIC);

  auto& current = AddMethod(event_system_class, "get_current", reinterpret_cast<void*>(&EventSystem_get_current),
                            T(event_system_class), {}, METHOD_ATTRIBUTE_PUBLIC | METHOD_ATTRIBUTE_STATIC);
  AddProperty(event_system_class, "current", &current);
  auto& selected_getter = AddMethod(event_system_class, "get_currentSelectedGameObject",
                                    reinterpret_cast<void*>(&EventSystem_get_selected), T(game_object_class), {});
  AddProperty(event_system_class, "currentSelectedGameObject", &selected_getter);
  AddMethod(event_system_class, "SetSelectedGameObject", reinterpret_cast<void*>(&EventSystem_set_selected),
            T(void_class), {T(game_object_class)});
  AddMethod(game_object_class, "GetComponent", reinterpret_cast<void*>(&GameObject_get_component), T(input_field_class),
            {T(type_class)});
  auto& focused = AddMethod(input_field_class, "get_isFocused", reinterpret_cast<void*>(&InputField_get_focused),
                            T(bool_class), {});
  AddProperty(input_field_class, "isFocused", &focused);
  event_system.klass    = &event_system_class.klass;
  selected_object.klass = &game_object_class.klass;
  input_field.klass     = &input_field_class.klass;

  inner_class.klass.declaringType = &outer_class.klass;
  outer_class.nested.push_back(&inner_class.klass);

  base_object.header.klass    = &base_class.klass;
  base_object.count           = 7;
  derived_object.header.klass = &derived_class.klass;
  other_object.header.klass   = &other_class.klass;
  list_a.header.klass         = &list_a_class.klass;
  list_b.header.klass         = &list_b_class.klass;
  string_object.klass         = &string_class.klass;
  type_object.klass           = &type_class.klass;
}
} // namespace

API(Il2CppDomain*, il2cpp_domain_get, ())
{ return reinterpret_cast<Il2CppDomain*>(1); }
END_API
API(const Il2CppAssembly*, il2cpp_domain_assembly_open, (Il2CppDomain*, const char*))
{ return reinterpret_cast<Il2CppAssembly*>(1); }
END_API
API(const Il2CppImage*, il2cpp_assembly_get_image, (const Il2CppAssembly*))
{ return reinterpret_cast<Il2CppImage*>(1); }
END_API
API(const Il2CppImage*, il2cpp_get_corlib, ())
{ return reinterpret_cast<Il2CppImage*>(1); }
END_API
API(Il2CppClass*, il2cpp_class_from_name, (const Il2CppImage*, const char* namespaze, const char* name))
{
  const auto fake = std::ranges::find_if(classes, [&](const FakeClass& fake) {
    return std::string_view(fake.klass.namespaze) == namespaze && std::string_view(fake.klass.name) == name;
  });
  return fake == classes.end() ? nullptr : &fake->klass;
}
END_API
API(const char*, il2cpp_class_get_name, (Il2CppClass * klass))
{ return klass->name; }
END_API
API(const char*, il2cpp_class_get_namespace, (Il2CppClass * klass))
{ return klass->namespaze; }
END_API
API(Il2CppClass*, il2cpp_class_get_declaring_type, (Il2CppClass * klass))
{ return klass->declaringType; }
END_API
API(Il2CppClass*, il2cpp_class_get_parent, (Il2CppClass * klass))
{ return klass->parent; }
END_API
API(Il2CppClass*, il2cpp_class_get_nested_types, (Il2CppClass * klass, void** iter))
{
  auto& nested = Fake(klass).nested;
  auto  index  = reinterpret_cast<size_t>(*iter);
  if (index >= nested.size())
    return nullptr;
  *iter = reinterpret_cast<void*>(index + 1);
  return nested[index];
}
END_API
API(const MethodInfo*, il2cpp_class_get_methods, (Il2CppClass * klass, void** iter))
{
  auto index = reinterpret_cast<size_t>(*iter);
  method_lookups += index == 0;
  if (index >= klass->method_count)
    return nullptr;
  *iter = reinterpret_cast<void*>(index + 1);
  return klass->methods[index];
}
END_API
API(const PropertyInfo*, il2cpp_class_get_property_from_name, (Il2CppClass * klass, const char* name))
{
  for (; klass; klass = klass->parent)
    for (auto& property : Fake(klass).properties)
      if (std::string_view(property.name) == name)
        return &property;
  return nullptr;
}
END_API
API(const MethodInfo*, il2cpp_property_get_get_method, (PropertyInfo * property))
{ return property->get; }
END_API
API(const MethodInfo*, il2cpp_property_get_set_method, (PropertyInfo * property))
{ return property->set; }
END_API
API(FieldInfo*, il2cpp_class_get_field_from_name, (Il2CppClass * klass, const char* name))
{
  for (; klass; klass = klass->parent)
    for (auto& field : Fake(klass).fields)
      if (std::string_view(field.name) == name)
        return &field;
  return nullptr;
}
END_API
API(Il2CppClass*, il2cpp_class_from_type, (const Il2CppType* type))
{ return static_cast<Il2CppClass*>(type->data.dummy); }
END_API
API(const Il2CppType*, il2cpp_class_get_type, (Il2CppClass * klass))
{ return &klass->byval_arg; }
END_API
API(Il2CppMethodPointer, il2cpp_resolve_icall, (const char* name))
{
  static constexpr auto answer = +[]() -> int32_t { return 42; };
  static constexpr auto frame  = +[]() -> int32_t { return input_frame; };
  static constexpr auto held   = +[]() { return any_held; };
  static constexpr auto down   = +[]() { return any_down; };
  static constexpr auto key    = +[](unity::KeyCode key) {
    ++held_reads;
    return held_keys[std::to_underlying(key)];
  };
  static constexpr auto key_down = +[](unity::KeyCode key) {
    ++down_reads;
    return down_keys[std::to_underlying(key)];
  };
  const std::pair<std::string_view, Il2CppMethodPointer> calls[] = {
      {"Test::Answer()", reinterpret_cast<Il2CppMethodPointer>(answer)},
      {"UnityEngine.Time::get_frameCount()", reinterpret_cast<Il2CppMethodPointer>(frame)},
      {"UnityEngine.Input::get_anyKey()", reinterpret_cast<Il2CppMethodPointer>(held)},
      {"UnityEngine.Input::get_anyKeyDown()", reinterpret_cast<Il2CppMethodPointer>(down)},
      {"UnityEngine.Input::GetKeyInt(UnityEngine.KeyCode)", reinterpret_cast<Il2CppMethodPointer>(key)},
      {"UnityEngine.Input::GetKeyDownInt(UnityEngine.KeyCode)", reinterpret_cast<Il2CppMethodPointer>(key_down)},
  };
  const auto* call = std::ranges::find(calls, name, &std::pair<std::string_view, Il2CppMethodPointer>::first);
  return call == std::end(calls) ? nullptr : call->second;
}
END_API
API(Il2CppObject*, il2cpp_type_get_object, (const Il2CppType*))
{ return &type_object; }
END_API
API(char*, il2cpp_type_get_name, (const Il2CppType* type))
{ return strdup(static_cast<Il2CppClass*>(type->data.dummy)->name); }
END_API
API(void, il2cpp_free, (void* pointer))
{ std::free(pointer); }
END_API
API(bool, il2cpp_class_is_valuetype, (const Il2CppClass* klass))
{ return klass->byval_arg.valuetype; }
END_API
API(bool, il2cpp_class_is_enum, (const Il2CppClass* klass))
{ return klass == &state_class.klass; }
END_API
API(int32_t, il2cpp_class_value_size, (Il2CppClass * klass, uint32_t*))
{ return Fake(klass).value_size; }
END_API
API(bool, il2cpp_class_is_assignable_from, (Il2CppClass * klass, Il2CppClass* other))
{
  // The real runtime dereferences both classes.
  if (!klass || !other) {
    std::fprintf(stderr, "il2cpp_class_is_assignable_from called with a null class\n");
    std::abort();
  }
  for (; other; other = other->parent)
    if (other == klass)
      return true;
  return false;
}
END_API
API(void, il2cpp_runtime_class_init, (Il2CppClass * klass))
{ initialized.push_back(klass); }
END_API
API(const MethodInfo*, il2cpp_object_get_virtual_method, (Il2CppObject * object, const MethodInfo* method))
{
  for (auto* klass = object->klass; klass; klass = klass->parent)
    for (const auto* candidate : std::span(klass->methods, klass->method_count))
      if (std::string_view(candidate->name) == method->name)
        return candidate;
  return nullptr;
}
END_API
API(void*, il2cpp_object_unbox, (Il2CppObject*))
{ return &shared_result; }
END_API
API(Il2CppObject*, il2cpp_runtime_invoke, (const MethodInfo* method, void*, void**, Il2CppException** exception))
{
  if (method != shared_method || !exception) {
    std::fputs("unexpected boxed invocation\n", stderr);
    std::abort();
  }
  ++boxed_invocations;
  return &string_object;
}
END_API
API(void, il2cpp_format_exception, (const Il2CppException*, char* message, int size))
{ std::snprintf(message, size, "NullReferenceException"); }
END_API
API(Il2CppObject*, il2cpp_object_new, (const Il2CppClass* klass))
{
  if (klass == &action_class.klass) {
    delegate_object.object.klass = const_cast<Il2CppClass*>(klass);
    return &delegate_object.object;
  }
  allocated              = {};
  allocated.header.klass = const_cast<Il2CppClass*>(klass);
  return &allocated.header;
}
END_API
API(void, il2cpp_field_set_value, (Il2CppObject * object, FieldInfo* field, void* value))
{
  // Like Field::SetValueRaw: value types are copied from the pointer, references are the pointer.
  auto  size = Fake(static_cast<Il2CppClass*>(field->type->data.dummy)).value_size;
  auto* dest = reinterpret_cast<char*>(object) + field->offset;
  if (size)
    std::memcpy(dest, value, size);
  else
    std::memcpy(dest, &value, sizeof(void*));
}
END_API
API(void, il2cpp_field_static_get_value, (FieldInfo*, void* value))
{ std::memcpy(value, &static_flag, sizeof(static_flag)); }
END_API
API(void, il2cpp_field_static_set_value, (FieldInfo*, void* value))
{ std::memcpy(&static_flag, value, sizeof(static_flag)); }
END_API

namespace
{
struct Base : il2cpp::Object {
  using Class = il2cpp::Class<"Game", "Game", "Base">;

  int32_t Count()
  { return Get<"Count", int32_t>(); }
  void SetCount(int32_t value)
  { Set<"Count">(value); }
  int32_t count()
  { return Field<"_count", int32_t>(); }
  void SetCount_(int32_t value)
  { SetField<"_count">(value); }
  Base* target()
  { return Field<"_target", Base*>(); }
  void SetTarget(Base* value)
  { SetField<"_target">(value); }
  Vec3 Position()
  { return Get<"Position", Vec3>(); }
  const char* Describe()
  { return Call<"Describe", const char*>(); }
};
struct Derived : Base {
  using Class = il2cpp::Class<"Game", "Game", "Derived">;
};
struct Other : il2cpp::Object {
  using Class = il2cpp::Class<"Game", "Game", "Other">;
};
// A binding whose class is not in the game, as after an update renames it.
struct Gone : il2cpp::Object {
  using Class = il2cpp::Class<"Game", "Game", "Gone">;
};
enum class State : int32_t { kIdle = 0, kBusy = 2 };

// Detour targets for the hook set test, long enough for SPUD's patch.
[[gnu::noinline]] int Scaled(int value)
{
  volatile int result = value;
  for (int i = 0; i < 2; ++i)
    result = result * 3 + 1;
  return result - value;
}
[[gnu::noinline]] int Offset(int value)
{
  volatile int result = value;
  for (int i = 0; i < 2; ++i)
    result = result + 5;
  return result * 2;
}
int Scaled_Hook(auto original, int value)
{ return original(value) + 100; }
int Offset_Hook(auto original, int value)
{ return original(value) + 1000; }

size_t ErrorsMentioning(std::string_view text)
{
  return std::ranges::count_if(errors, [text](const std::string& error) { return error.contains(text); });
}
} // namespace

int main()
{
  BuildRuntime();
  il2cpp::SetErrorHandler([](std::string_view message) { errors.emplace_back(message); });
  auto* base = reinterpret_cast<Base*>(&base_object);

  // Properties call the generated getter with the receiver and its MethodInfo.
  Require(base->Count() == 7 && seen_self == &base_object.header, "property getter receiver");
  Require(seen_method && std::string_view(seen_method->name) == "get_Count", "trailing MethodInfo");
  base->SetCount(9);
  Require(base->Count() == 9, "property setter");
  Require(base->Position().y == 2.f, "struct return");
  Require(base->Get<"State", State>() == State::kBusy, "enum property");

  // Fields read at their offset and write through the runtime.
  Require(base->count() == 9, "field read");
  base->SetCount_(4);
  Require(base_object.count == 4, "field write");
  base->SetTarget(base);
  Require(base->target() == base, "reference field");
  Base::Class::SetField<"s_flag">(int32_t{5});
  Require(Base::Class::Field<"s_flag", int32_t>() == 5 && static_flag == 5, "static field");

  // Overloads are chosen by C++ argument types.
  base->Call<"Select">(int32_t{3});
  Require(seen_overload == "int" && seen_int == 3, "int overload");
  base->Call<"Select">(true);
  Require(seen_overload == "bool" && seen_bool, "bool overload");
  Require(base->Call<"Find", bool>(reinterpret_cast<Il2CppString*>(&string_object)) && seen_overload == "string",
          "string overload");
  Require(base->Call<"Find", bool>(Other::Class::TypeObject()) && seen_overload == "type", "type overload");

  // ref/out arguments.
  Il2CppObject* found = nullptr;
  Require(base->Call<"TryGet", bool>(int32_t{1}, std::ref(found)) && found == &other_object.header, "out argument");

  // A named signature converts the arguments and binds T& parameters to the caller's lvalues.
  base->Call<"Select", void(int32_t)>(7);
  Require(seen_overload == "int" && seen_int == 7, "signature picks overload");
  found = nullptr;
  Require(base->Call<"TryGet", bool(int32_t, Il2CppObject*&)>(1, found) && found == &other_object.header,
          "signature out argument");
  Require(Base::Class::Call<"Twice", int32_t(int32_t)>(4) == 8, "static signature call");

  // Virtual calls dispatch on the receiver; non-virtual calls do not.
  auto* derived = reinterpret_cast<Derived*>(&derived_object);
  Require(std::string_view(base->Describe()) == "base", "base dispatch");
  Require(std::string_view(static_cast<Base*>(derived)->Describe()) == "derived", "virtual dispatch");

  // Static members initialize their class.
  Require(Base::Class::Call<"Twice", int32_t>(int32_t{21}) == 42, "static call");
  Require(Base::Class::Get<"Instance", Base*>() == base, "static property");
  Require(std::ranges::contains(initialized, &base_class.klass), "class init");

  // Construction runs the matching constructor.
  auto* made = il2cpp::New<Base>(int32_t{12});
  Require(made && made->klass == &base_class.klass && made->count() == 12, "construction");

  // Constructors are not inherited; a missing one is reported instead of running a base constructor.
  Require(il2cpp::New<Derived>(int32_t{1}) == nullptr && ErrorsMentioning("Game.Derived..ctor"), "ctor not inherited");

  // Lookups happen once per binding, not per call.
  const int lookups = method_lookups;
  base->Call<"Select", void(bool)>(false);
  Require(method_lookups > lookups, "first call looks the method up");
  const int resolved = method_lookups;
  for (int i = 0; i < 3; ++i)
    base->Call<"Select", void(bool)>(i == 1);
  Require(method_lookups == resolved && seen_overload == "bool", "cached method lookup");

  // Missing and drifted members are reported once and return defaults without calling native code.
  const int calls = native_calls;
  Require(base->Get<"Missing", int32_t>() == 0 && base->Get<"Missing", int32_t>() == 0, "missing default");
  Require(ErrorsMentioning("missing property Game.Base.Missing") == 1, "missing reported once");
  base->Call<"Select">(1.5f);
  Require(ErrorsMentioning("no overload of Game.Base.Select") == 1, "drift reported");
  Require(base->Get<"Count", bool>() == false && ErrorsMentioning("property Game.Base.Count is Int32") == 1,
          "property type drift");
  Require(base->Field<"_count", Other*>() == nullptr && ErrorsMentioning("field Game.Base._count") == 1,
          "field type drift");
  Require(base->Field<"_target", Gone*>() == nullptr && ErrorsMentioning("field Game.Base._target") == 1,
          "reference to a missing class rejected");
  Require(base->Call<"Find", bool>(reinterpret_cast<Base*>(&other_object)) == false, "unrelated reference rejected");
  Require(native_calls == calls, "rejected members do not call native code");
  Require(il2cpp::Class<"Game", "Game", "Nope">::Raw() == nullptr && ErrorsMentioning("missing class Game:Game.Nope"),
          "missing class");

  // Managed exceptions are contained, reported once per method, and return the default.
  Require(base->Call<"Explode", int32_t>() == 0 && base->Call<"Explode", int32_t>() == 0, "exception contained");
  Require(ErrorsMentioning("Game.Base.Explode threw: NullReferenceException") == 1, "exception reported once");

  // Internal calls run when present; a missing one is reported once and returns the default.
  static const auto answer  = il2cpp::ICall<int32_t()>("Test::Answer()");
  static const auto renamed = il2cpp::ICall<int32_t()>("Test::Renamed()");
  Require(answer && answer() == 42, "internal call");
  Require(!renamed && renamed() == 0 && renamed() == 0, "missing internal call returns the default");
  Require(ErrorsMentioning("missing internal call Test::Renamed()") == 1, "missing internal call reported once");

  // Nested classes resolve through their declaring type.
  Require(il2cpp::Class<"Game", "Game", "Outer/Inner">::Raw() == &inner_class.klass, "nested class");

  // Unbound wrappers resolve against each runtime class.
  auto* first  = reinterpret_cast<il2cpp::List<Il2CppObject*>*>(&list_a);
  auto* second = reinterpret_cast<il2cpp::List<Il2CppObject*>*>(&list_b);
  Require(first->Count() == 2 && second->Count() == 3 && first->Count() == 2, "per-class lookup");
  int items = 0;
  for (auto* item : *second)
    items += item != nullptr;
  Require(items == 3 && (*first)[0] == &list_a.header, "list iteration");
  Require(std::ranges::count(*second, nullptr) == 0 && std::ranges::distance(il2cpp::Items(second)) == 3,
          "lists are ranges");
  Require(il2cpp::At(first, 0) == &list_a.header && il2cpp::At(first, 2) == nullptr && il2cpp::At(first, -1) == nullptr,
          "list At checks the index");
  il2cpp::List<Il2CppObject*>*  no_list  = nullptr;
  il2cpp::Array<Il2CppObject*>* no_array = nullptr;
  Require(il2cpp::Items(no_list).empty() && il2cpp::Items(no_array).empty() && il2cpp::At(no_list, 0) == nullptr
              && il2cpp::At(no_array, 0) == nullptr,
          "a null collection reads as empty");
  Require(first->HasMethod<int32_t()>("get_Count"), "runtime collection method schema");
  Require(!first->HasMethod<int32_t(int32_t)>("get_Item"), "runtime collection schema rejects a value result");

  first->Set(1, &other_object.header);
  Require(seen_self == &list_a.header && seen_int == 1 && list_a.target == &other_object.header,
          "list setter passes the receiver, index and value");
  Require(seen_method && seen_method->klass == &list_a_class.klass, "list setter receives its runtime MethodInfo");
  second->Set(2, nullptr);
  Require(seen_self == &list_b.header && seen_int == 2 && list_b.target == nullptr
              && seen_method->klass == &list_b_class.klass,
          "list setters resolve per runtime class and accept null");

  // Fully shared generic code is marshalled by the runtime.
  Require(base->Call<"Shared", int32_t>() == 41 && boxed_invocations == 1, "shared generic fallback");

  // Delegates bind static methods and can jump to native code.
  auto  target   = Base::Class::Method("OnEvent");
  auto* delegate = reinterpret_cast<Il2CppDelegate*>(
      il2cpp::NewDelegate(&action_class.klass, target, reinterpret_cast<Il2CppMethodPointer>(&NativeTarget)));
  Require(delegate && delegate_target == target.Info()
              && delegate->method_ptr == reinterpret_cast<Il2CppMethodPointer>(&NativeTarget),
          "static delegate");

  // Hook lookups by count and by parameter type names.
  Require(Base::Class::Method("Select", {"System.Boolean"}).Pointer() == reinterpret_cast<void*>(&Base_Select_Bool),
          "hook lookup by parameter types");
  Require(!Base::Class::Method("Absent", 0) && ErrorsMentioning("missing method Game.Base.Absent"), "missing hook");
  Require(Base::Class::Method("OnEvent").Name() == "Game.Base.OnEvent", "method name for diagnostics");

  Require(Base::Class::Method<void(bool)>("Select").Pointer() == reinterpret_cast<void*>(&Base_Select_Bool),
          "typed hook lookup selects the complete instance signature");
  Require(!Base::Class::Method<int32_t(bool)>("Select"), "typed hook lookup rejects a changed return type");
  Require(!Base::Class::Method<int32_t(int32_t)>("Twice"), "instance hook lookup rejects a static method");
  Require(!Base::Class::Method<int32_t()>("Shared"), "typed hook lookup rejects fully shared generic code");
  Require(Base::Class::HasField<"_count", int32_t>(), "instance field schema accepts the declared type");
  Require(!Base::Class::HasField<"_count", Other*>(), "instance field schema rejects an incompatible type");
  Require(!Base::Class::HasField<"s_flag", int32_t>(), "instance field schema rejects a static field");
  Require(Base::Class::HasProperty<"Count", int32_t>(), "property schema accepts the declared type");
  Require(!Base::Class::HasProperty<"Count", Other*>(), "property schema rejects an incompatible type");

  // Static hook lookups check the signature without running the class constructor ahead of the game.
  initialized.clear();
  Require(Base::Class::StaticMethod<int32_t(int32_t)>("Twice").Pointer() == reinterpret_cast<void*>(&Base_Twice),
          "typed static hook lookup");
  Require(initialized.empty(), "a static hook lookup leaves the class uninitialized");
  Require(!Base::Class::StaticMethod<void(bool)>("Select"), "static hook lookup rejects an instance method");

  // Boxed primitives unbox only as their own type.
  Il2CppObject boxed{};
  boxed.klass    = &int_class.klass;
  int32_t number = 0;
  int64_t wide   = 7;
  Require(il2cpp::Unbox(&boxed, number) && number == shared_result, "unbox the boxed type");
  Require(!il2cpp::Unbox(&boxed, wide) && wide == 7, "unbox rejects another type and leaves the value");
  Require(!il2cpp::Unbox(static_cast<Il2CppObject*>(nullptr), number), "unbox rejects null");

  // A hook set installs every hook or none: a missing member, or a null address, leaves its targets untouched.
  {
    const int       scaled_before = Scaled(2);
    il2cpp::HookSet incomplete("Incomplete");
    IL2CPP_STAGE(incomplete, il2cpp::Method(), Scaled_Hook);
    IL2CPP_STAGE_AT(incomplete, &Scaled, Scaled_Hook);
    Require(!incomplete.Commit() && Scaled(2) == scaled_before, "a set with a missing method installs nothing");
    Require(ErrorsMentioning("Incomplete not installed: a method it hooks is missing") == 1,
            "an incomplete set says what it cost");
#if defined(__x86_64__) || defined(_M_X64)
    const int       offset_before = Offset(2);
    il2cpp::HookSet complete("Complete");
    IL2CPP_STAGE_AT(complete, &Scaled, Scaled_Hook);
    IL2CPP_STAGE_AT(complete, &Offset, Offset_Hook);
    Require(complete.Commit() && Scaled(2) == scaled_before + 100 && Offset(2) == offset_before + 1000,
            "a complete set installs every hook");
#endif
  }

  // Managed strings convert to UTF-8, and compare with ASCII without converting.
  const auto utf16 = [](std::u16string_view text) {
    auto* str   = static_cast<Il2CppString*>(std::calloc(1, sizeof(Il2CppString) + text.size() * sizeof(Il2CppChar)));
    str->length = static_cast<int32_t>(text.size());
    std::copy(text.begin(), text.end(), str->chars);
    return str;
  };
  auto* mixed = utf16(u"K\u00E9rk \u2192 \U0001F596");
  Require(il2cpp::ToUtf8(mixed) == "K\xC3\xA9rk \xE2\x86\x92 \xF0\x9F\x96\x96",
          "UTF-16 to UTF-8 with a surrogate pair");
  auto* lone = utf16(std::u16string_view(u"a\xD800"
                                         "b",
                                         3));
  Require(il2cpp::ToUtf8(lone)
              == "a\xEF\xBF\xBD"
                 "b",
          "a lone surrogate becomes U+FFFD");
  Require(il2cpp::ToUtf8(nullptr).empty(), "null string");
  auto* ascii = utf16(u"Galaxy");
  Require(il2cpp::Equals(ascii, "Galaxy") && !il2cpp::Equals(ascii, "Galax") && !il2cpp::Equals(nullptr, ""),
          "ASCII comparison");
  for (auto* str : {mixed, lone, ascii})
    std::free(str);

  KeyboardSnapshot();
  InputFocus();
  std::cout << "IL2CPP binding tests passed\n";
}

// Typed bindings against a controlled managed runtime: lookup, signature checks, native calls,
// fields, dispatch, and failure reporting.
#include "il2cpp_test_api.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
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

std::deque<FakeClass>                         classes;
std::deque<MethodInfo>                        methods;
std::deque<std::vector<const Il2CppType*>>    parameter_lists;
std::deque<Il2CppType>                        field_types;
std::unordered_map<const Il2CppClass*, FakeClass*> registry;
std::vector<std::string>                      errors;
std::vector<const Il2CppClass*>               initialized;
int                                           method_lookups = 0;

FakeClass& Define(const char* namespaze, const char* name, Il2CppTypeEnum type, FakeClass* parent = nullptr,
                  int32_t value_size = 0)
{
  auto& fake               = classes.emplace_back();
  fake.klass.name          = name;
  fake.klass.namespaze     = namespaze;
  fake.klass.parent        = parent ? &parent->klass : nullptr;
  fake.klass.byval_arg.type = type;
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
FakeClass& void_class   = Define("System", "Void", IL2CPP_TYPE_VOID);
FakeClass& object_class = Define("System", "Object", IL2CPP_TYPE_OBJECT);
FakeClass& int_class    = Define("System", "Int32", IL2CPP_TYPE_I4, nullptr, 4);
FakeClass& bool_class   = Define("System", "Boolean", IL2CPP_TYPE_BOOLEAN, nullptr, 1);
FakeClass& float_class  = Define("System", "Single", IL2CPP_TYPE_R4, nullptr, 4);
FakeClass& string_class = Define("System", "String", IL2CPP_TYPE_STRING, &object_class);
FakeClass& type_class   = Define("System", "Type", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& vec3_class   = Define("UnityEngine", "Vector3", IL2CPP_TYPE_VALUETYPE, nullptr, sizeof(Vec3));
FakeClass& state_class  = Define("Game", "State", IL2CPP_TYPE_VALUETYPE, nullptr, 4);
FakeClass& base_class   = Define("Game", "Base", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& derived_class = Define("Game", "Derived", IL2CPP_TYPE_CLASS, &base_class);
FakeClass& other_class  = Define("Game", "Other", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& outer_class  = Define("Game", "Outer", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& inner_class  = Define("", "Inner", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& list_a_class = Define("Game", "ListA", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& list_b_class = Define("Game", "ListB", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& delegate_base = Define("System", "Delegate", IL2CPP_TYPE_CLASS, &object_class);
FakeClass& action_class = Define("System", "Action", IL2CPP_TYPE_CLASS, &delegate_base);
FakeClass& intptr_class = Define("System", "IntPtr", IL2CPP_TYPE_I, nullptr, sizeof(void*));

TestObject base_object{}, derived_object{}, other_object{}, list_a{}, list_b{}, allocated{};
Il2CppObject string_object{}, type_object{};
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
Il2CppException thrown{};
int32_t         Base_Explode(Il2CppObject*, const MethodInfo*)
{ throw Il2CppExceptionWrapper(&thrown); }
void Never(Il2CppObject*, const MethodInfo*)
{ throw std::runtime_error("shared generic method was called natively"); }

const MethodInfo* delegate_target = nullptr;
void              DelegateInvoker(Il2CppMethodPointer, const MethodInfo*, void* self, void** args, void*)
{
  // Delegate(object target, IntPtr method): the second argument points at the MethodInfo address.
  auto* delegate = static_cast<Il2CppDelegate*>(self);
  delegate_target = *static_cast<const MethodInfo**>(args[1]);
  delegate->method = delegate_target;
}
void NativeTarget()
{
}

MethodInfo* base_describe = nullptr;
MethodInfo* shared_method = nullptr;
int32_t     shared_result = 41;
int         boxed_invocations = 0;

void BuildRuntime()
{
  auto& get_count = AddMethod(base_class, "get_Count", reinterpret_cast<void*>(&Base_get_Count), T(int_class), {});
  auto& set_count = AddMethod(base_class, "set_Count", reinterpret_cast<void*>(&Base_set_Count), T(void_class),
                              {T(int_class)});
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
  auto& get_instance = AddMethod(base_class, "get_Instance", reinterpret_cast<void*>(&Base_get_Instance),
                                 T(base_class), {}, METHOD_ATTRIBUTE_PUBLIC | METHOD_ATTRIBUTE_STATIC);
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
  AddMethod(list_b_class, "get_Count", reinterpret_cast<void*>(&ListB_get_Count), T(int_class), {});
  AddMethod(list_b_class, "get_Item", reinterpret_cast<void*>(&List_get_Item), T(object_class), {T(int_class)});

  auto& ctor = AddMethod(action_class, ".ctor", nullptr, T(void_class), {T(object_class), T(intptr_class)});
  ctor.invoker_method = &DelegateInvoker;
  AddMethod(base_class, "OnEvent", reinterpret_cast<void*>(&NativeTarget), T(void_class), {},
            METHOD_ATTRIBUTE_PUBLIC | METHOD_ATTRIBUTE_STATIC);

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
  for (auto& fake : classes) {
    if (!std::strcmp(fake.klass.namespaze, namespaze) && !std::strcmp(fake.klass.name, name))
      return &fake.klass;
  }
  return nullptr;
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
  if (index >= klass->method_count)
    return nullptr;
  *iter = reinterpret_cast<void*>(index + 1);
  return klass->methods[index];
}
END_API
API(const MethodInfo*, il2cpp_class_get_method_from_name, (Il2CppClass * klass, const char* name, int count))
{
  ++method_lookups;
  for (; klass; klass = klass->parent) {
    for (uint16_t i = 0; i < klass->method_count; ++i) {
      auto* method = klass->methods[i];
      if (!std::strcmp(method->name, name) && (count < 0 || method->parameters_count == count))
        return method;
    }
  }
  return nullptr;
}
END_API
API(const PropertyInfo*, il2cpp_class_get_property_from_name, (Il2CppClass * klass, const char* name))
{
  for (; klass; klass = klass->parent) {
    for (auto& property : Fake(klass).properties) {
      if (!std::strcmp(property.name, name))
        return &property;
    }
  }
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
  for (; klass; klass = klass->parent) {
    for (auto& field : Fake(klass).fields) {
      if (!std::strcmp(field.name, name))
        return &field;
    }
  }
  return nullptr;
}
END_API
API(Il2CppClass*, il2cpp_class_from_type, (const Il2CppType* type))
{ return static_cast<Il2CppClass*>(type->data.dummy); }
END_API
API(const Il2CppType*, il2cpp_class_get_type, (Il2CppClass * klass))
{ return &klass->byval_arg; }
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
  for (; other; other = other->parent) {
    if (other == klass)
      return true;
  }
  return false;
}
END_API
API(void, il2cpp_runtime_class_init, (Il2CppClass * klass))
{ initialized.push_back(klass); }
END_API
API(const MethodInfo*, il2cpp_object_get_virtual_method, (Il2CppObject * object, const MethodInfo* method))
{
  for (auto* klass = object->klass; klass; klass = klass->parent) {
    for (uint16_t i = 0; i < klass->method_count; ++i) {
      if (!std::strcmp(klass->methods[i]->name, method->name))
        return klass->methods[i];
    }
  }
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
  auto size = Fake(static_cast<Il2CppClass*>(field->type->data.dummy)).value_size;
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
enum class State : int32_t { kIdle = 0, kBusy = 2 };

void Require(bool condition, const char* what)
{
  if (!condition)
    throw std::runtime_error(what);
}

size_t ErrorsMentioning(std::string_view text)
{
  size_t count = 0;
  for (auto& error : errors)
    count += error.find(text) != std::string::npos;
  return count;
}
} // namespace

int main()
{
  BuildRuntime();
  il2cpp::SetErrorHandler([](std::string_view message) { errors.emplace_back(message); });
  auto* base = reinterpret_cast<Base*>(&base_object);

  // Properties call the generated getter with the receiver and its MethodInfo.
  Require(base->Count() == 7 && seen_self == &base_object.header, "property getter receiver");
  Require(seen_method && !std::strcmp(seen_method->name, "get_Count"), "trailing MethodInfo");
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

  // Virtual calls dispatch on the receiver; non-virtual calls do not.
  auto* derived = reinterpret_cast<Derived*>(&derived_object);
  Require(!std::strcmp(base->Describe(), "base"), "base dispatch");
  Require(!std::strcmp(static_cast<Base*>(derived)->Describe(), "derived"), "virtual dispatch");

  // Static members initialize their class.
  Require(Base::Class::Call<"Twice", int32_t>(int32_t{21}) == 42, "static call");
  Require(Base::Class::Get<"Instance", Base*>() == base, "static property");
  Require(std::find(initialized.begin(), initialized.end(), &base_class.klass) != initialized.end(), "class init");

  // Construction runs the matching constructor.
  auto* made = il2cpp::New<Base>(int32_t{12});
  Require(made && made->klass == &base_class.klass && made->count() == 12, "construction");

  // Constructors are not inherited; a missing one is reported instead of running a base constructor.
  Require(il2cpp::New<Derived>(int32_t{1}) == nullptr && ErrorsMentioning("Game.Derived..ctor"), "ctor not inherited");

  // Lookups happen once per binding, not per call.
  const int lookups = method_lookups;
  for (int i = 0; i < 3; ++i)
    base->Call<"Select">(int32_t{i});
  Require(method_lookups == lookups, "cached method lookup");

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
  Require(base->Call<"Find", bool>(reinterpret_cast<Base*>(&other_object)) == false, "unrelated reference rejected");
  Require(native_calls == calls, "rejected members do not call native code");
  Require(il2cpp::Class<"Game", "Game", "Nope">::Raw() == nullptr && ErrorsMentioning("missing class Game:Game.Nope"),
          "missing class");

  // Managed exceptions are contained, reported once per method, and return the default.
  Require(base->Call<"Explode", int32_t>() == 0 && base->Call<"Explode", int32_t>() == 0, "exception contained");
  Require(ErrorsMentioning("Game.Base.Explode threw: NullReferenceException") == 1, "exception reported once");

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

  std::cout << "IL2CPP binding tests passed\n";
}

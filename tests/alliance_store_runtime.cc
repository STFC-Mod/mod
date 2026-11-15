// Exercise the production Alliance Games adapter against a controlled IL2CPP boundary.
// This fixture makes no Unity/rendering or native-detour claim.
#include <il2cpp-config.h>
#if _WIN32
#undef IL2CPP_IMPORT
#define IL2CPP_IMPORT
#endif
#include "patches/alliance_store_bindings.h"
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace alliance_store_game;

void Require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

Il2CppClass reference_class{}, child_class{}, foreign_class{}, boolean_class{}, int32_class{}, int64_class{};
Il2CppClass void_class{}, vector_class{}, foreign_vector_class{}, anchor_class{};
struct Instance {
  Object object{};
  alignas(8) unsigned char storage[32]{};
} receiver;
struct Box {
  Object object{};
  alignas(8) unsigned char data[16]{};
} box;
Object            argument{}, foreign_object{};
MethodInfo        method{}, override_method{};
const MethodInfo* methods[2]{};
const Il2CppType* parameters[3]{};
FieldInfo         field{};

Object*              returned          = nullptr;
bool                 managed_exception = false, null_unbox = false;
int                  invoke_calls = 0, virtual_calls = 0, field_reads = 0, field_writes = 0;
const MethodInfo*    seen_method = nullptr;
void*                seen_target = nullptr;
std::array<void*, 3> seen_arguments{};
int64_t              seen_int64   = 0;
bool                 seen_boolean = false;
Vector2              seen_vector{};
void*                seen_field_argument = nullptr;
bool                 drop_field_write = false, use_override = false;

struct HandleSlot {
  Object* target = nullptr;
  bool    live = false, weak = false;
};
std::array<HandleSlot, 16> handles{};
HandleSlot*                last_handle = nullptr;
bool                       fail_handle = false, mismatch_handle = false;
int                        handle_creations = 0, handle_frees = 0, weak_creations = 0;

Il2CppGCHandle NewHandle(Object* value, bool weak)
{
  if (fail_handle)
    return nullptr;
  for (auto& handle : handles) {
    if (handle.live)
      continue;
    handle      = {mismatch_handle ? &foreign_object : value, true, weak};
    last_handle = &handle;
    ++handle_creations;
    weak_creations += weak;
    return &handle;
  }
  throw std::runtime_error("fixture handle capacity exhausted");
}

void InitClass(Il2CppClass& cls, const char* space, const char* name, Il2CppTypeEnum type, bool value_type = false,
               uint32_t size = 0)
{
  cls.namespaze            = space;
  cls.name                 = name;
  cls.byval_arg.type       = type;
  cls.byval_arg.valuetype  = value_type;
  cls.byval_arg.data.dummy = &cls;
  cls.element_size         = size;
  cls.instance_size        = sizeof(Instance);
}

void InitMethod(const char* name, Il2CppClass& result, std::initializer_list<const Il2CppType*> args = {})
{
  method                  = {};
  method.name             = name;
  method.klass            = &reference_class;
  method.return_type      = &result.byval_arg;
  method.parameters_count = static_cast<uint8_t>(args.size());
  size_t index            = 0;
  for (auto* type : args)
    parameters[index++] = type;
  method.parameters = parameters;
  // The adapter checks this invoker; the mock runtime owns invocation itself.
  method.invoker_method        = reinterpret_cast<InvokerMethod>(1);
  methods[0]                   = &method;
  reference_class.methods      = methods;
  reference_class.method_count = 1;
  receiver.object.klass        = &reference_class;
  returned                     = nullptr;
  managed_exception = use_override = null_unbox = false;
  seen_arguments                                = {};
}

template <class T> void ReturnBox(Il2CppClass& cls, T value)
{
  static_assert(sizeof(T) <= sizeof(box.data));
  box.object.klass = &cls;
  std::memcpy(box.data, &value, sizeof(value));
  returned = &box.object;
}

void InitField(Il2CppClass& type)
{
  field                       = {};
  field.name                  = "value";
  field.type                  = &type.byval_arg;
  field.parent                = &reference_class;
  field.offset                = offsetof(Instance, storage);
  reference_class.fields      = &field;
  reference_class.field_count = 1;
  drop_field_write            = false;
  receiver.object.klass       = &reference_class;
  std::memset(receiver.storage, 0, sizeof(receiver.storage));
}

size_t FieldSize(FieldInfo* value)
{
  auto* cls = static_cast<Il2CppClass*>(value->type->data.dummy);
  return cls->byval_arg.valuetype ? cls->element_size : sizeof(Object*);
}
} // namespace

// Windows imports API functions; macOS resolves API function pointers.
#if _WIN32
#define API(ret, name, params) extern "C" ret name params
#define END_API
#else
#define API(ret, name, params) name##_t name = +[] params->ret
#define END_API ;
#endif
API(Il2CppClass*, il2cpp_class_from_type, (const Il2CppType* type))
{ return type ? static_cast<Il2CppClass*>(type->data.dummy) : nullptr; }
END_API
API(const Il2CppType*, il2cpp_class_get_type, (Il2CppClass * cls))
{ return cls ? &cls->byval_arg : nullptr; }
END_API
API(bool, il2cpp_class_is_valuetype, (const Il2CppClass* cls))
{ return cls && cls->byval_arg.valuetype; }
END_API
API(bool, il2cpp_class_is_enum, (const Il2CppClass* cls))
{ return cls && cls->enumtype; }
END_API
API(const Il2CppType*, il2cpp_class_enum_basetype, (Il2CppClass * cls))
{ return cls == &anchor_class ? &int32_class.byval_arg : nullptr; }
END_API
API(int32_t, il2cpp_class_value_size, (Il2CppClass * cls, uint32_t* alignment))
{
  *alignment = 4;
  return static_cast<int32_t>(cls->element_size);
}
END_API
API(int32_t, il2cpp_class_instance_size, (Il2CppClass * cls))
{ return static_cast<int32_t>(cls->instance_size); }
END_API
API(Il2CppClass*, il2cpp_class_get_parent, (Il2CppClass * cls))
{ return cls ? cls->parent : nullptr; }
END_API
API(bool, il2cpp_class_is_assignable_from, (Il2CppClass * expected, Il2CppClass* actual))
{
  for (; actual; actual = actual->parent)
    if (actual == expected)
      return true;
  return false;
}
END_API
API(const MethodInfo*, il2cpp_class_get_methods, (Il2CppClass * cls, void** iterator))
{
  const auto index = reinterpret_cast<uintptr_t>(*iterator);
  if (index >= cls->method_count)
    return nullptr;
  *iterator = reinterpret_cast<void*>(index + 1);
  return cls->methods[index];
}
END_API
API(const MethodInfo*, il2cpp_object_get_virtual_method, (Object*, const MethodInfo* selected))
{
  ++virtual_calls;
  return use_override && selected == &method ? &override_method : selected;
}
END_API
API(char*, il2cpp_type_get_name, (const Il2CppType* type))
{
  auto* cls = type ? static_cast<Il2CppClass*>(type->data.dummy) : nullptr;
  if (!cls)
    return nullptr;
  const auto name = std::string(cls->namespaze) + "." + cls->name;
  auto*      copy = static_cast<char*>(std::malloc(name.size() + 1));
  Require(copy != nullptr, "fixture type-name allocation");
  std::memcpy(copy, name.c_str(), name.size() + 1);
  return copy;
}
END_API
API(void, il2cpp_free, (void* value))
{ std::free(value); }
END_API
API(void*, il2cpp_object_unbox, (Object * object))
{ return null_unbox ? nullptr : reinterpret_cast<Box*>(object)->data; }
END_API
API(Object*, il2cpp_runtime_invoke, (const MethodInfo* selected, void* target, void** args, Il2CppException** error))
{
  ++invoke_calls;
  seen_method    = selected;
  seen_target    = target;
  seen_arguments = {};
  for (size_t i = 0; args && i < selected->parameters_count && i < seen_arguments.size(); ++i) {
    seen_arguments[i] = args[i];
    if (selected->parameters[i] == &int64_class.byval_arg)
      std::memcpy(&seen_int64, args[i], sizeof(seen_int64));
    else if (selected->parameters[i] == &boolean_class.byval_arg)
      std::memcpy(&seen_boolean, args[i], sizeof(seen_boolean));
    else if (selected->parameters[i] == &vector_class.byval_arg)
      std::memcpy(&seen_vector, args[i], sizeof(seen_vector));
  }
  if (managed_exception)
    *error = reinterpret_cast<Il2CppException*>(&foreign_object);
  return returned;
}
END_API
API(FieldInfo*, il2cpp_class_get_field_from_name, (Il2CppClass * cls, const char* name))
{
  for (size_t i = 0; i < cls->field_count; ++i)
    if (std::strcmp(cls->fields[i].name, name) == 0)
      return &cls->fields[i];
  return nullptr;
}
END_API
API(int, il2cpp_field_get_flags, (FieldInfo * value))
{ return value->type->attrs; }
END_API
API(void, il2cpp_field_get_value, (Object * object, FieldInfo* value, void* output))
{
  ++field_reads;
  std::memcpy(output, reinterpret_cast<unsigned char*>(object) + value->offset, FieldSize(value));
}
END_API
API(void, il2cpp_field_set_value, (Object * object, FieldInfo* value, void* input))
{
  ++field_writes;
  seen_field_argument = input;
  if (drop_field_write)
    return;
  auto* destination = reinterpret_cast<unsigned char*>(object) + value->offset;
  if (static_cast<Il2CppClass*>(value->type->data.dummy)->byval_arg.valuetype)
    std::memcpy(destination, input, FieldSize(value));
  else
    std::memcpy(destination, &input, sizeof(Object*)); // Reference itself, not its address.
}
END_API
API(Il2CppGCHandle, il2cpp_gchandle_new, (Object * object, bool pinned))
{
  Require(!pinned, "adapter must not pin its short-lived roots");
  return NewHandle(object, false);
}
END_API
API(Il2CppGCHandle, il2cpp_gchandle_new_weakref, (Object * object, bool resurrection))
{
  Require(!resurrection, "weak Unity identities must not track resurrection");
  return NewHandle(object, true);
}
END_API
API(Object*, il2cpp_gchandle_get_target, (Il2CppGCHandle value))
{
  auto* handle = static_cast<HandleSlot*>(value);
  return handle && handle->live ? handle->target : nullptr;
}
END_API
API(void, il2cpp_gchandle_free, (Il2CppGCHandle value))
{
  auto* handle = static_cast<HandleSlot*>(value);
  Require(handle && handle->live, "handle freed twice");
  handle->live = false;
  ++handle_frees;
}
END_API

namespace
{
void CallCases()
{
  InitMethod("get_Reference", reference_class);
  const auto null_reference = Call<Object*>(&receiver.object, "get_Reference");
  Require(null_reference && !null_reference.value, "successful null reference is known");
  const auto before_missing = invoke_calls;
  Require(!Call<Object*>(&receiver.object, "missing") && invoke_calls == before_missing,
          "missing method must not invoke");
  managed_exception = true;
  Require(!Call<Object*>(&receiver.object, "get_Reference"), "managed exception is unknown, not known null");
  managed_exception = false;
  returned          = &foreign_object;
  Require(!Call<Object*>(&receiver.object, "get_Reference"), "foreign reference result must be rejected");

  InitMethod("get_Id", int64_class);
  ReturnBox(int64_class, int64_t{0});
  const auto zero = Call<int64_t>(&receiver.object, "get_Id");
  Require(zero && zero.value == 0, "valid Int64 zero must remain known");
  ReturnBox(int64_class, int64_t{0x100000007});
  Require(Call<int64_t>(&receiver.object, "get_Id").value == 0x100000007, "Int64 must preserve high bits");
  method.return_type      = &int32_class.byval_arg;
  const auto before_width = invoke_calls;
  Require(!Call<int64_t>(&receiver.object, "get_Id") && invoke_calls == before_width,
          "Int32 metadata is not an Int64 getter");
  method.return_type = &int64_class.byval_arg;
  ReturnBox(int32_class, int32_t{0});
  Require(!Call<int64_t>(&receiver.object, "get_Id"), "Int32 box is not known Int64 zero");
  returned = nullptr;
  Require(!Call<int64_t>(&receiver.object, "get_Id"), "null scalar result is unknown");
  ReturnBox(int64_class, int64_t{0});
  fail_handle = true;
  Require(!Call<int64_t>(&receiver.object, "get_Id"), "failed box custody is unknown, not zero");
  fail_handle = false;
  null_unbox  = true;
  Require(!Call<int64_t>(&receiver.object, "get_Id"), "missing unbox data is unknown");

  InitMethod("get_Eligible", boolean_class);
  ReturnBox(boolean_class, false);
  const auto known_false = Call<bool>(&receiver.object, "get_Eligible");
  Require(known_false && !known_false.value, "valid false must remain known");
  managed_exception = true;
  Require(!Call<bool>(&receiver.object, "get_Eligible"), "failed eligibility is unknown, not false");

  InitMethod("get_NormalizedPosition", vector_class);
  ReturnBox(vector_class, Vector2{0.25f, 0.75f});
  const auto position = NormalizedPosition(&receiver.object);
  Require(position && position.value.x == 0.25f && position.value.y == 0.75f, "exact Vector2 return");
  ReturnBox(foreign_vector_class, Vector2{0.25f, 0.75f});
  Require(!NormalizedPosition(&receiver.object), "same-sized foreign value is not Vector2");
  method.return_type        = &foreign_vector_class.byval_arg;
  const auto before_foreign = invoke_calls;
  Require(!NormalizedPosition(&receiver.object) && invoke_calls == before_foreign, "foreign Vector2 signature");
}

void SignatureAndArgumentsCases()
{
  InitMethod("Apply", void_class, {&int64_class.byval_arg, &reference_class.byval_arg, &boolean_class.byval_arg});
  int64_t id      = 0x100000009;
  bool    enabled = false;
  Require(Call<void>(&receiver.object, "Apply",
                     {Scalar(id, "System.Int64"), Ref(&argument), Scalar(enabled, "System.Boolean")})
              .succeeded,
          "void success with scalar and reference arguments");
  Require(seen_target == &receiver.object && seen_arguments[0] == &id && seen_int64 == id
              && seen_arguments[1] == &argument && seen_arguments[2] == &enabled && !seen_boolean,
          "scalar addresses and direct references must retain their distinct ABI");
  const auto before_invalid = invoke_calls;
  Require(!Call<void>(&receiver.object, "Apply",
                      {Scalar(id, "System.Int32"), Ref(&argument), Scalar(enabled, "System.Boolean")}),
          "wrong scalar signature");
  Require(!Call<void>(&receiver.object, "Apply",
                      {Scalar(id, "System.Int64"), Ref(&foreign_object), Scalar(enabled, "System.Boolean")}),
          "foreign reference argument");
  Require(invoke_calls == before_invalid, "invalid argument metadata must not invoke");
  Require(Call<void>(&receiver.object, "Apply",
                     {Scalar(id, "System.Int64"), Ref(nullptr), Scalar(enabled, "System.Boolean")})
              .succeeded,
          "null reference argument is valid");

  InitMethod("get_Reference", reference_class);
  Require(Signature<Object*>(&method, false, {}), "valid instance signature");
  method.is_generic = true;
  Require(!Signature<Object*>(&method, false, {}), "open generic method must be rejected");
  method.is_generic  = false;
  method.is_inflated = true;
  Require(Call<Object*>(&receiver.object, "get_Reference").succeeded, "closed accessor metadata remains usable");
  method.invoker_method = nullptr;
  Require(!Signature<Object*>(&method, false, {}), "missing invoker must be rejected");
  method.invoker_method        = reinterpret_cast<InvokerMethod>(1);
  MethodInfo duplicate         = method;
  methods[1]                   = &duplicate;
  reference_class.method_count = 2;
  const auto before_ambiguous  = invoke_calls;
  Require(!Call<Object*>(&receiver.object, "get_Reference") && invoke_calls == before_ambiguous,
          "ambiguous compatible overloads must not invoke");
  reference_class.method_count = 1;
  method.flags                 = METHOD_ATTRIBUTE_STATIC;
  Require(Call<Object*>(nullptr, "get_Reference", {}, &reference_class).succeeded && !seen_target,
          "static null reference success");
  Require(!Call<Object*>(&receiver.object, "get_Reference"), "static method is not an instance callback");

  InitMethod("OnValueChangedCallback", void_class, {&vector_class.byval_arg});
  receiver.object.klass     = &child_class; // Method is inherited from the base scroller.
  override_method           = method;
  override_method.klass     = &child_class;
  use_override              = true;
  const auto before_virtual = virtual_calls;
  Require(NotifyPosition(&receiver.object, {0.125f, 0.875f}), "virtual position notification success");
  Require(virtual_calls == before_virtual + 1 && seen_method == &override_method && seen_target == &receiver.object
              && seen_vector.x == 0.125f && seen_vector.y == 0.875f,
          "notification must use the selected override and copied Vector2 argument");
  override_method.return_type    = &boolean_class.byval_arg;
  const auto before_bad_override = invoke_calls;
  Require(!NotifyPosition(&receiver.object, {}) && invoke_calls == before_bad_override,
          "virtual override signature must be requalified");
}

void FieldCases()
{
  InitField(boolean_class);
  Field<bool> flag;
  Require(flag.Resolve(&child_class, "value"), "inherited boolean field binding");
  Require(flag.Write(&receiver.object, true) && flag.Write(&receiver.object, false) && !flag.Read(&receiver.object),
          "boolean field preserves false and verifies writeback");
  drop_field_write = true;
  Require(!flag.Write(&receiver.object, true), "failed scalar writeback must be visible");
  drop_field_write          = false;
  const auto before_foreign = field_writes;
  Require(!flag.Write(&foreign_object, true) && field_writes == before_foreign, "foreign field owner rejected");
  Require(!flag.Resolve(&reference_class, "missing"), "rebinding missing field must clear prior binding");

  InitField(anchor_class);
  Field<int32_t> anchor;
  Require(anchor.Resolve(&reference_class, "value", "Digit.Client.Tooltip.TooltipAnchor"), "Int32 enum field");
  Require(anchor.Write(&receiver.object, -1) && anchor.Read(&receiver.object) == -1, "Undefined anchor is valid");
  Require(anchor.Write(&receiver.object, 0) && anchor.Read(&receiver.object) == 0, "zero anchor is valid");

  InitField(reference_class);
  Field<Object*> reference;
  Require(reference.Resolve(&reference_class, "value", "Tests.Reference"), "reference field binding");
  Require(reference.Write(&receiver.object, &argument) && seen_field_argument == &argument
              && reference.Read(&receiver.object) == &argument,
          "reference write takes the object itself");
  const auto before_bad_reference = field_writes;
  Require(!reference.Write(&receiver.object, &foreign_object) && field_writes == before_bad_reference,
          "foreign reference field value rejected");
  Require(reference.Write(&receiver.object, nullptr) && !reference.Read(&receiver.object), "null reference write");

  InitField(vector_class);
  Field<Vector2> vector;
  Require(vector.Resolve(&reference_class, "value") && vector.Write(&receiver.object, {0.5f, 0.75f}),
          "Vector2 field uses value storage");
  const auto observed = vector.Read(&receiver.object);
  Require(observed.x == 0.5f && observed.y == 0.75f, "Vector2 field readback");
  InitField(foreign_vector_class);
  Require(!vector.Resolve(&reference_class, "value"), "same-sized foreign field is not Vector2");
  InitField(boolean_class);
  boolean_class.byval_arg.attrs = FIELD_ATTRIBUTE_STATIC;
  Require(!flag.Resolve(&reference_class, "value"), "static field rejected");
  boolean_class.byval_arg.attrs = 0;
  field.offset                  = sizeof(Instance);
  Require(!flag.Resolve(&reference_class, "value"), "field beyond object extent rejected");
  field.offset = 0;
  Require(!flag.Resolve(&reference_class, "value"), "field overlapping object header rejected");
}

void HandleCases()
{
  const auto before = handle_frees;
  {
    auto first = Handle::Strong(&argument);
    Require(first && first.Get() == &argument, "strong handle custody");
    auto moved = std::move(first);
    Require(!first && moved.Get() == &argument, "move transfers handle custody");
    auto replacement = Handle::Strong(&receiver.object);
    replacement      = std::move(moved);
    Require(!moved && replacement.Get() == &argument && handle_frees == before + 1,
            "move assignment releases old custody exactly once");
    replacement.Reset();
    replacement.Reset();
  }
  Require(handle_frees == before + 2, "reset/destruction do not double free");
  {
    const auto weak_before = weak_creations;
    auto       weak        = Handle::Weak(&argument);
    Require(weak && weak_creations == weak_before + 1, "weak Unity identity handle");
    last_handle->target = nullptr; // Simulated collection of a weak target.
    Require(weak && !weak.Get(), "collected weak target remains distinct from allocation failure");
  }
  fail_handle = true;
  Require(!Handle::Strong(&argument), "failed strong allocation");
  {
    Roots roots;
    Require(!roots.Keep(nullptr) && roots.healthy, "null reference requires no custody");
    Require(!roots.Keep(&argument) && !roots.healthy, "failed root marks operation unhealthy");
  }
  fail_handle                = false;
  mismatch_handle            = true;
  const auto before_mismatch = handle_frees;
  Require(!Handle::Strong(&argument) && handle_frees == before_mismatch + 1,
          "wrong handle target releases acquired handle");
  mismatch_handle = false;
  {
    Roots roots;
    Require(roots.Keep(&argument) == &argument && roots.Keep(&receiver.object) == &receiver.object && roots.healthy,
            "operation roots keep successful custody");
  }
  Require(handle_creations == handle_frees, "all acquired handles must be released");
}
} // namespace

int main()
{
  InitClass(reference_class, "Tests", "Reference", IL2CPP_TYPE_CLASS);
  InitClass(child_class, "Tests", "Child", IL2CPP_TYPE_CLASS);
  child_class.parent = &reference_class;
  InitClass(foreign_class, "Tests", "Foreign", IL2CPP_TYPE_CLASS);
  InitClass(boolean_class, "System", "Boolean", IL2CPP_TYPE_BOOLEAN, true, sizeof(bool));
  InitClass(int32_class, "System", "Int32", IL2CPP_TYPE_I4, true, sizeof(int32_t));
  InitClass(int64_class, "System", "Int64", IL2CPP_TYPE_I8, true, sizeof(int64_t));
  InitClass(void_class, "System", "Void", IL2CPP_TYPE_VOID, true);
  InitClass(vector_class, "UnityEngine", "Vector2", IL2CPP_TYPE_VALUETYPE, true, sizeof(Vector2));
  InitClass(foreign_vector_class, "Tests", "ForeignVector", IL2CPP_TYPE_VALUETYPE, true, sizeof(Vector2));
  InitClass(anchor_class, "Digit.Client.Tooltip", "TooltipAnchor", IL2CPP_TYPE_VALUETYPE, true, sizeof(int32_t));
  anchor_class.enumtype = true;
  argument.klass        = &child_class;
  foreign_object.klass  = &foreign_class;
  CallCases();
  SignatureAndArgumentsCases();
  FieldCases();
  HandleCases();
  std::cout << "Alliance Games runtime adapter regressions passed\n";
}

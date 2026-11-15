// Exercise the production checked helpers against controlled IL2CPP metadata.
#include <il2cpp-config.h>
#if _WIN32
#undef IL2CPP_IMPORT
#define IL2CPP_IMPORT
#endif
#include "il2cpp/il2cpp_checked.h"
#include <iostream>

namespace
{
Il2CppClass       reference_class, boolean_class, other_class;
Il2CppType        reference_type{}, boolean_type{};
Il2CppObject      receiver{}, argument{}, result{};
MethodInfo        method{};
FieldInfo         field{};
const Il2CppType* parameters[]  = {&reference_type};
int               invocations   = 0;
bool              throw_managed = false, boxed_value = false;
void**            observed_args = nullptr;
} // namespace

// Windows imports functions; macOS resolves API function pointers.
#if _WIN32
#define API(ret, name, params) extern "C" ret name params
#define END_API
#else
#define API(ret, name, params) name##_t name = +[] params->ret
#define END_API ;
#endif
API(const MethodInfo*, il2cpp_class_get_method_from_name, (Il2CppClass*, const char*, int))
{ return &method; }
END_API
API(FieldInfo*, il2cpp_class_get_field_from_name, (Il2CppClass*, const char*))
{ return &field; }
END_API
API(Il2CppClass*, il2cpp_class_from_type, (const Il2CppType* type))
{ return type == &reference_type ? &reference_class : &boolean_class; }
END_API
API(bool, il2cpp_class_is_valuetype, (const Il2CppClass* cls))
{ return cls == &boolean_class; }
END_API
API(bool, il2cpp_class_is_assignable_from, (Il2CppClass * a, Il2CppClass* b))
{ return a == b; }
END_API
API(const Il2CppType*, il2cpp_class_get_type, (Il2CppClass * cls))
{ return cls == &boolean_class ? &boolean_type : &reference_type; }
END_API
API(void*, il2cpp_object_unbox, (Il2CppObject*))
{ return &boxed_value; }
END_API
API(Il2CppObject*, il2cpp_field_get_value_object, (FieldInfo*, Il2CppObject*))
{ return &result; }
END_API
API(Il2CppObject*, il2cpp_runtime_invoke, (const MethodInfo*, void*, void** args, Il2CppException** exception))
{
  ++invocations;
  observed_args = args;
  if (throw_managed)
    *exception = reinterpret_cast<Il2CppException*>(&result);
  return &result;
}
END_API

void Require(bool condition)
{
  if (!condition)
    throw std::runtime_error("test failed");
}
template <class F> void Reject(F operation)
{
  try {
    operation();
  } catch (const std::runtime_error&) {
    return;
  }
  throw std::runtime_error("expected rejection");
}
int main()
{
  reference_class.name = "Reference";
  boolean_class.name   = "Boolean";
  reference_type.type  = IL2CPP_TYPE_CLASS;
  boolean_type.type    = IL2CPP_TYPE_BOOLEAN;
  receiver.klass = argument.klass = &reference_class;
  result.klass                    = &boolean_class;
  method.return_type              = &boolean_type;
  method.parameters               = parameters;
  method.parameters_count         = 1;
  method.invoker_method           = reinterpret_cast<InvokerMethod>(1); // checked but never called by the mock runtime
  void* args[]                    = {&argument};
  auto  invoke                    = [&] { return Il2CppChecked::Invoke(&receiver, "Method", 1, args); };
  Require(invoke() == &result && invocations == 1 && observed_args == args && args[0] == &argument);
  args[0] = nullptr;
  Require(invoke() == &result && invocations == 2); // null managed reference is legal
  args[0]        = &argument;
  argument.klass = &other_class;
  Reject(invoke);
  argument.klass       = &reference_class;
  reference_type.byref = true;
  Reject(invoke);
  reference_type.byref = false;
  parameters[0]        = &boolean_type;
  Reject(invoke);
  parameters[0] = &reference_type;
  method.flags  = METHOD_ATTRIBUTE_STATIC;
  Reject(invoke);
  method.flags            = 0;
  method.parameters_count = 0;
  Reject(invoke);
  method.parameters_count = 1;
  Require(invocations == 2); // invalid metadata must never reach the managed runtime
  throw_managed = true;
  Reject(invoke);
  throw_managed = false;
  boxed_value   = false;
  Require(!Il2CppChecked::Boolean(&result));
  boxed_value = true;
  Require(Il2CppChecked::Boolean(&result));
  result.klass = &reference_class;
  Reject([&] { Il2CppChecked::Boolean(&result); });
  field.type = &reference_type;
  Require(Il2CppChecked::ReferenceField(&receiver, "Field", "Reference") == &result);
  reference_type.attrs = FIELD_ATTRIBUTE_STATIC;
  Reject([&] { Il2CppChecked::ReferenceField(&receiver, "Field", "Reference"); });
  reference_type.attrs = 0;
  field.type           = &boolean_type;
  Reject([&] { Il2CppChecked::ReferenceField(&receiver, "Field", "Boolean"); });
  result.klass = &boolean_class;
  Require(Il2CppChecked::BooleanField(&receiver, "Flag"));
  Require(Il2CppChecked::Invoke(nullptr, "Method") == nullptr);
  Require(Il2CppChecked::ReferenceField(nullptr, "Field", "Reference") == nullptr);
  std::cout << "Checked IL2CPP invocation/field tests passed\n";
}

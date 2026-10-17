#include "settings/native_boolean_callback.h"
#include <cassert>
#include <il2cpp-object-internals.h>
#include <iostream>

namespace
{
int  donorCalls = 0, calls = 0;
bool Donor(Il2CppObject*, const MethodInfo*)
{
  ++donorCalls;
  return false;
}
bool Getter(Il2CppObject*, const MethodInfo*)
{
  ++calls;
  return true;
}
void Setter(Il2CppObject*, bool value, const MethodInfo*)
{ calls += value ? 10 : 20; }
Il2CppObject* invokedTarget = nullptr;
void          Command(Il2CppObject* target, const MethodInfo*)
{ invokedTarget = target; }
Il2CppString  text{};
Il2CppString* TextGetter(Il2CppObject*, const MethodInfo*)
{ return &text; }
} // namespace
int main()
{
  Il2CppType boolType{}, voidType{};
  boolType.type = IL2CPP_TYPE_BOOLEAN;
  voidType.type = IL2CPP_TYPE_VOID;
  MethodInfo schema{};
  schema.return_type          = &boolType;
  schema.methodPointer        = reinterpret_cast<Il2CppMethodPointer>(Donor);
  schema.virtualMethodPointer = schema.methodPointer;
  mod_settings::NativeCallback<bool> get;
  assert(get.Initialize(&schema, Getter));
  assert(!get.Initialize(&schema, Getter));
  assert(schema.methodPointer == reinterpret_cast<Il2CppMethodPointer>(Donor));
  auto* owned  = get.method();
  auto  direct = reinterpret_cast<decltype(&Getter)>(owned->methodPointer);
  assert(direct(nullptr, owned));
  auto virt = reinterpret_cast<decltype(&Getter)>(owned->virtualMethodPointer);
  assert(virt(nullptr, owned));
  bool value = false;
  owned->invoker_method(schema.methodPointer, owned, nullptr, nullptr, &value);
  assert(value && calls == 3 && donorCalls == 0);
  schema.return_type             = &voidType;
  const Il2CppType* parameters[] = {&boolType};
  schema.parameters_count        = 1;
  schema.parameters              = parameters;
  mod_settings::NativeCallback<void, bool> set;
  assert(set.Initialize(&schema, Setter));
  void* args[] = {&value};
  owned        = set.method();
  owned->invoker_method(nullptr, owned, nullptr, args, nullptr);
  assert(calls == 13 && donorCalls == 0);
  mod_settings::NativeCallback<bool> invalid;
  assert(!invalid.Initialize(&schema, Getter));
  schema.parameters_count = 0;
  schema.parameters       = nullptr;
  schema.flags            = METHOD_ATTRIBUTE_SPECIAL_NAME | METHOD_ATTRIBUTE_RT_SPECIAL_NAME;
  mod_settings::NativeCallback<void> command;
  assert(command.Initialize(&schema, Command));
  Il2CppObject token{};
  command.method()->invoker_method(nullptr, command.method(), &token, nullptr, nullptr);
  assert(invokedTarget == &token);
  invokedTarget = nullptr;
  reinterpret_cast<decltype(&Command)>(command.method()->methodPointer)(&token, command.method());
  assert(invokedTarget == &token && donorCalls == 0);
  schema.return_type = &boolType;
  schema.flags       = METHOD_ATTRIBUTE_VIRTUAL;
  assert(!invalid.Initialize(&schema, Getter));
  schema.flags       = 0;
  schema.is_inflated = true;
  assert(!invalid.Initialize(&schema, Getter));
  Il2CppType stringType{};
  stringType.type = IL2CPP_TYPE_STRING;
  MethodInfo stringSchema{};
  stringSchema.return_type = &stringType;
  mod_settings::NativeCallback<Il2CppString*> stringGet;
  assert(stringGet.Initialize(&stringSchema, TextGetter));
  auto*         stringMethod = stringGet.method();
  Il2CppString* result       = nullptr;
  stringMethod->invoker_method(nullptr, stringMethod, nullptr, nullptr, &result);
  assert(result == &text);
  assert(reinterpret_cast<decltype(&TextGetter)>(stringMethod->methodPointer)(nullptr, stringMethod) == &text);
  assert(reinterpret_cast<decltype(&TextGetter)>(stringMethod->virtualMethodPointer)(nullptr, stringMethod) == &text);
  mod_settings::NativeCallback<Il2CppString*> wrongString;
  assert(!wrongString.Initialize(&schema, TextGetter));
  std::cout << "PASS owned callback: direct, virtual and runtime invoker paths; donor remains untouched\n";
}

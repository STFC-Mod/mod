#pragma once

#include <array>
#include <cstddef>
#include <il2cpp-class-internals.h>
#include <il2cpp-tabledefs.h>
#include <type_traits>
#include <utility>

namespace mod_settings
{
// Process-lifetime schema, owned by the registration, not a settings page. The
// donor supplies only matching reflection metadata; every executable path is
// replaced. Never modify a MethodInfo owned by IL2CPP.
template <typename Result, typename... Args> class NativeCallback
{
public:
  using Function                                   = Result (*)(Il2CppObject*, Args..., const MethodInfo*);
  NativeCallback()                                 = default;
  NativeCallback(const NativeCallback&)            = delete;
  NativeCallback& operator=(const NativeCallback&) = delete;
  bool            Initialize(const MethodInfo* schema, Function callback)
  {
    if (initialized_ || !schema || !callback || schema->is_generic || schema->is_inflated
        || schema->has_full_generic_sharing_signature || (schema->flags & METHOD_ATTRIBUTE_STATIC)
        || (schema->flags & METHOD_ATTRIBUTE_VIRTUAL) || schema->parameters_count != sizeof...(Args))
      return false;
    if (!schema->return_type || schema->return_type->byref)
      return false;
    if constexpr (std::is_void_v<Result>) {
      if (schema->return_type->type != IL2CPP_TYPE_VOID)
        return false;
    } else if constexpr (std::is_same_v<Result, bool>) {
      if (schema->return_type->type != IL2CPP_TYPE_BOOLEAN)
        return false;
    } else if constexpr (std::is_same_v<Result, float>) {
      if (schema->return_type->type != IL2CPP_TYPE_R4)
        return false;
    } else if constexpr (std::is_same_v<Result, Il2CppString*>) {
      if (schema->return_type->type != IL2CPP_TYPE_STRING)
        return false;
    } else {
      static_assert(std::is_same_v<Result, int>, "Only boolean, void, Single and Int32 callbacks are supported");
      // Enum users additionally validate the Int32 backing type in the adapter.
      if (schema->return_type->type != IL2CPP_TYPE_VALUETYPE && schema->return_type->type != IL2CPP_TYPE_I4)
        return false;
    }
    static_assert(((std::is_same_v<Args, bool> || std::is_same_v<Args, int> || std::is_same_v<Args, float>) && ...),
                  "Only boolean, Single and Int32 callback arguments are supported");
    constexpr std::array<int, sizeof...(Args)> types{(std::is_same_v<Args, bool>    ? IL2CPP_TYPE_BOOLEAN
                                                      : std::is_same_v<Args, float> ? IL2CPP_TYPE_R4
                                                                                    : IL2CPP_TYPE_I4)...};
    for (std::size_t i = 0; i < sizeof...(Args); ++i)
      if (!schema->parameters || !schema->parameters[i] || schema->parameters[i]->byref
          || schema->parameters[i]->type != types[i])
        return false;
    method_                      = *schema;
    method_.methodPointer        = reinterpret_cast<Il2CppMethodPointer>(callback);
    method_.virtualMethodPointer = method_.methodPointer;
    method_.invoker_method       = Invoke;
    initialized_                 = true;
    return true;
  }
  const MethodInfo* method() const
  { return initialized_ ? &method_ : nullptr; }

private:
  template <std::size_t... I>
  static void Call(const MethodInfo* method, void* object, void** args, void* result, std::index_sequence<I...>)
  {
    auto callback = reinterpret_cast<Function>(method->methodPointer);
    if constexpr (std::is_void_v<Result>)
      callback(static_cast<Il2CppObject*>(object), *static_cast<Args*>(args[I])..., method);
    else
      *static_cast<Result*>(result) =
          callback(static_cast<Il2CppObject*>(object), *static_cast<Args*>(args[I])..., method);
  }
  static void Invoke(Il2CppMethodPointer, const MethodInfo* method, void* object, void** args, void* result)
  { Call(method, object, args, result, std::index_sequence_for<Args...>{}); }
  MethodInfo method_{};
  bool       initialized_ = false;
};
} // namespace mod_settings

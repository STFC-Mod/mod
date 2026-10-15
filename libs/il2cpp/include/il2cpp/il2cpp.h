#pragma once

// Typed access to the game's managed classes. Bindings name a managed class once, then read
// properties and fields and call methods by name. Member lookups and signature checks happen
// once. After that, calls jump straight to the generated native code.

#include "api.h"
#include <il2cpp-tabledefs.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace il2cpp
{
// Missing classes and members, and signatures that no longer match the binding, are reported
// once per lookup. Without a handler, reports go to stderr.
void SetErrorHandler(void (*handler)(std::string_view message));
// Sends a message through the same handler; for libraries built on this one.
void Report(std::string_view message);
// "Namespace.Outer/Inner", for diagnostics.
std::string ClassName(Il2CppClass* klass);

// Compile-time member name, used as a template argument: Call<"Show">().
template <size_t N> struct Name {
  char text[N];
  constexpr Name(const char (&value)[N])
  { std::copy_n(value, N, text); }
  constexpr std::string_view View() const
  { return {text, N - 1}; }
};

// Resolved method metadata. Hooks detour Pointer(); typed calls go through Class and Object.
class Method
{
public:
  Method() = default;
  explicit Method(const MethodInfo* info)
      : info_(info)
  {
  }
  explicit operator bool() const
  { return info_ != nullptr; }
  const MethodInfo* Info() const
  { return info_; }
  // Generated code takes `this` first (for instance methods) and a trailing const MethodInfo*.
  void* Pointer() const
  { return info_ ? reinterpret_cast<void*>(info_->methodPointer) : nullptr; }
  // "Namespace.Class.Method", for diagnostics.
  std::string Name() const;

private:
  const MethodInfo* info_ = nullptr;
};

namespace detail
{
  // What a C++ type means at the managed boundary. Bindings declare these through their template
  // arguments; resolution compares them with the metadata once and rejects drifted signatures.
  struct Slot {
    enum class Kind : uint8_t { kVoid, kValue, kReference };
    Kind           kind      = Kind::kVoid;
    bool           byref     = false;
    uint32_t       size      = 0;
    Il2CppTypeEnum primitive = IL2CPP_TYPE_END;
    // Required managed class for typed references; null accepts any reference type.
    Il2CppClass* (*klass)() = nullptr;
  };
  struct Signature {
    Slot                  result;
    std::span<const Slot> params;
  };

  Il2CppClass* StringClass();
  Il2CppClass* TypeClass();
  // System.Int32 and friends from corlib, by element type; null for anything else.
  Il2CppClass* PrimitiveClass(Il2CppTypeEnum type);
  void         ReportFault(const char* what);

  template <typename T>
  concept Bound = requires { T::Class::Raw(); };

  template <typename T> constexpr auto ClassOf()
  {
    if constexpr (Bound<T>)
      return &T::Class::Raw;
    else if constexpr (std::is_same_v<T, Il2CppString>)
      return &StringClass;
    else if constexpr (std::is_same_v<T, Il2CppReflectionType>)
      return &TypeClass;
    else
      return static_cast<Il2CppClass* (*)()>(nullptr);
  }

  template <typename T> constexpr Il2CppTypeEnum Primitive()
  {
    if constexpr (std::is_enum_v<T>)
      return Primitive<std::underlying_type_t<T>>();
    else if constexpr (std::is_same_v<T, bool>)
      return IL2CPP_TYPE_BOOLEAN;
    else if constexpr (std::is_same_v<T, char16_t>)
      return IL2CPP_TYPE_CHAR;
    else if constexpr (std::is_same_v<T, float>)
      return IL2CPP_TYPE_R4;
    else if constexpr (std::is_same_v<T, double>)
      return IL2CPP_TYPE_R8;
    else if constexpr (std::is_integral_v<T>) {
      constexpr bool kSigned = std::is_signed_v<T>;
      if constexpr (sizeof(T) == 1)
        return kSigned ? IL2CPP_TYPE_I1 : IL2CPP_TYPE_U1;
      else if constexpr (sizeof(T) == 2)
        return kSigned ? IL2CPP_TYPE_I2 : IL2CPP_TYPE_U2;
      else if constexpr (sizeof(T) == 4)
        return kSigned ? IL2CPP_TYPE_I4 : IL2CPP_TYPE_U4;
      else
        return kSigned ? IL2CPP_TYPE_I8 : IL2CPP_TYPE_U8;
    }
    return IL2CPP_TYPE_VALUETYPE;
  }

  template <typename T> struct Unref {
    using Type                   = T;
    static constexpr bool kByRef = false;
  };
  template <typename T> struct Unref<std::reference_wrapper<T>> {
    using Type                   = T;
    static constexpr bool kByRef = true;
  };

  template <typename T> constexpr Slot SlotOf()
  {
    using U = typename Unref<T>::Type;
    Slot slot;
    slot.byref = Unref<T>::kByRef;
    if constexpr (std::is_void_v<U>)
      return slot;
    else if constexpr (std::is_same_v<U, std::nullptr_t>)
      slot.kind = Slot::Kind::kReference;
    else if constexpr (std::is_pointer_v<U>) {
      static_assert(!std::is_function_v<std::remove_pointer_t<U>>, "managed values cannot be function pointers");
      slot.kind  = Slot::Kind::kReference;
      slot.klass = ClassOf<std::remove_cv_t<std::remove_pointer_t<U>>>();
    } else {
      static_assert(std::is_trivially_copyable_v<U>, "managed values must be trivially copyable");
      slot.kind      = Slot::Kind::kValue;
      slot.size      = sizeof(U);
      slot.primitive = Primitive<U>();
    }
    return slot;
  }

  template <typename... A> inline constexpr std::array<Slot, sizeof...(A)> kParams{SlotOf<A>()...};
  template <typename R, typename... A> constexpr Signature                 SignatureOf()
  { return {SlotOf<R>(), kParams<A...>}; }

  Il2CppClass* FindClass(std::string_view assembly, std::string_view namespaze, std::string_view name);
  Method       FindMethod(Il2CppClass* klass, std::string_view name, int count);
  Method       FindMethod(Il2CppClass* klass, std::string_view name, std::initializer_list<std::string_view> params);
  const MethodInfo* ResolveMethod(Il2CppClass* klass, std::string_view name, const Signature& signature,
                                  bool is_static);
  const MethodInfo* ResolveProperty(Il2CppClass* klass, std::string_view name, const Slot& value, bool setter,
                                    bool is_static);
  FieldInfo*        ResolveField(Il2CppClass* klass, std::string_view name, const Slot& value, bool is_static);

  // Members of bound classes resolve once. Unbound wrappers (generic collections, Object itself)
  // resolve against the receiver's runtime class, cached per class and thread.
  template <typename Self, typename Resolve> auto Lookup(const Il2CppObject& self, Resolve resolve)
  {
    using Result = std::invoke_result_t<Resolve, Il2CppClass*>;
    if constexpr (Bound<Self>) {
      static const Result member = resolve(Self::Class::Raw());
      return member;
    } else {
      static thread_local Il2CppClass* last = nullptr;
      static thread_local Result       cached{};
      if (self.klass != last) {
        static thread_local std::unordered_map<Il2CppClass*, Result> members;
        auto [found, inserted] = members.try_emplace(self.klass);
        if (inserted)
          found->second = resolve(self.klass);
        cached = found->second;
        last   = self.klass;
      }
      return cached;
    }
  }

  template <typename T>
  using Native = std::conditional_t<Unref<T>::kByRef, typename Unref<T>::Type*,
                                    std::conditional_t<std::is_same_v<T, std::nullptr_t>, void*, T>>;
  template <typename T> Native<T> ToNative(T& value)
  {
    if constexpr (Unref<T>::kByRef)
      return &value.get();
    else
      return value;
  }
  template <typename T> void* ToBoxedArgument(T& value)
  {
    if constexpr (Unref<T>::kByRef)
      return &value.get();
    else if constexpr (std::is_pointer_v<T> || std::is_same_v<T, std::nullptr_t>)
      return value;
    else
      return &value;
  }

  Il2CppObject* InvokeBoxed(const MethodInfo* method, void* target, void** args);
  void          ReportException(const MethodInfo* method, Il2CppException* exception);

  template <typename R, typename... A> R InvokeNative(const MethodInfo* method, Il2CppObject* target, A... args)
  {
    // Fully shared generic code takes a different native ABI. Let the runtime marshal it.
    if (method->has_full_generic_sharing_signature) {
      void* argv[] = {ToBoxedArgument(args)..., nullptr};
      // The runtime invoker takes value-type receivers unboxed.
      void* receiver = target && il2cpp_class_is_valuetype(method->klass) ? il2cpp_object_unbox(target) : target;
      auto* result   = InvokeBoxed(method, receiver, argv);
      if constexpr (std::is_pointer_v<R>)
        return reinterpret_cast<R>(result);
      else if constexpr (!std::is_void_v<R>) {
        R value{};
        if (result)
          std::memcpy(&value, il2cpp_object_unbox(result), sizeof(R));
        return value;
      } else
        return;
    }
    if (method->flags & METHOD_ATTRIBUTE_STATIC) {
      using Fn = R (*)(Native<A>..., const MethodInfo*);
      return reinterpret_cast<Fn>(method->methodPointer)(ToNative(args)..., method);
    }
    // virtualMethodPointer takes a boxed receiver, including value-type methods (adjustor thunks).
    using Fn = R (*)(Il2CppObject*, Native<A>..., const MethodInfo*);
    return reinterpret_cast<Fn>(method->virtualMethodPointer)(target, ToNative(args)..., method);
  }

  // Methods are validated at resolution, so this is a plain native call. Missing members (already
  // reported) and missing receivers return a value-initialized R. Managed exceptions stop here:
  // they are reported once per method and the call returns R(), so a throwing getter cannot unwind
  // through a hook and skip the game's own code.
  template <typename R, typename... A> R Invoke(const MethodInfo* method, Il2CppObject* target, A... args)
  {
    const bool is_static = method && (method->flags & METHOD_ATTRIBUTE_STATIC);
    if (!method || (!is_static && !target))
      return R();
    if (!is_static && (method->flags & METHOD_ATTRIBUTE_VIRTUAL)) {
      method = il2cpp_object_get_virtual_method(target, method);
      if (!method)
        return R();
    }
    try {
      return InvokeNative<R>(method, target, args...);
    } catch (const Il2CppExceptionWrapper& error) {
      ReportException(method, error.ex);
    } catch (...) {
      ReportException(method, nullptr);
    }
    return R();
  }

  // Invoke for TryCall: empty (false for void) when the method is missing, the receiver is null, or a managed
  // exception stopped the call. Exceptions are reported as Invoke reports them.
  template <typename R> using Tried = std::conditional_t<std::is_void_v<R>, bool, std::optional<R>>;
  template <typename R, typename... A> Tried<R> TryInvoke(const MethodInfo* method, Il2CppObject* target, A... args)
  {
    const bool is_static = method && (method->flags & METHOD_ATTRIBUTE_STATIC);
    if (!method || (!is_static && !target))
      return {};
    if (!is_static && (method->flags & METHOD_ATTRIBUTE_VIRTUAL)) {
      method = il2cpp_object_get_virtual_method(target, method);
      if (!method)
        return {};
    }
    try {
      if constexpr (std::is_void_v<R>) {
        InvokeNative<R>(method, target, args...);
        return true;
      } else {
        return InvokeNative<R>(method, target, args...);
      }
    } catch (const Il2CppExceptionWrapper& error) {
      ReportException(method, error.ex);
    } catch (...) {
      ReportException(method, nullptr);
    }
    return {};
  }

  // Call<"Name", F>: F is either the result type, with parameters taken from the argument types,
  // or a full signature R(P...). A signature converts arguments to P without narrowing, and a P of
  // T& is a ref/out parameter that binds the caller's lvalue (std::ref in the deduced form).
  template <typename T> struct Param {
    using Type = T;
    template <typename A> static T From(A&& value)
    { return T{std::forward<A>(value)}; }
  };
  template <typename T> struct Param<T&> {
    using Type = std::reference_wrapper<T>;
    static Type From(T& value)
    { return std::ref(value); }
  };

  template <typename Fn> struct MethodCall;
  template <typename R, typename... P> struct MethodCall<R(P...)> {
    static const MethodInfo* Resolve(Il2CppClass* klass, std::string_view name, bool is_static)
    { return ResolveMethod(klass, name, SignatureOf<R, typename Param<P>::Type...>(), is_static); }
    template <typename... A> static R Run(const MethodInfo* method, Il2CppObject* target, A&&... args)
    {
      static_assert(sizeof...(A) == sizeof...(P), "argument count does not match the signature");
      return Invoke<R, typename Param<P>::Type...>(method, target, Param<P>::From(std::forward<A>(args))...);
    }
    template <typename... A> static Tried<R> TryRun(const MethodInfo* method, Il2CppObject* target, A&&... args)
    {
      static_assert(sizeof...(A) == sizeof...(P), "argument count does not match the signature");
      return TryInvoke<R, typename Param<P>::Type...>(method, target, Param<P>::From(std::forward<A>(args))...);
    }
  };
  template <typename F, typename... A> struct CallOf : MethodCall<F(A...)> {
  };
  template <typename R, typename... P, typename... A> struct CallOf<R(P...), A...> : MethodCall<R(P...)> {
  };

  template <typename T> T ReadField(FieldInfo* field, const Il2CppObject* object)
  {
    T value{};
    if (!field)
      return value;
    if (field->type->attrs & FIELD_ATTRIBUTE_STATIC)
      il2cpp_field_static_get_value(field, &value);
    else if (object)
      std::memcpy(&value, reinterpret_cast<const char*>(object) + field->offset, sizeof(T));
    return value;
  }
  // The runtime's setter applies GC write barriers to references, including those inside structs.
  // It takes value types by address but stores a reference argument as the reference itself.
  template <typename T> void WriteField(FieldInfo* field, Il2CppObject* object, T value)
  {
    if (!field)
      return;
    void* argument = &value;
    if constexpr (std::is_pointer_v<T> || std::is_same_v<T, std::nullptr_t>) {
      if (!il2cpp_class_is_valuetype(il2cpp_class_from_type(field->type)))
        argument = const_cast<void*>(static_cast<const void*>(value));
    }
    if (field->type->attrs & FIELD_ATTRIBUTE_STATIC)
      il2cpp_field_static_set_value(field, argument);
    else if (object)
      il2cpp_field_set_value(object, field, argument);
  }
} // namespace detail

// Names one managed class. A binding declares `using Class = il2cpp::Class<...>;` and uses this for
// static members, hooks, and type checks. Nested types use "Outer/Inner".
template <Name Assembly, Name Namespace, Name TypeName> struct Class {
  static Il2CppClass* Raw()
  {
    static Il2CppClass* const klass = detail::FindClass(Assembly.View(), Namespace.View(), TypeName.View());
    return klass;
  }
  // The System.Type for this class. Cached: the runtime keeps type objects in a rooted, append-only cache, and
  // looking one up takes its reflection lock.
  static Il2CppReflectionType* TypeObject()
  {
    static Il2CppReflectionType* const type =
        Raw() ? reinterpret_cast<Il2CppReflectionType*>(il2cpp_type_get_object(il2cpp_class_get_type(Raw()))) : nullptr;
    return type;
  }

  // Lookups for detours. A count of -1 accepts any arity; parameter names are "Namespace.Type".
  static il2cpp::Method Method(std::string_view name, int count = -1)
  { return detail::FindMethod(Raw(), name, count); }
  static il2cpp::Method Method(std::string_view name, std::initializer_list<std::string_view> params)
  { return detail::FindMethod(Raw(), name, params); }

  // An instance method with its complete C++ signature, for hooks that must reject client ABI drift.
  template <typename F> static il2cpp::Method Method(std::string_view name)
  {
    const auto* method = detail::MethodCall<F>::Resolve(Raw(), name, false);
    if (method && (method->has_full_generic_sharing_signature || (method->flags & METHOD_ATTRIBUTE_ABSTRACT))) {
      Report("[IL2CPP] cannot hook shared or abstract method " + ClassName(Raw()) + "." + std::string(name));
      return {};
    }
    return il2cpp::Method(method);
  }

  // Checks an instance field's schema without reading an object, for hooks that write several related fields.
  template <Name N, typename T> static bool HasField()
  { return detail::ResolveField(Raw(), N.View(), detail::SlotOf<T>(), false) != nullptr; }

  template <Name N, typename F = void, typename... A> static auto Call(A&&... args)
  {
    using Sig                 = detail::CallOf<F, std::decay_t<A>...>;
    static const auto* method = Sig::Resolve(Raw(), N.View(), true);
    return Sig::Run(method, nullptr, std::forward<A>(args)...);
  }
  template <Name N, typename T> static T Get()
  {
    static const auto* getter = detail::ResolveProperty(Raw(), N.View(), detail::SlotOf<T>(), false, true);
    return detail::Invoke<T>(getter, nullptr);
  }
  template <Name N, typename T> static void Set(T value)
  {
    static const auto* setter = detail::ResolveProperty(Raw(), N.View(), detail::SlotOf<T>(), true, true);
    detail::Invoke<void>(setter, nullptr, value);
  }
  template <Name N, typename T> static T Field()
  {
    static auto* field = detail::ResolveField(Raw(), N.View(), detail::SlotOf<T>(), true);
    return detail::ReadField<T>(field, nullptr);
  }
  template <Name N, typename T> static void SetField(T value)
  {
    static auto* field = detail::ResolveField(Raw(), N.View(), detail::SlotOf<T>(), true);
    detail::WriteField(field, nullptr, value);
  }
};

// Base for reference-type bindings. A binding pointer is the managed object itself; bindings add
// no data members. Receivers must not be null. Missing members return a value-initialized result.
struct Object : Il2CppObject {
  // Checks a method on the receiver's runtime class, including inflated collection types.
  template <typename F, typename Self> il2cpp::Method Method(this Self& self, std::string_view name)
  { return il2cpp::Method(detail::MethodCall<F>::Resolve(self.klass, name, false)); }

  template <Name N, typename T, typename Self> T Get(this Self& self)
  {
    auto* getter = detail::Lookup<Self>(self, [](Il2CppClass* klass) {
      return detail::ResolveProperty(klass, N.View(), detail::SlotOf<T>(), false, false);
    });
    return detail::Invoke<T>(getter, &self);
  }
  template <Name N, typename Self, typename T> void Set(this Self& self, T value)
  {
    auto* setter = detail::Lookup<Self>(self, [](Il2CppClass* klass) {
      return detail::ResolveProperty(klass, N.View(), detail::SlotOf<T>(), true, false);
    });
    detail::Invoke<void>(setter, &self, value);
  }
  template <Name N, typename T, typename Self> T Field(this Self& self)
  {
    auto* field = detail::Lookup<Self>(
        self, [](Il2CppClass* klass) { return detail::ResolveField(klass, N.View(), detail::SlotOf<T>(), false); });
    return detail::ReadField<T>(field, &self);
  }
  template <Name N, typename Self, typename T> void SetField(this Self& self, T value)
  {
    auto* field = detail::Lookup<Self>(
        self, [](Il2CppClass* klass) { return detail::ResolveField(klass, N.View(), detail::SlotOf<T>(), false); });
    detail::WriteField(field, &self, value);
  }
  // Call<"Name", R>(args) selects the overload by the C++ argument types; std::ref marks ref/out
  // parameters. Call<"Name", R(P...)>(args) names the signature and converts the arguments to it.
  template <Name N, typename F = void, typename Self, typename... A> auto Call(this Self& self, A&&... args)
  {
    using Sig    = detail::CallOf<F, std::decay_t<A>...>;
    auto* method = detail::Lookup<Self>(self, [](Il2CppClass* klass) { return Sig::Resolve(klass, N.View(), false); });
    return Sig::Run(method, &self, std::forward<A>(args)...);
  }
  // Call, except that a failed call is told apart from a result that reads as zero: empty, or false for a void
  // method, when the method is missing or a managed exception (still reported) stopped it. For callers that undo
  // their own changes when the game's code fails.
  template <Name N, typename F = void, typename Self, typename... A> auto TryCall(this Self& self, A&&... args)
  {
    using Sig    = detail::CallOf<F, std::decay_t<A>...>;
    auto* method = detail::Lookup<Self>(self, [](Il2CppClass* klass) { return Sig::Resolve(klass, N.View(), false); });
    return Sig::TryRun(method, &self, std::forward<A>(args)...);
  }

  template <typename T> bool Is() const
  {
    auto* klass = T::Class::Raw();
    return klass && il2cpp_class_is_assignable_from(klass, this->klass);
  }
  template <typename T> T* As()
  { return Is<T>() ? static_cast<T*>(this) : nullptr; }
};

// Allocate T and run the constructor matching the arguments. Null when the class or constructor
// is missing. Native stacks are scanned by the GC; keep a GcHandle to hold the result longer.
template <typename T, typename... A> T* New(A... args)
{
  static const auto* ctor = detail::ResolveMethod(T::Class::Raw(), ".ctor", detail::SignatureOf<void, A...>(), false);
  if (!ctor)
    return nullptr;
  auto* object = static_cast<T*>(il2cpp_object_new(T::Class::Raw()));
  detail::Invoke<void>(ctor, object, args...);
  return object;
}

// Allocate T without running any constructor, for classes whose parameterless constructor has no generated code.
// Every field the game will read has to be set by the caller. Null when the class is missing.
template <typename T> T* Allocate()
{
  auto* klass = T::Class::Raw();
  return klass ? static_cast<T*>(il2cpp_object_new(klass)) : nullptr;
}

// System.Object, the element class of object[].
Il2CppClass* ObjectClass();

// The corlib class of a primitive C++ type: System.Byte for uint8_t, System.Int64 for int64_t.
template <typename T> Il2CppClass* PrimitiveClass()
{
  static_assert(std::is_arithmetic_v<T>, "only primitives have a corlib class here");
  return detail::PrimitiveClass(detail::Primitive<T>());
}

// A boxed primitive, for object parameters and object[] arguments.
template <typename T> Il2CppObject* Box(T value)
{
  auto* klass = PrimitiveClass<T>();
  return klass ? il2cpp_value_box(klass, &value) : nullptr;
}

// A new managed string. Like any managed object it lives while something managed or the native stack holds it.
inline Il2CppString* NewString(std::string_view utf8)
{ return il2cpp_string_new_len(utf8.data(), static_cast<uint32_t>(utf8.size())); }

// The string as UTF-8; empty for null.
std::string ToUtf8(const Il2CppString* str);

// Compares a managed string with ASCII text without converting it, for checks on hot paths.
inline bool Equals(const Il2CppString* str, std::string_view ascii)
{
  if (!str || static_cast<size_t>(str->length) != ascii.size())
    return false;
  for (size_t i = 0; i < ascii.size(); ++i)
    if (str->chars[i] != static_cast<Il2CppChar>(ascii[i]))
      return false;
  return true;
}

// The text of any object: a string as itself, anything else through its ToString override.
inline Il2CppString* ToString(Object* object)
{
  if (!object)
    return nullptr;
  if (object->klass == detail::StringClass())
    return reinterpret_cast<Il2CppString*>(object);
  return object->Call<"ToString", Il2CppString*>();
}

// Runs the method a delegate is bound to on its target directly, skipping the delegate's invoke stub. Unlike
// Call, a managed exception is not caught here, so the caller can fall back: catch Il2CppExceptionWrapper.
template <typename R = void, typename... A> R InvokeBound(Il2CppDelegate* delegate, A... args)
{ return detail::InvokeNative<R, A...>(delegate->method, delegate->target, args...); }

// Runs `read` and, on Windows, survives an access violation inside it: reading game objects whose layout drifted
// between client builds should cost a log line, not the game. False, after reporting `what`, when one was caught.
template <typename Fn> bool Guarded(const char* what, Fn&& read)
{
#if _WIN32
  __try {
    read();
  } __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
    detail::ReportFault(what);
    return false;
  }
#else
  (void)what;
  read();
#endif
  return true;
}

// Managed T[]. Element references are borrowed; use Set to store references with a GC barrier.
// Set supports references and primitives only.
template <typename T> struct Array : Il2CppObject {
  Il2CppArrayBounds*  bounds;
  il2cpp_array_size_t max_length;

  static Array* New(Il2CppClass* element, size_t length)
  { return reinterpret_cast<Array*>(il2cpp_array_new(element, length)); }

  size_t size() const
  { return max_length; }
  T* data()
  { return reinterpret_cast<T*>(reinterpret_cast<Il2CppArraySize*>(this)->vector); }
  T* begin()
  { return data(); }
  T* end()
  { return data() + max_length; }
  T operator[](size_t index)
  { return data()[index]; }
  void Set(size_t index, T value)
  {
    static_assert(std::is_pointer_v<T> || std::is_arithmetic_v<T> || std::is_enum_v<T>,
                  "structs may hold references that need per-field write barriers");
    if constexpr (std::is_pointer_v<T>)
      il2cpp_gc_wbarrier_set_field(this, reinterpret_cast<void**>(data() + index), value);
    else
      data()[index] = value;
  }
};

// System.Collections.Generic.List`1, or any class with the same indexer and Count members.
// Members resolve on the runtime class, so this works for inflated generics and IList implementers.
template <typename T> struct List : Object {
  int32_t Count()
  { return Call<"get_Count", int32_t>(); }
  T operator[](int32_t index)
  { return Call<"get_Item", T>(index); }
  void Add(T value)
  { Call<"Add">(value); }
  void RemoveAt(int32_t index)
  { Call<"RemoveAt">(index); }
  void Clear()
  { Call<"Clear">(); }

  struct Iterator {
    List*   list;
    int32_t index;
    T       operator*() const
    { return (*list)[index]; }
    Iterator& operator++()
    {
      ++index;
      return *this;
    }
    bool operator==(const Iterator& other) const
    { return index == other.index; }
  };
  // Count is read once; do not mutate the list while iterating.
  Iterator begin()
  { return {this, 0}; }
  Iterator end()
  { return {this, Count()}; }
};

// Build a delegate of `delegate_class` bound to a static method. With `native`, invocations jump to
// that C++ function instead of the method's generated code; it must match the method's native ABI.
Il2CppObject* NewDelegate(Il2CppClass* delegate_class, Method target, Il2CppMethodPointer native = nullptr);

// Move-only managed root. Weak handles do not retain the managed object.
// Release roots before the runtime shuts down; these do not destroy Unity native resources.
class GcHandle
{
public:
  GcHandle() = default;
  static GcHandle Strong(Il2CppObject* object)
  { return GcHandle(object ? il2cpp_gchandle_new(object, false) : nullptr); }
  static GcHandle Weak(Il2CppObject* object)
  { return GcHandle(object ? il2cpp_gchandle_new_weakref(object, false) : nullptr); }
  ~GcHandle()
  { Reset(); }
  GcHandle(const GcHandle&)            = delete;
  GcHandle& operator=(const GcHandle&) = delete;
  GcHandle(GcHandle&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr))
  {
  }
  GcHandle& operator=(GcHandle&& other) noexcept
  {
    if (this != &other) {
      Reset();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  explicit operator bool() const
  { return handle_ != nullptr; }
  Il2CppObject* Get() const
  { return handle_ ? il2cpp_gchandle_get_target(handle_) : nullptr; }
  template <typename T> T* Get() const
  { return static_cast<T*>(Get()); }
  void Reset()
  {
    if (handle_)
      il2cpp_gchandle_free(std::exchange(handle_, nullptr));
  }

private:
  explicit GcHandle(Il2CppGCHandle handle)
      : handle_(handle)
  {
  }
  Il2CppGCHandle handle_ = nullptr;
};

// Strong roots for the objects one operation touches, released together when it ends. Keep returns its argument,
// or null after marking the set unhealthy when the runtime could not root it. Null needs no root.
class GcRoots
{
public:
  Il2CppObject* Keep(Il2CppObject* object)
  {
    if (!object)
      return nullptr;
    auto root = GcHandle::Strong(object);
    if (root.Get() != object) {
      healthy_ = false;
      return nullptr;
    }
    roots_.push_back(std::move(root));
    return object;
  }
  template <typename T> T* Keep(T* object)
  { return reinterpret_cast<T*>(Keep(reinterpret_cast<Il2CppObject*>(object))); }
  // False once any Keep failed.
  bool healthy() const
  { return healthy_; }

private:
  std::vector<GcHandle> roots_;
  bool                  healthy_ = true;
};

namespace detail
{
  void ReportMissingICall(const char* name);
} // namespace detail

// A Unity engine internal call. One that a Unity upgrade renamed is reported once, when resolved, and then returns
// the default value instead of calling through null; test it with operator bool to choose another default.
template <typename Signature> class ICall;
template <typename R, typename... A> class ICall<R(A...)>
{
public:
  explicit ICall(const char* name)
      : fn_(reinterpret_cast<R (*)(A...)>(il2cpp_resolve_icall(name)))
  {
    if (!fn_)
      detail::ReportMissingICall(name);
  }

  explicit operator bool() const
  { return fn_ != nullptr; }

  R operator()(A... args) const
  {
    if constexpr (std::is_void_v<R>) {
      if (fn_)
        fn_(args...);
    } else {
      return fn_ ? fn_(args...) : R{};
    }
  }

private:
  R (*fn_)(A...);
};

// Resolves e.g. "UnityEngine.Input::GetKeyInt(UnityEngine.KeyCode)". Keep the result in a static: resolving is a
// lookup by name.
template <typename Signature> ICall<Signature> ResolveICall(const char* name)
{ return ICall<Signature>(name); }
} // namespace il2cpp

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
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace il2cpp
{
// Missing classes and members, and signatures that no longer match the binding, are reported
// once per lookup. Without a handler, reports go to stderr.
void SetErrorHandler(void (*handler)(std::string_view message));

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
    using Type                  = T;
    static constexpr bool kByRef = false;
  };
  template <typename T> struct Unref<std::reference_wrapper<T>> {
    using Type                  = T;
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
  template <typename R, typename... A> constexpr Signature SignatureOf()
  { return {SlotOf<R>(), kParams<A...>}; }

  Il2CppClass*      FindClass(std::string_view assembly, std::string_view namespaze, std::string_view name);
  Method            FindMethod(Il2CppClass* klass, std::string_view name, int count);
  Method            FindMethod(Il2CppClass* klass, std::string_view name, std::initializer_list<std::string_view> params);
  const MethodInfo* ResolveMethod(Il2CppClass* klass, std::string_view name, const Signature& signature, bool is_static);
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

  template <typename T> using Native = std::conditional_t<
      Unref<T>::kByRef, typename Unref<T>::Type*,
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
  static Il2CppReflectionType* TypeObject()
  {
    auto* klass = Raw();
    return klass ? reinterpret_cast<Il2CppReflectionType*>(il2cpp_type_get_object(il2cpp_class_get_type(klass)))
                 : nullptr;
  }

  // Lookups for detours. A count of -1 accepts any arity; parameter names are "Namespace.Type".
  static il2cpp::Method Method(std::string_view name, int count = -1)
  { return detail::FindMethod(Raw(), name, count); }
  static il2cpp::Method Method(std::string_view name, std::initializer_list<std::string_view> params)
  { return detail::FindMethod(Raw(), name, params); }

  template <Name N, typename R = void, typename... A> static R Call(A... args)
  {
    static const auto* method = detail::ResolveMethod(Raw(), N.View(), detail::SignatureOf<R, A...>(), true);
    return detail::Invoke<R>(method, nullptr, args...);
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
    auto* field = detail::Lookup<Self>(self, [](Il2CppClass* klass) {
      return detail::ResolveField(klass, N.View(), detail::SlotOf<T>(), false);
    });
    return detail::ReadField<T>(field, &self);
  }
  template <Name N, typename Self, typename T> void SetField(this Self& self, T value)
  {
    auto* field = detail::Lookup<Self>(self, [](Il2CppClass* klass) {
      return detail::ResolveField(klass, N.View(), detail::SlotOf<T>(), false);
    });
    detail::WriteField(field, &self, value);
  }
  // Overloads are selected by the C++ argument types. std::ref marks ref/out parameters.
  template <Name N, typename R = void, typename Self, typename... A> R Call(this Self& self, A... args)
  {
    auto* method = detail::Lookup<Self>(self, [](Il2CppClass* klass) {
      return detail::ResolveMethod(klass, N.View(), detail::SignatureOf<R, A...>(), false);
    });
    return detail::Invoke<R>(method, &self, args...);
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

// Unity engine internal calls, e.g. "UnityEngine.Input::GetKeyInt(UnityEngine.KeyCode)".
template <typename Signature> Signature* ResolveICall(const char* name)
{ return reinterpret_cast<Signature*>(il2cpp_resolve_icall(name)); }
} // namespace il2cpp

#pragma once

// Live instances of chosen MonoBehaviour classes, found without asking Unity. Track<T>() detours T's constructor
// and OnDestroy; a GC finalizer and Unity's liveness pass drop instances the game has released without destroying
// them. Instances are borrowed: use them on the game thread, within the frame.

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>

#include <ranges>
#include <vector>

namespace unity
{
namespace detail
{
  void Constructed(Il2CppObject* object);
  void Destroyed(Il2CppObject* object);
  // True the first time a native method is seen: constructors and OnDestroy often resolve to one shared base
  // implementation, which must be detoured only once.
  bool                       FirstDetour(void* native);
  void                       StartTracking();
  Il2CppObject*              Newest(Il2CppClass* klass);
  std::vector<Il2CppObject*> Instances(Il2CppClass* klass);

  inline void Ctor_Hook(auto original, Il2CppObject* _this, const MethodInfo* method)
  {
    original(_this, method);
    Constructed(_this);
  }

  inline void OnDestroy_Hook(auto original, Il2CppObject* _this, const MethodInfo* method)
  {
    Destroyed(_this);
    original(_this, method);
  }
} // namespace detail

// Starts tracking T and its subclasses. Call while installing hooks, before T's instances are created: instances
// that already exist are not found. Alternate parameterless lifecycle methods support pooled components.
template <typename T> void Track(std::string_view created = ".ctor", std::string_view released = "OnDestroy")
{
  detail::StartTracking();
  // Both lifecycle methods are parameterless; Unity's constructor and OnDestroy are the defaults.
  if (auto ctor = T::Class::Method(created, 0); ctor && detail::FirstDetour(ctor.Pointer()))
    IL2CPP_HOOK(ctor, detail::Ctor_Hook);
  if (auto on_destroy = T::Class::Method(released, 0); on_destroy && detail::FirstDetour(on_destroy.Pointer()))
    IL2CPP_HOOK(on_destroy, detail::OnDestroy_Hook);
}

// The newest live instance of T, or null. Only finds classes passed to Track.
template <typename T> T* Newest()
{ return static_cast<T*>(detail::Newest(T::Class::Raw())); }

// Every live instance of T, oldest first. The range owns a snapshot of borrowed pointers, so the caller may call
// into the game while the GC's finalizer thread keeps updating the tracker.
template <typename T> auto Instances()
{
  return detail::Instances(T::Class::Raw())
         | std::views::transform([](Il2CppObject* object) { return static_cast<T*>(object); });
}
} // namespace unity

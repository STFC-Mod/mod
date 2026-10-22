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
  void                       Constructed(Il2CppObject* object);
  void                       Destroyed(Il2CppObject* object);
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
// Tracking a class again does nothing; the first call's lifecycle methods stay.
template <typename T> void Track(std::string_view created = ".ctor", std::string_view released = "OnDestroy")
{
  static const bool tracked = [&] {
    detail::StartTracking();
    // Both lifecycle methods are parameterless; Unity's constructor and OnDestroy are the defaults. They go in as
    // one set: without its release hook, a pooled component would stay tracked after the pool took it back.
    // Classes often share one constructor or OnDestroy body, which then carries a chained hook per tracked class.
    // Tracking, untracking and the finalizer registration all tolerate running more than once for one object.
    il2cpp::HookSet hooks("tracking " + il2cpp::ClassName(T::Class::Raw()));
    IL2CPP_STAGE(hooks, T::Class::Method(created, 0), detail::Ctor_Hook);
    IL2CPP_STAGE(hooks, T::Class::Method(released, 0), detail::OnDestroy_Hook);
    return hooks.Commit();
  }();
  (void)tracked;
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

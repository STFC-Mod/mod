#pragma once

// Live instances of a fixed set of game UI classes, found without asking Unity. Constructors and OnDestroy are
// detoured, and a GC finalizer and the liveness pass drop instances the game has released.

#include <il2cpp/il2cpp.h>

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <mutex>

namespace object_tracker
{
// Borrowed pointers, keyed by class and every parent class. GC finalizers update this from their own thread.
struct Registry {
  std::mutex mutex;
  // Class -> instances, oldest first.
  eastl::unordered_map<Il2CppClass*, eastl::vector<Il2CppObject*>> objects;

  eastl::vector<Il2CppObject*>* Find(Il2CppClass* klass)
  {
    const auto it = objects.find(klass);
    return it == objects.end() ? nullptr : &it->second;
  }
};

Registry& registry();
} // namespace object_tracker

template <typename T> struct ObjectFinder {
  // The newest live instance, or null. Only classes InstallObjectTrackers registers are ever found.
  static T* Get()
  {
    auto&            registry = object_tracker::registry();
    std::scoped_lock lock(registry.mutex);
    auto*            objects = registry.Find(T::Class::Raw());
    return objects && !objects->empty() ? static_cast<T*>(objects->back()) : nullptr;
  }

  // A snapshot, so the caller can call into the game while the finalizer thread keeps updating the registry.
  static eastl::vector<T*> GetAll()
  {
    auto&             registry = object_tracker::registry();
    std::scoped_lock  lock(registry.mutex);
    eastl::vector<T*> result;
    if (auto* objects = registry.Find(T::Class::Raw()))
      for (auto* object : *objects)
        result.push_back(static_cast<T*>(object));
    return result;
  }
};

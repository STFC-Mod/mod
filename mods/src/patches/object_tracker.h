#pragma once

#include <il2cpp/il2cpp.h>
#include <EASTL/span.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

// Game objects registered by the mod's Unity lifecycle hooks. These are borrowed pointers.
extern eastl::unordered_map<Il2CppClass*, eastl::vector<uintptr_t>> tracked_objects;

template <typename T> class ObjectFinder
{
public:
  static T* Get()
  {
    auto& objects = tracked_objects[T::Class::Raw()];
    if (objects.empty()) {
      // TODO: assert?
      return nullptr;
    }
    return reinterpret_cast<T*>(objects.back());
  }

  static eastl::span<T*> GetAll()
  {
    auto& objects = tracked_objects[T::Class::Raw()];
    return {reinterpret_cast<T**>(objects.data()), reinterpret_cast<T**>(objects.data()) + objects.size()};
  }
};

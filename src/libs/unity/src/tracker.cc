// Owns the registry of tracked instances and the GC hooks that keep it honest.

#include <unity/tracker.h>

#include <il2cpp/hook.h>
#include <spud/signature.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace unity::detail
{
namespace
{
  // While the GC marks, the low bit of an object's klass pointer is its mark bit.
  Il2CppClass* ClassOf(Il2CppObject* object)
  { return reinterpret_cast<Il2CppClass*>(reinterpret_cast<uintptr_t>(object->klass) & ~uintptr_t{1}); }

  bool IsMarked(Il2CppObject* object)
  { return (reinterpret_cast<uintptr_t>(object->klass) & 1) != 0; }

  // Borrowed pointers, keyed by class and every parent class, oldest first. The GC finalizer thread updates this,
  // so every access holds the mutex. Never destroyed: the finalizer thread can still run while static destructors
  // run at exit.
  struct Registry {
    std::mutex                                                   mutex;
    std::unordered_map<Il2CppClass*, std::vector<Il2CppObject*>> objects;

    std::vector<Il2CppObject*>* Find(Il2CppClass* klass)
    {
      const auto it = objects.find(klass);
      return it == objects.end() ? nullptr : &it->second;
    }

    // Add and Remove need the mutex held. A base and a derived constructor can both be detoured and both run for
    // one object, so adding twice is a no-op; a second entry would outlive Remove and hand out a freed object.
    void Add(Il2CppObject* object)
    {
      if (std::ranges::contains(objects[object->klass], object))
        return;
      for (auto* klass = object->klass; klass; klass = klass->parent)
        objects[klass].push_back(object);
    }

    // The object may still carry its mark bit when OnDestroy or its finalizer follows a liveness pass.
    void Remove(Il2CppObject* object)
    {
      for (auto* klass = ClassOf(object); klass; klass = klass->parent)
        if (auto* instances = Find(klass))
          std::erase(*instances, object);
    }
  };
  Registry& registry = *new Registry;

  // Unity's Boehm fork takes a per-entry "finalize mark proc" as a sixth argument. GC_finalize calls it for every
  // unreachable finalized object, and the game's own registrations pass a no-op. Leaving the argument out puts
  // register garbage in the entry, which GC_finalize later calls (seen as a call to null during
  // ReclaimUnusedMemory).
  void FinalizeMarkProcNoop(void*) {}

  using FinalizerCallback = void (*)(void* object, void* client_data);
  using RegisterFinalizer = void (*)(uintptr_t obj, FinalizerCallback fn, void* cd, FinalizerCallback* ofn, void** ocd,
                                     void (*mark_proc)(void*));
  // Not exported; found by its prologue. Null when the scan fails, and then only OnDestroy and the liveness pass
  // untrack.
  RegisterFinalizer register_finalizer = nullptr;

  // A finalizer that was registered before ours, called after untracking.
  struct ChainedFinalizer {
    FinalizerCallback callback;
    void*             data;
  };

  // Runs on the GC finalizer thread. `data` is the replaced finalizer, if the object had one.
  void OnFinalize(void* object, void* data)
  {
    if (object) {
      std::scoped_lock lock(registry.mutex);
      registry.Remove(static_cast<Il2CppObject*>(object));
    }
    if (auto* chained = static_cast<ChainedFinalizer*>(data)) {
      const auto next = *chained;
      delete chained;
      next.callback(object, next.data);
    }
  }

  void WatchFinalizer(Il2CppObject* object)
  {
    if (!register_finalizer)
      return;
    const auto        address       = reinterpret_cast<uintptr_t>(object);
    FinalizerCallback previous      = nullptr;
    void*             previous_data = nullptr;
    register_finalizer(address, OnFinalize, nullptr, &previous, &previous_data, FinalizeMarkProcNoop);
    // An object has one finalizer slot. Ours is what reports the object gone, and the one it replaced must still
    // run, so ours calls it. A base and a derived constructor can both register: then the slot already held ours,
    // with any chain, and is put back as it was.
    if (previous == OnFinalize)
      register_finalizer(address, previous, previous_data, nullptr, nullptr, FinalizeMarkProcNoop);
    else if (previous)
      register_finalizer(address, OnFinalize, new ChainedFinalizer{previous, previous_data}, nullptr, nullptr,
                         FinalizeMarkProcNoop);
  }

  // After Unity's liveness pass, objects still marked are unreachable from managed code; forget them before their
  // native side goes away.
  void LivenessFinalize_Hook(auto original, void* state)
  {
    original(state);

    std::scoped_lock lock(registry.mutex);
    // Filter each class list once, including inherited entries; removing one object at a time rescans them.
    for (auto& [klass, instances] : registry.objects)
      std::erase_if(instances, IsMarked);
  }

  RegisterFinalizer FindRegisterFinalizer()
  {
#if _WIN32
    auto matches = spud::find_in_module("40 56 57 41 57 48 83 EC ? 83 3D", "GameAssembly.dll");
#elif SPUD_ARCH_ARM64
    auto matches = spud::find_in_module(
        "FF ? 02 D1 FC 6F ? A9 FA 67 ? A9 F8 5F ? A9 F6 57 ? A9 F4 4F ? A9 FD 7B ? A9 FD ? 02 91 E4 0F ? A9",
        "GameAssembly.dylib");
#else
    auto matches = spud::find_in_module("55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC ? 4C 89 45 ? 48 89 4D ? 83 3D",
                                        "GameAssembly.dylib");
#endif
    if (matches.empty()) {
      il2cpp::Report("[Unity] GC_register_finalizer_inner not found; an object the GC frees without OnDestroy "
                     "stays tracked, and can be read after it is freed");
      return nullptr;
    }
    return reinterpret_cast<RegisterFinalizer>(matches.front().address());
  }
} // namespace

void Constructed(Il2CppObject* object)
{
  if (!object || !object->klass)
    return;
  WatchFinalizer(object);
  std::scoped_lock lock(registry.mutex);
  registry.Add(object);
}

void Destroyed(Il2CppObject* object)
{
  if (!object)
    return;
  std::scoped_lock lock(registry.mutex);
  registry.Remove(object);
}

void StartTracking()
{
  [[maybe_unused]] static const bool started = [] {
    il2cpp::HookSet hooks("object tracking");
    IL2CPP_STAGE_AT(hooks, il2cpp_unity_liveness_finalize, LivenessFinalize_Hook);
    hooks.Commit();
    register_finalizer = FindRegisterFinalizer();
    return true;
  }();
}

// The registry never holds a null class, so a missing binding finds nothing.
Il2CppObject* Newest(Il2CppClass* klass)
{
  std::scoped_lock lock(registry.mutex);
  auto*            objects = registry.Find(klass);
  return objects && !objects->empty() ? objects->back() : nullptr;
}

std::vector<Il2CppObject*> Instances(Il2CppClass* klass)
{
  std::scoped_lock lock(registry.mutex);
  auto*            objects = registry.Find(klass);
  return objects ? *objects : std::vector<Il2CppObject*>{};
}
} // namespace unity::detail

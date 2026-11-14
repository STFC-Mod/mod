// Owns the registry of tracked instances and the GC hooks that keep it honest.

#include <unity/tracker.h>

#include <il2cpp/hook.h>
#include <spud/signature.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace unity::detail
{
namespace
{
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
  };
  Registry& registry = *new Registry;

  // While the GC marks, the low bit of an object's klass pointer is its mark bit.
  Il2CppClass* ClassOf(Il2CppObject* object)
  { return reinterpret_cast<Il2CppClass*>(reinterpret_cast<uintptr_t>(object->klass) & ~uintptr_t{1}); }

  bool IsMarked(Il2CppObject* object)
  { return (reinterpret_cast<uintptr_t>(object->klass) & 1) != 0; }

  // Callers hold the registry lock. A base and a derived constructor can both be detoured and both run for one
  // object, so tracking twice is a no-op; a second entry would outlive Untrack and hand out a freed object.
  void Track(Il2CppObject* object)
  {
    auto& own = registry.objects[object->klass];
    if (std::ranges::find(own, object) != own.end())
      return;
    for (auto* klass = object->klass; klass; klass = klass->parent)
      registry.objects[klass].push_back(object);
  }

  void Untrack(Il2CppObject* object)
  {
    for (auto* klass = ClassOf(object); klass; klass = klass->parent)
      if (auto* instances = registry.Find(klass))
        std::erase(*instances, object);
  }

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
      Untrack(static_cast<Il2CppObject*>(object));
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
    // Filter each class list once, including inherited entries; untracking one object at a time rescans them.
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
      il2cpp::Report("[Unity] GC_register_finalizer_inner not found; tracked objects are only dropped on OnDestroy "
                     "and Unity's liveness pass");
      return nullptr;
    }
    return reinterpret_cast<RegisterFinalizer>(matches.front().address());
  }

  std::unordered_set<void*> detoured;
} // namespace

void Constructed(Il2CppObject* object)
{
  if (!object || !object->klass)
    return;
  WatchFinalizer(object);
  std::scoped_lock lock(registry.mutex);
  Track(object);
}

void Destroyed(Il2CppObject* object)
{
  if (!object)
    return;
  std::scoped_lock lock(registry.mutex);
  Untrack(object);
}

bool FirstDetour(void* native)
{ return detoured.insert(native).second; }

void StartTracking()
{
  static bool started = false;
  if (std::exchange(started, true))
    return;
  IL2CPP_DETOUR(il2cpp_unity_liveness_finalize, LivenessFinalize_Hook, "il2cpp_unity_liveness_finalize");
  register_finalizer = FindRegisterFinalizer();
}

Il2CppObject* Newest(Il2CppClass* klass)
{
  std::scoped_lock lock(registry.mutex);
  auto*            objects = klass ? registry.Find(klass) : nullptr;
  return objects && !objects->empty() ? objects->back() : nullptr;
}

std::vector<Il2CppObject*> Instances(Il2CppClass* klass)
{
  std::scoped_lock lock(registry.mutex);
  auto*            objects = klass ? registry.Find(klass) : nullptr;
  return objects ? *objects : std::vector<Il2CppObject*>{};
}
} // namespace unity::detail

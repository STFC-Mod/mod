#include "patches/object_tracker.h"

#include "patches/patches.h"

#include <prime/chat.h>
#include <prime/fleet.h>
#include <prime/inventory.h>
#include <prime/navigation.h>
#include <prime/ui.h>
#include <prime/viewers.h>

#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>

#include <algorithm>
#include <spdlog/spdlog.h>
#include <spud/detour.h>
#include <spud/signature.h>

namespace object_tracker
{
// Never destroyed: the GC finalizer thread can still call OnFinalize while static destructors run at exit.
Registry& registry()
{
  static auto& registry = *new Registry;
  return registry;
}
} // namespace object_tracker

namespace
{
using object_tracker::registry;

// While the GC marks, the low bit of an object's klass pointer is its mark bit.
Il2CppClass* ClassOf(Il2CppObject* object)
{ return reinterpret_cast<Il2CppClass*>(reinterpret_cast<uintptr_t>(object->klass) & ~uintptr_t{1}); }

bool IsMarked(Il2CppObject* object)
{ return (reinterpret_cast<uintptr_t>(object->klass) & 1) != 0; }

// Callers hold the registry lock. A base and a derived constructor can both be detoured and both run for one
// object, so tracking twice is a no-op; a second entry would outlive Untrack and hand out a freed object.
void Track(Il2CppObject* object)
{
  auto& own = registry().objects[object->klass];
  if (std::ranges::find(own, object) != own.end())
    return;
  for (auto* klass = object->klass; klass; klass = klass->parent)
    registry().objects[klass].push_back(object);
}

void Untrack(Il2CppObject* object)
{
  for (auto* klass = ClassOf(object); klass; klass = klass->parent)
    if (auto* instances = registry().Find(klass))
      instances->erase_first(object);
}

// Unity's Boehm fork takes a per-entry "finalize mark proc" as a sixth argument. GC_finalize calls it for every
// unreachable finalized object, and the game's own registrations pass a no-op. Leaving the argument out puts
// register garbage in the entry, which GC_finalize later calls (seen as a call to null during
// ReclaimUnusedMemory).
void FinalizeMarkProcNoop(void*) {}

using FinalizerCallback                                       = void (*)(void* object, void* client_data);
void (*GC_register_finalizer_inner)(uintptr_t obj, FinalizerCallback fn, void* cd, FinalizerCallback* ofn, void** ocd,
                                    void (*mark_proc)(void*)) = nullptr;

// A finalizer that was registered before ours, called after untracking.
struct ChainedFinalizer {
  FinalizerCallback callback;
  void*             data;
};

// Runs on the GC finalizer thread. `data` is the replaced finalizer, if the object had one.
void OnFinalize(void* object, void* data)
{
  if (object) {
    std::scoped_lock lock(registry().mutex);
    Untrack(static_cast<Il2CppObject*>(object));
  }
  if (auto* chained = static_cast<ChainedFinalizer*>(data)) {
    const auto next = *chained;
    delete chained;
    next.callback(object, next.data);
  }
}

void Ctor_Hook(auto original, Il2CppObject* _this, const MethodInfo* method)
{
  original(_this, method);
  if (!_this || !_this->klass)
    return;

  if (GC_register_finalizer_inner) {
    FinalizerCallback previous      = nullptr;
    void*             previous_data = nullptr;
    GC_register_finalizer_inner(reinterpret_cast<uintptr_t>(_this), OnFinalize, nullptr, &previous, &previous_data,
                                FinalizeMarkProcNoop);
    // An object has one finalizer slot. Ours is what reports the object gone, and the one it replaced must still
    // run, so ours calls it. A base and a derived constructor can both be detoured and both register: then the
    // slot already holds ours, with any chain, and is put back as it was.
    if (previous == OnFinalize)
      GC_register_finalizer_inner(reinterpret_cast<uintptr_t>(_this), previous, previous_data, nullptr, nullptr,
                                  FinalizeMarkProcNoop);
    else if (previous)
      GC_register_finalizer_inner(reinterpret_cast<uintptr_t>(_this), OnFinalize,
                                  new ChainedFinalizer{previous, previous_data}, nullptr, nullptr,
                                  FinalizeMarkProcNoop);
  }
  std::scoped_lock lock(registry().mutex);
  spdlog::trace("Tracking {}({})", static_cast<void*>(_this), _this->klass->name);
  Track(_this);
}

void OnDestroy_Hook(auto original, Il2CppObject* _this, const MethodInfo* method)
{
  if (_this) {
    std::scoped_lock lock(registry().mutex);
    spdlog::trace("Clearing {}({})", static_cast<void*>(_this), ClassOf(_this)->name);
    Untrack(_this);
  }
  original(_this, method);
}

// After Unity's liveness pass, objects still marked are unreachable from managed code; forget them before their
// native side goes away.
void LivenessFinalize_Hook(auto original, void* state)
{
  original(state);

  std::scoped_lock             lock(registry().mutex);
  eastl::vector<Il2CppObject*> released;
  // Every object sits in its own class's list, so checking those lists alone visits each object once.
  for (auto& [klass, instances] : registry().objects)
    for (auto* object : instances)
      if (ClassOf(object) == klass && IsMarked(object))
        released.push_back(object);
  for (auto* object : released) {
    spdlog::trace("Clearing {}({})", static_cast<void*>(object), ClassOf(object)->name);
    Untrack(object);
  }
}

// OnDestroy and sometimes .ctor resolve to a shared base implementation, so this set spans every tracked type:
// each native entry point is detoured once.
eastl::unordered_set<void*> detoured;

template <typename T> void TrackObject()
{
  // Unity constructs MonoBehaviours through their parameterless constructor.
  if (auto ctor = T::Class::Method(".ctor", 0); ctor && detoured.insert(ctor.Pointer()).second)
    SPUD_STATIC_DETOUR(ctor.Pointer(), Ctor_Hook);
  if (auto on_destroy = T::Class::Method("OnDestroy"); on_destroy && detoured.insert(on_destroy.Pointer()).second)
    SPUD_STATIC_DETOUR(on_destroy.Pointer(), OnDestroy_Hook);
}
} // namespace

void InstallObjectTrackers()
{
  TrackObject<PreScanTargetWidget>();
  TrackObject<FleetBarViewController>();
  TrackObject<FleetMeshSelector>();
  TrackObject<AllianceStarbaseObjectViewerWidget>();
  TrackObject<AnimatedRewardsScreenViewController>();
  TrackObject<ArmadaObjectViewerWidget>();
  TrackObject<ArtifactHallDetailsViewController>();
  TrackObject<AssignShipsWidget>();
  TrackObject<CelestialObjectViewerWidget>();
  TrackObject<EmbassyObjectViewer>();
  TrackObject<FullScreenChatViewController>();
  TrackObject<HousingObjectViewerWidget>();
  TrackObject<InventoryListViewController>();
  TrackObject<MiningObjectViewerWidget>();
  TrackObject<MissionsObjectViewerWidget>();
  TrackObject<NavigationInteractionUIViewController>();
  TrackObject<OfficerAssignmentViewController>();
  TrackObject<ElementSelectorViewController>();
  TrackObject<ShipManagementViewController>();
  TrackObject<ShipManagementScreenStateViewController>();
  TrackObject<StarNodeObjectViewerWidget>();

  SPUD_STATIC_DETOUR(il2cpp_unity_liveness_finalize, LivenessFinalize_Hook);

  // GC_register_finalizer_inner is not exported; find it by its prologue.
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
  if (matches.size() == 0) {
    spdlog::warn("Unable to resolve GC_register_finalizer_inner; object finalizers disabled");
    return;
  }
  GC_register_finalizer_inner = reinterpret_cast<decltype(GC_register_finalizer_inner)>(matches[0].address());
}

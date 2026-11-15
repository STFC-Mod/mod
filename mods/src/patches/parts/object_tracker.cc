#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>

#include "prime/AllianceStarbaseObjectViewerWidget.h"
#include "prime/AnimatedRewardsScreenViewController.h"
#include "prime/ArmadaObjectViewerWidget.h"
#include "prime/ArtifactHallDetailsViewController.h"
#include "prime/AssignShipsWidget.h"
#include "prime/CelestialObjectViewerWidget.h"
#include "prime/EmbassyObjectViewer.h"
#include "prime/FleetBarViewController.h"
#include "prime/FleetMeshSelector.h"
#include "prime/FullScreenChatViewController.h"
#include "prime/HousingObjectViewerWidget.h"
#include "prime/InventoryListViewController.h"
#include "prime/MiningObjectViewerWidget.h"
#include "prime/OfficerAssignmentViewController.h"
#include "prime/MissionsObjectViewerWidget.h"
#include "prime/NavigationInteractionUIViewController.h"
#include "prime/PreScanTargetWidget.h"
#include "prime/ElementSelectorViewController.h"
#include "prime/ShipManagementViewController.h"
#include "prime/ShipManagementScreenStateViewController.h"
#include "prime/StarNodeObjectViewerWidget.h"

#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/vector.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>
#include <spud/signature.h>

#include <mutex>

std::mutex                                                   tracked_objects_mutex;
eastl::unordered_map<Il2CppClass*, eastl::vector<uintptr_t>> tracked_objects;

void add_to_tracking_recursive(Il2CppClass* klass, void* _this)
{
  if (!klass) {
    return;
  }

  auto& tracked_object_vector = tracked_objects[klass];
  tracked_object_vector.emplace_back(uintptr_t(_this));

  return add_to_tracking_recursive(klass->parent, _this);
}

void remove_from_tracking_all(void* _this)
{
#define GET_CLASS(obj) ((Il2CppClass*)(((size_t)obj) & ~(size_t)1))
  for (auto& [klass, tracked_object_vector] : tracked_objects) {
    tracked_object_vector.erase_first(uintptr_t(_this));
  }
#undef GET_CLASS
}

void remove_from_tracking_recursive(Il2CppClass* klass, void* _this)
{
#define GET_CLASS(obj) ((Il2CppClass*)(((size_t)obj) & ~(size_t)1))
  if (!GET_CLASS(klass)) {
    return;
  }

  if (tracked_objects.find(klass) == tracked_objects.end()) {
    return;
  }

  auto& tracked_object_vector = tracked_objects[GET_CLASS(klass->parent)];
  tracked_object_vector.erase_first(uintptr_t(_this));
  return remove_from_tracking_recursive(GET_CLASS(klass->parent), _this);
#undef GET_CLASS
}

// Unity's Boehm GC fork passes a per-entry "finalize mark proc" as a 6th argument to
// GC_register_finalizer_inner. GC_finalize calls it for every unreachable finalized object, and the
// game's own registrations (via GC_register_finalizer_no_order) always pass GC_null_finalize_mark_proc,
// a no-op. If the 6th argument is omitted, register/stack garbage ends up in the finalization entry and
// GC_finalize eventually calls it (observed as a call to nullptr -> SIGSEGV during ReclaimUnusedMemory).
void gc_finalize_mark_proc_noop(void*)
{
}

void (*GC_register_finalizer_inner)(unsigned __int64 obj, void (*fn)(void*, void*), void* cd,
                                    void (**ofn)(void*, void*), void** ocd, void (*mark_proc)(void*)) = nullptr;

void track_finalizer(void* _this, void*)
{
  if (_this == nullptr) {
    return;
  }

  std::scoped_lock lk{tracked_objects_mutex};

  auto object = (Il2CppObject*)_this;
  if (object->klass == nullptr) {
    remove_from_tracking_all(_this);
    return;
  }

#define GET_CLASS(obj) ((Il2CppClass*)(((size_t)obj) & ~(size_t)1))
  spdlog::trace("Clearing {}({})", (void*)_this, GET_CLASS(object->klass)->name);
  remove_from_tracking_all(_this);
#undef GET_CLASS
}

void* track_ctor(auto original, void* _this)
{
  auto obj = original(_this);
  if (_this == nullptr) {
    return _this;
  }

  auto cls = (Il2CppObject*)_this;
  if (cls->klass == nullptr) {
    return obj;
  }

  std::scoped_lock lk{tracked_objects_mutex};
  spdlog::trace("Tracking {}({})", _this, cls->klass->name);
  if (GC_register_finalizer_inner != nullptr) {
    typedef void (*FinalizerCallback)(void* object, void* client_data);
    FinalizerCallback oldCallback = nullptr;
    void*             oldData     = nullptr;
    GC_register_finalizer_inner((intptr_t)_this, track_finalizer, nullptr, &oldCallback, &oldData,
                                gc_finalize_mark_proc_noop);
    assert(!oldCallback);
  }
  add_to_tracking_recursive(cls->klass, _this);
  return obj;
}

void track_destroy(auto original, Il2CppObject* _this, uint64_t a2, uint64_t a3)
{
#define GET_CLASS(obj) ((Il2CppClass*)(((size_t)obj) & ~(size_t)1))
  if (_this != nullptr) {
    std::scoped_lock lk{tracked_objects_mutex};
    spdlog::trace("Clearing {}({})", (void*)_this, GET_CLASS(_this->klass)->name);
    remove_from_tracking_all(_this);
  }
  return original(_this, a2, a3);
#undef GET_CLASS
}

void track_free(auto original, void* _this)
{
#define GET_CLASS(obj) ((Il2CppClass*)(((size_t)obj) & ~(size_t)1))
  if (_this != nullptr) {
    std::scoped_lock lk{tracked_objects_mutex};
    auto             cls = (Il2CppObject*)_this;
    remove_from_tracking_all(_this);
    return original(_this);
  }
#undef GET_CLASS
}

void artifact_details_bind_hook(auto original, Il2CppObject* _this)
{
  original(_this);
  if (!_this || !_this->klass)
    return;

  std::scoped_lock lk{tracked_objects_mutex};
  remove_from_tracking_all(_this);
  add_to_tracking_recursive(_this->klass, _this);
}

void artifact_details_release_hook(auto original, Il2CppObject* _this)
{
  if (_this) {
    std::scoped_lock lk{tracked_objects_mutex};
    remove_from_tracking_all(_this);
  }
  original(_this);
}

void calc_liveness_hook(auto original, void* state)
{
  original(state);

  std::scoped_lock                                    lk{tracked_objects_mutex};
  eastl::vector<eastl::pair<Il2CppClass*, uintptr_t>> objects_to_free;
  eastl::unordered_set<uintptr_t>                     objects_seen;
#define IS_MARKED(obj) (((size_t)(obj)->klass) & (size_t)1)
  for (auto& [klass, objects] : tracked_objects) {
    for (auto object : objects) {
      if (IS_MARKED((Il2CppObject*)object) && objects_seen.find(object) == objects_seen.end()) {
        objects_to_free.emplace_back(klass, object);
        objects_seen.emplace(object);
      }
    }
  }

#undef IS_MARKED

#define GET_CLASS(obj) ((Il2CppClass*)(((size_t)obj) & ~(size_t)1))
  for (auto& [klass, object] : objects_to_free) {
    spdlog::trace("Clearing {}({})", (void*)object, GET_CLASS(klass)->name);
    remove_from_tracking_all((void*)object);
  }
#undef GET_CLASS
}

static eastl::unordered_set<void*> seen_ctor;
static eastl::unordered_set<void*> seen_destroy;

template <typename T> void TrackObject()
{
  auto& object_class = T::get_class_helper();
  auto  ctor         = object_class.GetMethod(".ctor");
  auto  on_destroy   = object_class.GetMethod("OnDestroy");
  if (seen_ctor.find(ctor) == seen_ctor.end()) {
    SPUD_STATIC_DETOUR(ctor, track_ctor);
    seen_ctor.emplace(ctor);
  }

  if (seen_destroy.find(on_destroy) == seen_destroy.end()) {
    SPUD_STATIC_DETOUR(on_destroy, track_destroy);
    seen_destroy.emplace(on_destroy);
  }
}

void TrackArtifactDetailsCanvas()
{
  auto* cls = ArtifactHallDetailsViewController::get_class_helper().get_cls();
  const auto* bind = method_contract::Resolve(cls, "OnDidBindCanvasContext", false, "System.Void", {});
  const auto* release = method_contract::Resolve(cls, "OnAboutToReleaseCanvasContext", false, "System.Void", {});
  if (!bind || !release || bind->has_full_generic_sharing_signature || release->has_full_generic_sharing_signature) {
    spdlog::warn("Object tracker: artifact details canvas lifecycle unavailable; artifact arrow navigation disabled");
    return;
  }

  const bool bind_installed = SPUD_STATIC_DETOUR(bind->methodPointer, artifact_details_bind_hook);
  const bool release_installed = SPUD_STATIC_DETOUR(release->methodPointer, artifact_details_release_hook);
  if (bind_installed && release_installed)
    spdlog::info("Object tracker: tracking artifact details through canvas lifecycle");
  else
    spdlog::warn("Object tracker: artifact details canvas hooks were not installed; artifact arrow navigation disabled");
}

void InstallObjectTrackers()
{
  TrackObject<PreScanTargetWidget>();
  TrackObject<FleetBarViewController>();
  TrackObject<FleetMeshSelector>();
  TrackObject<AllianceStarbaseObjectViewerWidget>();
  TrackObject<AnimatedRewardsScreenViewController>();
  TrackObject<ArmadaObjectViewerWidget>();
  TrackArtifactDetailsCanvas();
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

  SPUD_STATIC_DETOUR(il2cpp_unity_liveness_finalize, calc_liveness_hook);

#if defined(__APPLE__) && defined(SPUD_ARCH_ARM64)
  spdlog::warn("Object tracker: macOS ARM64 SPUD indirect-branch fix active; liveness-finalize hook enabled");
#endif

#if _WIN32
  auto GC_register_finalizer_inner_matches =
      spud::find_in_module("40 56 57 41 57 48 83 EC ? 83 3D", "GameAssembly.dll");
#else
#if SPUD_ARCH_ARM64
  auto GC_register_finalizer_inner_matches = spud::find_in_module(
    "FF ? 02 D1 FC 6F ? A9 FA 67 ? A9 F8 5F ? A9 F6 57 ? A9 F4 4F ? A9 FD 7B ? A9 FD ? 02 91 E4 0F ? A9", "GameAssembly.dylib");
#else
  auto GC_register_finalizer_inner_matches = spud::find_in_module(
      "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC ? 4C 89 45 ? 48 89 4D ? 83 3D", "GameAssembly.dylib");
#endif
#endif

  if (GC_register_finalizer_inner_matches.size() == 0) {
    spdlog::warn("Unable to resolve GC_register_finalizer_inner; object finalizers disabled");
    return;
  }

  const auto GC_register_finalizer_inner_match = GC_register_finalizer_inner_matches.get(0);
  GC_register_finalizer_inner = (decltype(GC_register_finalizer_inner))GC_register_finalizer_inner_match.address();
}

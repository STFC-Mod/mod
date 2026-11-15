#include "config.h"
#include "errormsg.h"

#include <il2cpp/il2cpp_helper.h>

#include <prime/Hub.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <cmath>
#include <cstring>
#include <unordered_map>

// Station-interior zoom-out: the interior view's far stop is an OrbitFrameProvider pinned at a fixed
// radius that is also the exterior station camera, so the hook pushes that radius out while the section
// is Starbase_Interior and restores native otherwise (see fix_pan.cc, which drives the same provider).
// graphics.interior_zoom_scale multiplies that native radius (1.0 = native) rather than setting an
// absolute distance like zoom/haven_zoom, so it stays resolution-independent; field resolution follows
// haven_zoom.cc.
namespace
{
FieldInfo *provider_constraint = nullptr; // AbstractOrbitFrameProvider._constraint -> OrbitConstraint
FieldInfo *constraint_radius   = nullptr; // OrbitConstraint._radius -> Constraint.FloatData
FieldInfo *radius_enabled      = nullptr; // Constraint.FloatData._enabled
FieldInfo *radius_min          = nullptr; // Constraint.FloatData._min
FieldInfo *radius_max          = nullptr; // Constraint.FloatData._max
bool       fields_ok           = false;

struct NativeRange {
  float min, max;
};
// Native (pre-widen) radius range, keyed by the Constraint.FloatData radius object, NOT the provider:
// the provider is recreated across transitions while the radius it drives persists and is shared with the
// exterior camera, so provider-keying stranded the exterior at a widened radius it never restored. The
// pointer is only a map key (the stored value is plain floats), so a retired object cannot cause a
// stale-object access.
std::unordered_map<Il2CppObject *, NativeRange> g_native;
bool                                            g_reported = false;

float InteriorScale()
{
  const auto scale = Config::Get().interior_zoom_scale;
  return (std::isfinite(scale) && scale > 1.0f) ? scale : 1.0f;
}

void OrbitFrameProvider_UpdateConstrains_Hook(auto original, Il2CppObject *provider, void *camera)
{
  if (!fields_ok || provider == nullptr) {
    original(provider, camera);
    return;
  }

  Il2CppObject *constraint = nullptr, *radius = nullptr;
  il2cpp_field_get_value(provider, provider_constraint, &constraint);
  if (constraint != nullptr) {
    il2cpp_field_get_value(constraint, constraint_radius, &radius);
  }
  if (radius == nullptr) {
    original(provider, camera);
    return;
  }

  auto      *section_manager = Hub::get_SectionManager();
  const bool in_interior =
      section_manager != nullptr && section_manager->CurrentSection == SectionID::Starbase_Interior;
  const float scale = InteriorScale();

  if (in_interior && scale > 1.0f) {
    // Capture the native range once (before any widening), keyed by this radius object.
    auto it = g_native.find(radius);
    if (it == g_native.end()) {
      NativeRange nr{0.0f, 0.0f};
      il2cpp_field_get_value(radius, radius_min, &nr.min);
      il2cpp_field_get_value(radius, radius_max, &nr.max);
      it = g_native.emplace(radius, nr).first;
      if (!g_reported) {
        bool enabled = false;
        if (radius_enabled) {
          il2cpp_field_get_value(radius, radius_enabled, &enabled);
        }
        spdlog::info("[InteriorZoom] interior orbit endpoint native radius min={} max={} enabled={} (scale={})", nr.min,
                     nr.max, enabled, scale);
        g_reported = true;
      }
    }

    // Widen relative to the captured native range. Idempotent: always derived from the stored native,
    // so reasserting each frame cannot compound. Both ends scale together so a pinned endpoint
    // (min==max) simply relocates farther out; a real range stretches outward.
    float new_min = it->second.min * scale;
    float new_max = it->second.max * scale;
    il2cpp_field_set_value(radius, radius_min, &new_min);
    il2cpp_field_set_value(radius, radius_max, &new_max);
  } else {
    // Not the interior (or feature off): the far endpoint is SHARED with the exterior view, so restore
    // native on any radius we widened, else the exterior stays zoomed out and its labels drop out of LOD.
    auto it = g_native.find(radius);
    if (it != g_native.end()) {
      float native_min = it->second.min, native_max = it->second.max;
      il2cpp_field_set_value(radius, radius_min, &native_min);
      il2cpp_field_set_value(radius, radius_max, &native_max);
    }
  }

  original(provider, camera); // native clamp now runs against the (widened or restored) range
}
} // namespace

void InstallInteriorZoomHooks()
{
  auto orbit = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "OrbitFrameProvider");
  auto abstract_orbit =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "AbstractOrbitFrameProvider");
  auto orbit_constraint =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "OrbitConstraint");
  auto constraint = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "Constraint");

  if (!orbit.isValidHelper()) {
    ErrorMsg::MissingHelper("CameraController", "OrbitFrameProvider");
    return;
  }

  if (abstract_orbit.isValidHelper()) {
    provider_constraint = abstract_orbit.GetField("_constraint").get_info();
  }
  if (orbit_constraint.isValidHelper()) {
    constraint_radius = orbit_constraint.GetField("_radius").get_info();
  }
  if (constraint.isValidHelper()) {
    Il2CppClass *float_data = nullptr;
    void        *iterator   = nullptr;
    while (auto *nested = il2cpp_class_get_nested_types(constraint.get_cls(), &iterator)) {
      if (std::strcmp(il2cpp_class_get_name(nested), "FloatData") == 0) {
        float_data = nested;
        break;
      }
    }
    if (float_data != nullptr) {
      IL2CppClassHelper fd(float_data);
      radius_enabled = fd.GetField("_enabled").get_info();
      radius_min     = fd.GetField("_min").get_info();
      radius_max     = fd.GetField("_max").get_info();
    }
  }

  fields_ok =
      provider_constraint != nullptr && constraint_radius != nullptr && radius_min != nullptr && radius_max != nullptr;
  if (!fields_ok) {
    spdlog::warn("[InteriorZoom] orbit-constraint fields not resolved; interior zoom disabled, native behavior kept");
    return;
  }

  auto ptr_update_constrains = orbit.GetMethod("UpdateConstrains");
  if (ptr_update_constrains == nullptr) {
    ErrorMsg::MissingMethod("OrbitFrameProvider", "UpdateConstrains");
    return;
  }
  if (SPUD_STATIC_DETOUR(ptr_update_constrains, OrbitFrameProvider_UpdateConstrains_Hook)) {
    spdlog::info("[InteriorZoom] installed interior orbit-endpoint hook (scale={})", InteriorScale());
  } else {
    spdlog::warn("[InteriorZoom] OrbitFrameProvider.UpdateConstrains detour failed; native behavior kept");
  }
}

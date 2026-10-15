#include "config.h"
#include "features/features.h"

#include "game/navigation.h"
#include <il2cpp/hook.h>

#include <game/app.h>

#include <spdlog/spdlog.h>

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
struct NativeRange {
  float min, max;
};
// Native (pre-widen) radius range, keyed by the Constraint.FloatData radius object, NOT the provider:
// the provider is recreated across transitions while the radius it drives persists and is shared with the
// exterior camera, so provider-keying stranded the exterior at a widened radius it never restored. The
// pointer is only a map key (the stored value is plain floats), so a retired object cannot cause a
// stale-object access.
std::unordered_map<InteriorRadiusConstraint*, NativeRange> g_native;
bool                                                       g_reported = false;

float InteriorScale()
{
  const auto scale = Config::Get().graphics.interior_zoom_scale;
  return (std::isfinite(scale) && scale > 1.0f) ? scale : 1.0f;
}

void OrbitFrameProvider_UpdateConstrains_Hook(auto original, OrbitFrameProvider* provider, void* camera)
{
  if (provider == nullptr) {
    original(provider, camera);
    return;
  }

  auto* constraint = provider->As<InteriorOrbitProvider>()->Constraint();
  auto* radius     = constraint ? constraint->Radius() : nullptr;
  if (radius == nullptr) {
    original(provider, camera);
    return;
  }

  auto*      section_manager = Hub::Sections();
  const bool in_interior =
      section_manager != nullptr && section_manager->CurrentSection() == SectionID::Starbase_Interior;
  const float scale = InteriorScale();

  if (in_interior && scale > 1.0f) {
    // Capture the native range once (before any widening), keyed by this radius object.
    auto it = g_native.find(radius);
    if (it == g_native.end()) {
      NativeRange nr{0.0f, 0.0f};
      nr.min = radius->Minimum();
      nr.max = radius->Maximum();
      it     = g_native.emplace(radius, nr).first;
      if (!g_reported) {
        const bool enabled = radius->Enabled();
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
    radius->SetMinimum(new_min);
    radius->SetMaximum(new_max);
  } else {
    // Not the interior (or feature off): the far endpoint is SHARED with the exterior view, so restore
    // native on any radius we widened, else the exterior stays zoomed out and its labels drop out of LOD.
    auto it = g_native.find(radius);
    if (it != g_native.end()) {
      float native_min = it->second.min, native_max = it->second.max;
      radius->SetMinimum(native_min);
      radius->SetMaximum(native_max);
    }
  }

  original(provider, camera); // native clamp now runs against the (widened or restored) range
}
} // namespace

namespace
{
void InstallInteriorZoomHooks()
{
  const bool ready = InteriorOrbitProvider::Class::HasField<"_constraint", InteriorOrbitConstraint*>()
                     && InteriorOrbitConstraint::Class::HasField<"_radius", InteriorRadiusConstraint*>()
                     && InteriorRadiusConstraint::Class::HasField<"_min", float>()
                     && InteriorRadiusConstraint::Class::HasField<"_max", float>();
  if (!ready) {
    spdlog::warn("[InteriorZoom] orbit-constraint fields not resolved; native behavior kept");
    return;
  }
  IL2CPP_HOOK(OrbitFrameProvider::Class::Method("UpdateConstrains"), OrbitFrameProvider_UpdateConstrains_Hook);
}
} // namespace

const Feature kInteriorZoom{"InteriorZoom", &Config::Patches::interiorzoomhooks, InstallInteriorZoomHooks};

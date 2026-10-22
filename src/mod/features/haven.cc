// Haven, the planetary base: [graphics] haven_zoom, which extends the camera's zoom-out and keeps the roads drawn
// above the ground at that distance, and [ui] reverse_haven_history, which lists help history newest first.

#include "config.h"
#include "features/features.h"

#include "game/camera.h"
#include "game/haven.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <concepts>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
// --- Haven zoom ---

// From further out the roads sink into the ground mesh. A depth bias on the road materials keeps them drawn on top.
// Finding the materials (GetComponentsInChildren and every material's name) is the costly part, so it runs every 5 s
// while the bias is checked every second. The handles are weak, so a cached material never keeps an unloaded scene or
// asset alive.
class HavenRoads
{
public:
  void Apply()
  {
    if (!apply_.Due())
      return;
    if (auto* owner = owner_.Get<unity::GameObject>(); !unity::Alive(owner) || !owner->activeInHierarchy()) {
      owner_.Reset();
      materials_.clear();
    }
    // A replacement can take the visuals' path while the old object is still alive and active, so the owner is
    // looked up again at every search, not only once it is gone.
    if (search_.Due())
      Search();
    for (const auto& handle : materials_)
      if (auto* material = handle.Get<unity::Material>(); unity::Alive(material))
        Bias(material);
  }

private:
  void Search()
  {
    materials_.clear();
    auto* root = unity::GameObject::Find("StarbaseRoot/StarbaseManager/StarbasePlanetaryVisualsHolder");
    owner_     = il2cpp::GcHandle::Weak(root);
    std::unordered_set<unity::Material*> seen;
    for (auto* renderer : il2cpp::Items(root ? root->GetComponentsInChildren<unity::Renderer>(true) : nullptr))
      for (auto* material : il2cpp::Items(renderer ? renderer->sharedMaterials() : nullptr))
        if (material && IsRoad(material) && seen.insert(material).second)
          materials_.push_back(il2cpp::GcHandle::Weak(material));
  }

  static bool IsRoad(unity::Material* material)
  {
    static constexpr std::string_view kRoadMaterials[] = {
        "mat_PB_3101101_asphalt_", "mat_PB_3101201_asphalt_", "mat_PB_3101301_asphalt_", "mat_PB_3101401_asphalt_",
        "mat_PB_3101501_asphalt_", "mat_PB_3101601_asphalt_", "mat_PB_3101701_asphalt_"};
    const auto name = material->Name();
    if (!std::ranges::any_of(kRoadMaterials, [&](std::string_view prefix) { return name.starts_with(prefix); }))
      return false;
    auto* shader = material->shader();
    return shader && shader->Name() == "Custom/LitMSAE";
  }

  static void Bias(unity::Material* material)
  {
    const std::array properties{il2cpp::NewString("_OffsetFactor"), il2cpp::NewString("_OffsetUnits")};
    if (!material->HasProperty(properties[0]) || !material->HasProperty(properties[1]))
      return;
    const std::array before{material->GetFloat(properties[0]), material->GetFloat(properties[1])};
    if (!std::isfinite(before[0]) || !std::isfinite(before[1]))
      return;
    // Negative offsets pull the roads toward the camera. Keep an already stronger native bias.
    for (size_t i = 0; i < properties.size(); ++i)
      if (before[i] > -1.0f)
        material->SetFloat(properties[i], -1.0f);
  }

  Throttle                      apply_{std::chrono::seconds(1)}, search_{std::chrono::seconds(5)};
  il2cpp::GcHandle              owner_;
  std::vector<il2cpp::GcHandle> materials_;
};

void ApplyHavenRoadDepthBias()
{
  // Leaked: its handles must not be freed by static destructors after the runtime is gone.
  static auto* roads = new HavenRoads;
  roads->Apply();
}

// Haven's zoom blends between two planetary orbits, each pinned to one radius (min == max) and sitting on it. Reads
// that radius, or nothing for any other kind of endpoint.
std::optional<float> HavenOrbitRadius(FrameProvider* provider)
{
  if (!provider || !provider->Is<PlanetaryOrbitFrameProvider>() || !provider->Is<AbstractOrbitFrameProvider>())
    return std::nullopt;
  auto* orbit      = provider->As<AbstractOrbitFrameProvider>();
  auto* constraint = orbit->constraint();
  auto* data       = constraint ? constraint->radius() : nullptr;
  if (!data || !data->enabled())
    return std::nullopt;
  const auto minimum = data->min(), maximum = data->max();
  const auto radius = orbit->radius();
  if (std::isfinite(radius) && std::isfinite(maximum) && maximum > 0.0f && minimum == maximum
      && std::abs(radius - maximum) < 0.01f)
    return radius;
  return std::nullopt;
}

// Every value is finite: NaN and infinity both fail.
bool HavenFinite(std::floating_point auto... values)
{ return (std::isfinite(values) && ...); }

void BlendFrameProvider_UpdateCameraFrame_Hook(auto original, BlendFrameProvider* _this, unity::Camera* camera)
{
  // Native input, constraints and endpoint updates run first. Only this blend's newly generated output is extended;
  // endpoint assets and normalized LOD stay native.
  original(_this, camera);
  const auto max_zoom = Config::Get().graphics.haven_zoom;
  if (!std::isfinite(max_zoom) || max_zoom <= 0.0f)
    return;

  auto*      source        = _this->sourceFrameProvider();
  auto*      target        = _this->targetFrameProvider();
  const auto source_pinned = HavenOrbitRadius(source);
  const auto target_pinned = source_pinned ? HavenOrbitRadius(target) : std::nullopt;
  if (!target_pinned)
    return;
  const float source_radius = *source_pinned, target_radius = *target_pinned;
  if (source_radius == target_radius || max_zoom <= std::max(source_radius, target_radius))
    return;

  auto* source_orbit = source->As<AbstractOrbitFrameProvider>();
  auto* target_orbit = target->As<AbstractOrbitFrameProvider>();
  auto* pivot        = source_orbit->orbitPivot();
  if (!pivot || target_orbit->orbitPivot() != pivot || source_orbit->lookTarget() != pivot
      || target_orbit->lookTarget() != pivot)
    return;
  auto*        source_frame = _this->sourceFrame();
  auto*        target_frame = _this->targetFrame();
  CameraFrame* result       = _this->blendedCameraFrame();
  auto*        curve        = _this->blendCurve();
  const auto   minimum = _this->softLimitMin(), maximum = _this->softLimitMax(), ratio = _this->blendRatio();
  if (!source_frame || !target_frame || !result || !curve || source_frame == target_frame || result == source_frame
      || result == target_frame || !HavenFinite(minimum, maximum, ratio) || minimum < 0.0f || maximum > 1.0f
      || maximum <= minimum)
    return;

  auto       position = result->position();
  const auto ar = source_frame->rotation(), br = target_frame->rotation();
  const auto af = source_frame->fov(), bf = target_frame->fov();
  const auto rotation_dot    = ar.x * br.x + ar.y * br.y + ar.z * br.z + ar.w * br.w;
  const auto gap             = std::abs(source_radius - target_radius);
  const auto rotation_length = ar.x * ar.x + ar.y * ar.y + ar.z * ar.z + ar.w * ar.w;
  // Qualify the common-pivot, same-facing perspective orbit measured in Haven: unit rotations that agree up to sign
  // (|dot| = 1) and one field of view. Only then is moving along the camera's back axis the same as a wider orbit.
  // Preserve native output when a client changes that geometry or camera mode.
  if (!HavenFinite(rotation_length, rotation_dot, af, bf) || std::abs(rotation_length - 1.0f) > 0.0001f
      || std::abs(std::abs(rotation_dot) - 1.0f) > 0.0001f || af <= 0.0f || std::abs(af - bf) > 0.001f
      || source_frame->orthographic() || target_frame->orthographic() || result->orthographic())
    return;

  const auto at_ratio   = curve->Evaluate(ratio);
  const auto at_minimum = curve->Evaluate(minimum);
  const auto at_maximum = curve->Evaluate(maximum);
  if (!HavenFinite(at_ratio, at_minimum, at_maximum))
    return;
  // Weights run from 0 at the inner orbit to 1 at the outer one, whichever way the blend goes. outer_weight and
  // inner_weight are the curve at the soft limits, where the native zoom stops; progress is how far the current
  // ratio is between them. The extra distance grows with progress up to max_zoom - native_outer at the outer stop,
  // and never takes the camera past max_zoom.
  const bool source_is_far = source_radius > target_radius;
  const auto outer_weight =
      source_is_far ? 1.0f - std::clamp(at_minimum, 0.0f, 1.0f) : std::clamp(at_maximum, 0.0f, 1.0f);
  const auto inner_weight =
      source_is_far ? 1.0f - std::clamp(at_maximum, 0.0f, 1.0f) : std::clamp(at_minimum, 0.0f, 1.0f);
  if (outer_weight <= inner_weight)
    return;
  const auto weight       = source_is_far ? 1.0 - std::clamp(at_ratio, 0.0f, 1.0f) : std::clamp(at_ratio, 0.0f, 1.0f);
  const auto inner_radius = std::min(source_radius, target_radius);
  const auto native_outer = inner_radius + gap * outer_weight;
  const auto native_distance = inner_radius + gap * weight;
  const auto progress        = std::clamp((weight - inner_weight) / (outer_weight - inner_weight), 0.0, 1.0);
  const auto extra           = std::min(max_zoom - native_distance, (max_zoom - native_outer) * progress);
  if (!std::isfinite(extra))
    return;
  ApplyHavenRoadDepthBias();
  if (extra <= 0.0)
    return;

  // Sequential endpoint updates can cache different positions of their shared pivot during pan. Use the camera back
  // axis so expansion remains steady: the rotation's forward vector is (2(xz + wy), 2(yz - wx), 1 - 2(x^2 + y^2)), and
  // the camera moves `extra` against it. The far plane moves out by the same amount.
  position.x += static_cast<float>(-2.0 * (ar.x * ar.z + ar.w * ar.y) * extra);
  position.y += static_cast<float>(-2.0 * (ar.y * ar.z - ar.w * ar.x) * extra);
  position.z += static_cast<float>((2.0 * (ar.x * ar.x + ar.y * ar.y) - 1.0) * extra);
  auto clip = result->farPlane();
  if (!std::isfinite(clip) || clip <= 0.0f)
    return;
  clip += static_cast<float>(extra);
  if (!HavenFinite(position.x, position.y, position.z, clip) || clip <= 0.0f)
    return;
  result->SetPosition(position);
  result->SetFarPlane(clip);
  static bool reported = false;
  if (!std::exchange(reported, true))
    spdlog::info("[HavenZoom] expanded native outer distance {} to maximum {}", native_outer, max_zoom);
}

// --- Help history ---

void PlanetaryHelpHistoryContext_ApplyRecords_Hook(auto original, PlanetaryHelpHistoryContext* _this,
                                                   il2cpp::List<Il2CppObject*>* records)
{
  if (records && Config::Get().ui.reverse_haven_history) {
    // Swap through the managed setter so reference writes keep their GC barriers.
    for (int32_t i = 0, j = records->Count() - 1; i < j; ++i, --j) {
      auto* first = (*records)[i];
      auto* last  = (*records)[j];
      records->Set(i, last);
      records->Set(j, first);
    }
  }
  original(_this, records);
}

bool InstallHavenZoom()
{
  il2cpp::HookSet hooks("HavenZoom");
  IL2CPP_STAGE(hooks, BlendFrameProvider::Class::Method<void(unity::Camera*)>("UpdateCameraFrame"),
               BlendFrameProvider_UpdateCameraFrame_Hook);
  return hooks.Commit();
}

bool InstallHavenHistory()
{
  il2cpp::HookSet hooks("HavenHistory");
  IL2CPP_STAGE(hooks, PlanetaryHelpHistoryContext::Class::Method("ApplyRecords", 1),
               PlanetaryHelpHistoryContext_ApplyRecords_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kHavenZoom{"HavenZoom", &Config::Patches::havenzoomhooks, InstallHavenZoom};
const Feature kHavenHistory{"HavenHistory", &Config::Patches::havenhistoryhooks, InstallHavenHistory};

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

namespace
{
// --- Haven zoom ---

// From further out the roads sink into the ground mesh. A depth bias on the road materials keeps them drawn on top.
void ApplyHavenRoadDepthBias()
{
  // Once a second: the scene search below is too costly for every frame.
  static Throttle throttle(std::chrono::seconds(1));
  if (!throttle.Due())
    return;
  auto* root      = unity::GameObject::Find("StarbaseRoot/StarbaseManager/StarbasePlanetaryVisualsHolder");
  auto* renderers = root ? root->GetComponentsInChildren<unity::Renderer>(true) : nullptr;
  if (!renderers)
    return;
  for (auto* renderer : *renderers) {
    auto* materials = renderer ? renderer->sharedMaterials() : nullptr;
    if (!materials)
      continue;
    for (auto* material : *materials) {
      if (!material)
        continue;
      const auto name = material->Name();
      bool       road = false;
      for (const auto* prefix :
           {"mat_PB_3101101_asphalt_", "mat_PB_3101201_asphalt_", "mat_PB_3101301_asphalt_", "mat_PB_3101401_asphalt_",
            "mat_PB_3101501_asphalt_", "mat_PB_3101601_asphalt_", "mat_PB_3101701_asphalt_"})
        road |= name.starts_with(prefix);
      auto* shader = road ? material->shader() : nullptr;
      if (!shader || shader->Name() != "Custom/LitMSAE")
        continue;
      const std::array properties{il2cpp::NewString("_OffsetFactor"), il2cpp::NewString("_OffsetUnits")};
      if (!material->HasProperty(properties[0]) || !material->HasProperty(properties[1]))
        continue;
      const std::array before{material->GetFloat(properties[0]), material->GetFloat(properties[1])};
      if (!std::isfinite(before[0]) || !std::isfinite(before[1]))
        continue;
      // Keep an already stronger native bias. Resolve scene objects afresh each time.
      for (size_t i = 0; i < properties.size(); ++i)
        if (before[i] > -1.0f)
          material->SetFloat(properties[i], -1.0f);
    }
  }
}

bool ReadHavenRadius(FrameProvider* provider, float& radius)
{
  if (!provider || !provider->Is<PlanetaryOrbitFrameProvider>() || !provider->Is<AbstractOrbitFrameProvider>())
    return false;
  auto* orbit      = provider->As<AbstractOrbitFrameProvider>();
  auto* constraint = orbit->constraint();
  auto* data       = constraint ? constraint->radius() : nullptr;
  if (!data || !data->enabled())
    return false;
  const auto minimum = data->min(), maximum = data->max();
  radius = orbit->radius();
  return std::isfinite(radius) && std::isfinite(maximum) && maximum > 0.0f && minimum == maximum
         && std::abs(radius - maximum) < 0.01f;
}

void BlendFrameProvider_UpdateCameraFrame_Hook(auto original, BlendFrameProvider* _this, unity::Camera* camera)
{
  // Native input, constraints and endpoint updates run first. Only this blend's newly generated output is extended;
  // endpoint assets and normalized LOD stay native.
  original(_this, camera);
  const auto max_zoom = Config::Get().graphics.haven_zoom;
  if (!std::isfinite(max_zoom) || max_zoom <= 0.0f)
    return;

  auto* source        = _this->sourceFrameProvider();
  auto* target        = _this->targetFrameProvider();
  float source_radius = 0.0f, target_radius = 0.0f;
  if (!ReadHavenRadius(source, source_radius) || !ReadHavenRadius(target, target_radius)
      || source_radius == target_radius || max_zoom <= std::max(source_radius, target_radius))
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
      || result == target_frame || !std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(ratio)
      || minimum < 0.0f || maximum > 1.0f || maximum <= minimum)
    return;

  auto       position = result->position();
  const auto ar = source_frame->rotation(), br = target_frame->rotation();
  const auto af = source_frame->fov(), bf = target_frame->fov();
  const auto source_orthographic = source_frame->orthographic();
  const auto target_orthographic = target_frame->orthographic();
  const auto result_orthographic = result->orthographic();
  const auto rotation_dot        = ar.x * br.x + ar.y * br.y + ar.z * br.z + ar.w * br.w;
  const auto gap                 = std::abs(source_radius - target_radius);
  const auto rotation_length     = ar.x * ar.x + ar.y * ar.y + ar.z * ar.z + ar.w * ar.w;
  // Qualify the common-pivot, same-facing perspective orbit measured in Haven. Preserve native output when a client
  // changes that geometry or camera mode.
  if (!std::isfinite(rotation_length) || std::abs(rotation_length - 1.0f) > 0.0001f || !std::isfinite(rotation_dot)
      || std::abs(std::abs(rotation_dot) - 1.0f) > 0.0001f || !std::isfinite(af) || !std::isfinite(bf) || af <= 0.0f
      || std::abs(af - bf) > 0.001f || source_orthographic || target_orthographic || result_orthographic)
    return;

  const auto at_ratio   = curve->Evaluate(ratio);
  const auto at_minimum = curve->Evaluate(minimum);
  const auto at_maximum = curve->Evaluate(maximum);
  if (!std::isfinite(at_ratio) || !std::isfinite(at_minimum) || !std::isfinite(at_maximum))
    return;
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
  // axis so expansion remains steady.
  position.x += static_cast<float>(-2.0 * (ar.x * ar.z + ar.w * ar.y) * extra);
  position.y += static_cast<float>(-2.0 * (ar.y * ar.z - ar.w * ar.x) * extra);
  position.z += static_cast<float>((2.0 * (ar.x * ar.x + ar.y * ar.y) - 1.0) * extra);
  auto clip = result->farPlane();
  if (!std::isfinite(clip) || clip <= 0.0f)
    return;
  clip += static_cast<float>(extra);
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(clip)
      || clip <= 0.0f)
    return;
  result->SetPosition(position);
  result->SetFarPlane(clip);
  static bool reported = false;
  if (!reported) {
    spdlog::info("[HavenZoom] expanded native outer distance {} to maximum {}", native_outer, max_zoom);
    reported = true;
  }
}

// --- Help history ---

void PlanetaryHelpHistoryContext_ApplyRecords_Hook(auto original, PlanetaryHelpHistoryContext* _this,
                                                   il2cpp::List<Il2CppObject*>* records)
{
  if (records && Config::Get().ui.reverse_haven_history) {
    // Swap through the managed setter so reference writes keep their GC barriers.
    const auto count = records->Count();
    for (int32_t i = 0; i < count / 2; ++i) {
      auto* first = (*records)[i];
      auto* last  = (*records)[count - i - 1];
      records->Set(i, last);
      records->Set(count - i - 1, first);
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

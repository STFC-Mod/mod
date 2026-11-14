// Haven's extended camera range, road surface depth and help-history ordering.
#include "game/haven.h"
#include "features/features.h"

#include <il2cpp/hook.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace
{
// The road materials under Haven's visuals. Searching for them (GetComponentsInChildren and every material's name) is
// the costly part, so it runs every 5 s while the bias is checked every second. Weak handles, so a cached material
// never keeps an unloaded scene or asset alive.
class HavenRoads
{
  using Clock = std::chrono::steady_clock;

public:
  void Apply()
  {
    const auto now = Clock::now();
    if (now < next_apply_)
      return;
    next_apply_ = now + std::chrono::seconds(1);
    if (auto* owner = owner_.Get<unity::GameObject>(); !unity::Alive(owner) || !owner->activeInHierarchy()) {
      owner_.Reset();
      materials_.clear();
    }
    // A replacement can take the visuals' path while the old object is still alive and active, so the owner is
    // looked up again at every search, not only once it is gone.
    if (now >= next_search_) {
      next_search_ = now + std::chrono::seconds(5);
      Search();
    }
    for (const auto& handle : materials_)
      if (auto* material = handle.Get<unity::Material>(); unity::Alive(material))
        Bias(material);
  }

private:
  void Search()
  {
    materials_.clear();
    auto* root      = unity::GameObject::Find("StarbaseRoot/StarbaseManager/StarbasePlanetaryVisualsHolder");
    owner_          = il2cpp::GcHandle::Weak(root);
    auto* renderers = root ? root->GetComponentsInChildren<unity::Renderer>(true) : nullptr;
    if (!renderers || renderers->size() > 4096)
      return;
    std::unordered_set<unity::Material*> seen;
    for (auto* renderer : *renderers) {
      auto* materials = renderer ? renderer->SharedMaterials() : nullptr;
      if (!materials || materials->size() > 16)
        continue;
      for (auto* material : *materials)
        if (material && IsRoad(material) && seen.insert(material).second)
          materials_.push_back(il2cpp::GcHandle::Weak(material));
    }
  }

  static bool IsRoad(unity::Material* material)
  {
    const auto name = material->Name();
    bool       road = false;
    for (const auto* prefix :
         {"mat_PB_3101101_asphalt_", "mat_PB_3101201_asphalt_", "mat_PB_3101301_asphalt_", "mat_PB_3101401_asphalt_",
          "mat_PB_3101501_asphalt_", "mat_PB_3101601_asphalt_", "mat_PB_3101701_asphalt_"})
      road |= name.starts_with(prefix);
    auto* shader = road ? material->shader() : nullptr;
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
    // Keep an already stronger native bias.
    for (size_t i = 0; i < properties.size(); ++i)
      if (before[i] > -1.0f)
        material->SetFloat(properties[i], -1.0f);
  }

  il2cpp::GcHandle              owner_;
  std::vector<il2cpp::GcHandle> materials_;
  Clock::time_point             next_apply_{}, next_search_{};
};

void ApplyHavenRoadDepthBias()
{
  static const bool available = unity::Material::Class::Method<bool(Il2CppString*)>("HasProperty")
                                && unity::Material::Class::Method<float(Il2CppString*)>("GetFloat")
                                && unity::Material::Class::Method<void(Il2CppString*, float)>("SetFloat");
  if (!available)
    return;
  // Leaked: its handles must not be freed by static destructors after the runtime is gone.
  static auto* roads = new HavenRoads;
  roads->Apply();
}

bool ReadHavenRadius(HavenFrameProvider* provider, float& radius)
{
  if (!provider || !provider->Is<HavenPlanetaryProvider>() || !provider->Is<HavenOrbitProvider>())
    return false;
  auto* orbit      = provider->As<HavenOrbitProvider>();
  auto* constraint = orbit->Constraint();
  auto* data       = constraint ? constraint->Radius() : nullptr;
  if (!data || !data->Enabled())
    return false;
  const auto minimum = data->Minimum(), maximum = data->Maximum();
  radius = orbit->Radius();
  return std::isfinite(radius) && std::isfinite(maximum) && maximum > 0.0f && minimum == maximum
         && std::abs(radius - maximum) < 0.01f;
}

void HavenCamera_UpdateCameraFrame_Hook(auto original, HavenBlendProvider* provider, unity::Camera* camera)
{
  // Native input, constraints and endpoint updates run first. Only this blend's
  // newly generated output is extended; endpoint assets and normalized LOD stay native.
  original(provider, camera);
  const auto max_zoom = Config::Get().graphics.haven_zoom;
  if (provider == nullptr || !std::isfinite(max_zoom) || max_zoom <= 0.0f)
    return;

  auto* source        = provider->Source();
  auto* target        = provider->Target();
  float source_radius = 0.0f, target_radius = 0.0f;
  if (!ReadHavenRadius(source, source_radius) || !ReadHavenRadius(target, target_radius)
      || source_radius == target_radius || max_zoom <= std::max(source_radius, target_radius))
    return;

  auto* source_orbit = source->As<HavenOrbitProvider>();
  auto* target_orbit = target->As<HavenOrbitProvider>();
  auto* pivot        = source_orbit->Pivot();
  if (!pivot || target_orbit->Pivot() != pivot || source_orbit->LookTarget() != pivot
      || target_orbit->LookTarget() != pivot)
    return;
  auto*             source_frame = provider->SourceFrame();
  auto*             target_frame = provider->TargetFrame();
  HavenCameraFrame* result       = provider->ResultFrame();
  auto*             curve        = provider->Curve();
  const auto        minimum = provider->Minimum(), maximum = provider->Maximum(), ratio = provider->Ratio();
  if (source_frame == nullptr || target_frame == nullptr || result == nullptr || curve == nullptr
      || source_frame == target_frame || result == source_frame || result == target_frame || !std::isfinite(minimum)
      || !std::isfinite(maximum) || !std::isfinite(ratio) || minimum < 0.0f || maximum > 1.0f || maximum <= minimum)
    return;

  auto       position = result->Position();
  const auto ar = source_frame->Rotation(), br = target_frame->Rotation();
  const auto af = source_frame->Fov(), bf = target_frame->Fov();
  const auto source_orthographic = source_frame->Orthographic();
  const auto target_orthographic = target_frame->Orthographic();
  const auto result_orthographic = result->Orthographic();
  const auto rotation_dot        = ar.x * br.x + ar.y * br.y + ar.z * br.z + ar.w * br.w;
  const auto gap                 = std::abs(source_radius - target_radius);
  const auto rotation_length     = ar.x * ar.x + ar.y * ar.y + ar.z * ar.z + ar.w * ar.w;
  // Qualify the common-pivot, same-facing perspective orbit measured in Haven.
  // Preserve native output when a client changes that geometry or camera mode.
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

  // Sequential endpoint updates can cache different positions of their shared
  // pivot during pan. Use the camera back axis so expansion remains steady.
  position.x += static_cast<float>(-2.0 * (ar.x * ar.z + ar.w * ar.y) * extra);
  position.y += static_cast<float>(-2.0 * (ar.y * ar.z - ar.w * ar.x) * extra);
  position.z += static_cast<float>((2.0 * (ar.x * ar.x + ar.y * ar.y) - 1.0) * extra);
  auto clip = result->FarClip();
  if (!std::isfinite(clip) || clip <= 0.0f)
    return;
  clip += static_cast<float>(extra);
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(clip)
      || clip <= 0.0f)
    return;
  result->SetPosition(position);
  result->SetFarClip(clip);
  static bool reported = false;
  if (!reported) {
    spdlog::info("[HavenZoom] expanded native outer distance {} to maximum {}", native_outer, max_zoom);
    reported = true;
  }
}

void PlanetaryHelpHistoryContext_ApplyRecords_Hook(auto original, PlanetaryHelpHistoryContext* _this,
                                                   il2cpp::List<Il2CppObject*>* records)
{
  if (records && Config::Get().ui.reverse_haven_history) {
    const auto count = records->Count();
    if (count >= 0 && count <= 2000) {
      if (!records->Method<Il2CppObject*(int32_t)>("get_Item")
          || !records->Method<void(int32_t, Il2CppObject*)>("set_Item")) {
        original(_this, records);
        return;
      }
      // Swap through the managed setter so reference writes keep their GC barriers.
      for (int32_t i = 0; i < count / 2; ++i) {
        auto* first = (*records)[i];
        auto* last  = (*records)[count - i - 1];
        records->Call<"set_Item">(i, last);
        records->Call<"set_Item">(count - i - 1, first);
      }
    } else {
      spdlog::warn("Haven history count {} is implausible; keeping native order", count);
    }
  }
  original(_this, records);
}

void InstallHavenZoom()
{
  if (!HavenCameraBindingsReady())
    return;
  IL2CPP_HOOK(HavenBlendProvider::Class::Method<void(unity::Camera*)>("UpdateCameraFrame"),
              HavenCamera_UpdateCameraFrame_Hook);
}

void InstallHavenHistory()
{
  IL2CPP_HOOK(PlanetaryHelpHistoryContext::Class::Method("ApplyRecords", 1),
              PlanetaryHelpHistoryContext_ApplyRecords_Hook);
}
} // namespace

const Feature kHavenZoom{"HavenZoom", &Config::Patches::havenzoomhooks, InstallHavenZoom};
const Feature kHavenHistory{"HavenHistory", &Config::Patches::havenhistoryhooks, InstallHavenHistory};

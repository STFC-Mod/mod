#include "config.h"

#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>
#include <prime/Camera.h>
#include <prime/Vector3.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

void ApplyHavenRoadDepthBias();

namespace
{
Il2CppClass      *planetary_provider_class = nullptr;
FieldInfo        *blend_source = nullptr, *blend_target = nullptr, *blend_curve = nullptr;
FieldInfo        *blend_minimum = nullptr, *blend_maximum = nullptr, *blend_ratio = nullptr;
FieldInfo        *blend_source_frame = nullptr, *blend_target_frame = nullptr, *blend_result_frame = nullptr;
FieldInfo        *provider_constraint = nullptr, *provider_radius = nullptr, *constraint_radius = nullptr;
FieldInfo        *provider_pivot = nullptr, *provider_look_target = nullptr;
FieldInfo        *radius_enabled = nullptr, *radius_minimum = nullptr, *radius_maximum = nullptr;
FieldInfo        *frame_position = nullptr, *frame_rotation = nullptr, *frame_fov = nullptr;
FieldInfo        *frame_far_clip = nullptr, *frame_orthographic = nullptr;
const MethodInfo *curve_evaluate = nullptr;

struct HavenRotation {
  float x, y, z, w;
};

bool ReadHavenRadius(Il2CppObject *provider, float &radius)
{
  if (provider == nullptr || !il2cpp_class_is_assignable_from(planetary_provider_class, provider->klass))
    return false;
  Il2CppObject *constraint = nullptr, *data = nullptr;
  il2cpp_field_get_value(provider, provider_constraint, &constraint);
  if (constraint == nullptr)
    return false;
  il2cpp_field_get_value(constraint, constraint_radius, &data);
  if (data == nullptr)
    return false;
  bool enabled = false;
  il2cpp_field_get_value(data, radius_enabled, &enabled);
  if (!enabled)
    return false;
  float minimum = 0.0f, maximum = 0.0f;
  il2cpp_field_get_value(data, radius_minimum, &minimum);
  il2cpp_field_get_value(data, radius_maximum, &maximum);
  il2cpp_field_get_value(provider, provider_radius, &radius);
  return std::isfinite(radius) && std::isfinite(maximum) && maximum > 0.0f && minimum == maximum
         && std::abs(radius - maximum) < 0.01f;
}

void HavenCamera_UpdateCameraFrame_Hook(auto original, Il2CppObject *provider, Camera *camera)
{
  // Native input, constraints and endpoint updates run first. Only this blend's
  // newly generated output is extended; endpoint assets and normalized LOD stay native.
  original(provider, camera);
  const auto max_zoom = Config::Get().haven_zoom;
  if (provider == nullptr || !std::isfinite(max_zoom) || max_zoom <= 0.0f)
    return;

  Il2CppObject *source = nullptr, *target = nullptr;
  il2cpp_field_get_value(provider, blend_source, &source);
  il2cpp_field_get_value(provider, blend_target, &target);
  float source_radius = 0.0f, target_radius = 0.0f;
  if (!ReadHavenRadius(source, source_radius) || !ReadHavenRadius(target, target_radius)
      || source_radius == target_radius || max_zoom <= std::max(source_radius, target_radius))
    return;

  Il2CppObject *pivot = nullptr, *target_pivot = nullptr, *source_look_target = nullptr, *target_look_target = nullptr;
  il2cpp_field_get_value(source, provider_pivot, &pivot);
  il2cpp_field_get_value(target, provider_pivot, &target_pivot);
  il2cpp_field_get_value(source, provider_look_target, &source_look_target);
  il2cpp_field_get_value(target, provider_look_target, &target_look_target);
  if (pivot == nullptr || target_pivot != pivot || source_look_target != pivot || target_look_target != pivot)
    return;
  Il2CppObject *source_frame = nullptr, *target_frame = nullptr, *result = nullptr, *curve = nullptr;
  float         minimum = 0.0f, maximum = 0.0f, ratio = 0.0f;
  il2cpp_field_get_value(provider, blend_source_frame, &source_frame);
  il2cpp_field_get_value(provider, blend_target_frame, &target_frame);
  il2cpp_field_get_value(provider, blend_result_frame, &result);
  il2cpp_field_get_value(provider, blend_curve, &curve);
  il2cpp_field_get_value(provider, blend_minimum, &minimum);
  il2cpp_field_get_value(provider, blend_maximum, &maximum);
  il2cpp_field_get_value(provider, blend_ratio, &ratio);
  if (source_frame == nullptr || target_frame == nullptr || result == nullptr || curve == nullptr
      || source_frame == target_frame || result == source_frame || result == target_frame || !std::isfinite(minimum)
      || !std::isfinite(maximum) || !std::isfinite(ratio) || minimum < 0.0f || maximum > 1.0f || maximum <= minimum)
    return;

  Vector3       position{};
  HavenRotation ar{}, br{};
  float         af = 0.0f, bf = 0.0f;
  bool          source_orthographic = false, target_orthographic = false, result_orthographic = false;
  il2cpp_field_get_value(result, frame_position, &position);
  il2cpp_field_get_value(source_frame, frame_rotation, &ar);
  il2cpp_field_get_value(target_frame, frame_rotation, &br);
  il2cpp_field_get_value(source_frame, frame_fov, &af);
  il2cpp_field_get_value(target_frame, frame_fov, &bf);
  il2cpp_field_get_value(source_frame, frame_orthographic, &source_orthographic);
  il2cpp_field_get_value(target_frame, frame_orthographic, &target_orthographic);
  il2cpp_field_get_value(result, frame_orthographic, &result_orthographic);
  const auto rotation_dot    = ar.x * br.x + ar.y * br.y + ar.z * br.z + ar.w * br.w;
  const auto gap             = std::abs(source_radius - target_radius);
  const auto rotation_length = ar.x * ar.x + ar.y * ar.y + ar.z * ar.z + ar.w * ar.w;
  // Qualify the common-pivot, same-facing perspective orbit measured in Haven.
  // Preserve native output when a client changes that geometry or camera mode.
  if (!std::isfinite(rotation_length) || std::abs(rotation_length - 1.0f) > 0.0001f || !std::isfinite(rotation_dot)
      || std::abs(std::abs(rotation_dot) - 1.0f) > 0.0001f || !std::isfinite(af) || !std::isfinite(bf) || af <= 0.0f
      || std::abs(af - bf) > 0.001f || source_orthographic || target_orthographic || result_orthographic)
    return;

  const auto evaluate =
      reinterpret_cast<float (*)(Il2CppObject *, float, const MethodInfo *)>(curve_evaluate->methodPointer);
  const auto at_ratio   = evaluate(curve, ratio, curve_evaluate);
  const auto at_minimum = evaluate(curve, minimum, curve_evaluate);
  const auto at_maximum = evaluate(curve, maximum, curve_evaluate);
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
  float clip = 0.0f;
  il2cpp_field_get_value(result, frame_far_clip, &clip);
  if (!std::isfinite(clip) || clip <= 0.0f)
    return;
  clip += static_cast<float>(extra);
  if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(clip)
      || clip <= 0.0f)
    return;
  il2cpp_field_set_value(result, frame_position, &position);
  il2cpp_field_set_value(result, frame_far_clip, &clip);
  static bool reported = false;
  if (!reported) {
    spdlog::info("[HavenZoom] expanded native outer distance {} to maximum {}", native_outer, max_zoom);
    reported = true;
  }
}
} // namespace

void InstallHavenZoomHooks()
{
  auto blend = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "BlendFrameProvider");
  auto frame_provider = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "FrameProvider");
  auto orbit =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "AbstractOrbitFrameProvider");
  auto target = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "AbstractTargetController");
  auto planetary =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "PlanetaryOrbitFrameProvider");
  auto orbit_constraint =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "OrbitConstraint");
  auto constraint = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "Constraint");
  auto frame      = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "CameraFrame");
  auto editable   = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.CameraController", "EditableCameraFrame");
  auto curve      = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "AnimationCurve");
  for (const auto &[name, helper] : {std::pair{"BlendFrameProvider", &blend},
                                     {"FrameProvider", &frame_provider},
                                     {"AbstractOrbitFrameProvider", &orbit},
                                     {"AbstractTargetController", &target},
                                     {"PlanetaryOrbitFrameProvider", &planetary},
                                     {"OrbitConstraint", &orbit_constraint},
                                     {"Constraint", &constraint},
                                     {"CameraFrame", &frame},
                                     {"EditableCameraFrame", &editable},
                                     {"AnimationCurve", &curve}}) {
    if (helper->get_cls() == nullptr) {
      spdlog::warn("[HavenZoom] class {} not found; skipping hook installation and keeping native zoom range", name);
      return;
    }
  }
  Il2CppClass *radius_class = nullptr;
  void        *iterator     = nullptr;
  while (auto *nested = il2cpp_class_get_nested_types(constraint.get_cls(), &iterator)) {
    if (std::strcmp(il2cpp_class_get_name(nested), "FloatData") == 0) {
      radius_class = nested;
      break;
    }
  }
  if (radius_class == nullptr) {
    spdlog::warn("[HavenZoom] nested class Constraint.FloatData not found; skipping hook installation and keeping "
                 "native zoom range");
    return;
  }
  IL2CppClassHelper radius(radius_class);
  const auto       *update =
      method_contract::Resolve(blend.get_cls(), "UpdateCameraFrame", false, "System.Void", {"UnityEngine.Camera"});
  curve_evaluate = method_contract::Resolve(curve.get_cls(), "Evaluate", false, "System.Single", {"System.Single"});
  planetary_provider_class = planetary.get_cls();
  if (update == nullptr || update->has_full_generic_sharing_signature) {
    spdlog::warn("[HavenZoom] BlendFrameProvider.UpdateCameraFrame(UnityEngine.Camera) instance void method "
                 "unavailable or incompatible; skipping hook installation and keeping native zoom range");
    return;
  }
  if (curve_evaluate == nullptr || curve_evaluate->has_full_generic_sharing_signature) {
    spdlog::warn("[HavenZoom] AnimationCurve.Evaluate(System.Single) instance float method unavailable or "
                 "incompatible; skipping hook installation and keeping native zoom range");
    return;
  }
  if (!il2cpp_class_is_assignable_from(orbit.get_cls(), planetary_provider_class)) {
    spdlog::warn("[HavenZoom] PlanetaryOrbitFrameProvider no longer derives from AbstractOrbitFrameProvider; skipping "
                 "hook installation and keeping native zoom range");
    return;
  }
  struct FieldBinding {
    FieldInfo        **destination;
    IL2CppClassHelper *owner;
    const char        *name, *value_type;
    Il2CppClass       *reference_type;
  };
  const FieldBinding fields[] = {
      {&blend_source, &blend, "_sourceFrameProvider", nullptr, frame_provider.get_cls()},
      {&blend_target, &blend, "_targetFrameProvider", nullptr, frame_provider.get_cls()},
      {&blend_curve, &blend, "_blendCurve", nullptr, curve.get_cls()},
      {&blend_minimum, &blend, "_softLimitMin", "System.Single", nullptr},
      {&blend_maximum, &blend, "_softLimitMax", "System.Single", nullptr},
      {&blend_ratio, &blend, "_blendRatio", "System.Single", nullptr},
      {&blend_source_frame, &blend, "_sourceFrame", nullptr, frame.get_cls()},
      {&blend_target_frame, &blend, "_targetFrame", nullptr, frame.get_cls()},
      {&blend_result_frame, &blend, "_blendedCameraFrame", nullptr, editable.get_cls()},
      {&provider_constraint, &orbit, "_constraint", nullptr, orbit_constraint.get_cls()},
      {&provider_radius, &orbit, "_radius", "System.Single", nullptr},
      {&provider_pivot, &orbit, "_orbitPivot", nullptr, target.get_cls()},
      {&provider_look_target, &orbit, "_lookTarget", nullptr, target.get_cls()},
      {&constraint_radius, &orbit_constraint, "_radius", nullptr, radius_class},
      {&radius_enabled, &radius, "_enabled", "System.Boolean", nullptr},
      {&radius_minimum, &radius, "_min", "System.Single", nullptr},
      {&radius_maximum, &radius, "_max", "System.Single", nullptr},
      {&frame_position, &frame, "_position", "UnityEngine.Vector3", nullptr},
      {&frame_rotation, &frame, "_rotation", "UnityEngine.Quaternion", nullptr},
      {&frame_fov, &frame, "_FOV", "System.Single", nullptr},
      {&frame_far_clip, &frame, "_farPlane", "System.Single", nullptr},
      {&frame_orthographic, &frame, "_orthographic", "System.Boolean", nullptr},
  };
  for (const auto &binding : fields) {
    auto       *field   = binding.owner->GetField(binding.name).get_info();
    const char *failure = nullptr;
    if (field == nullptr)
      failure = "field not found";
    else if (il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC)
      failure = "field became static";
    else if (field->type == nullptr || field->type->byref)
      failure = "missing or by-reference field type";
    else if (binding.reference_type != nullptr) {
      if (il2cpp_class_is_valuetype(binding.reference_type)
          || il2cpp_class_from_type(field->type) != binding.reference_type)
        failure = "incompatible reference type";
    } else if (!method_contract::Type(field->type, binding.value_type)) {
      failure = "incompatible value type";
    }
    if (failure != nullptr) {
      auto *actual = field && field->type ? il2cpp_type_get_name(field->type) : nullptr;
      auto *expected =
          binding.reference_type ? il2cpp_type_get_name(il2cpp_class_get_type(binding.reference_type)) : nullptr;
      spdlog::warn(
          "[HavenZoom] {}.{}: {}; expected instance {}, found {}. Skipping hook installation; native zoom retained",
          il2cpp_class_get_name(binding.owner->get_cls()), binding.name, failure,
          expected ? expected : (binding.value_type ? binding.value_type : "<reference type>"),
          actual ? actual : "<unavailable>");
      il2cpp_free(expected);
      il2cpp_free(actual);
      return;
    }
    *binding.destination = field;
  }
  // [patches].havenzoomhooks controls installation independently of haven_zoom.
  if (SPUD_STATIC_DETOUR(update->methodPointer, HavenCamera_UpdateCameraFrame_Hook)) {
    spdlog::info("[HavenZoom] installed planetary blend camera hook (maximum distance {})", Config::Get().haven_zoom);
  } else {
    spdlog::warn("[HavenZoom] camera hook was not installed; keeping native zoom range");
  }
}

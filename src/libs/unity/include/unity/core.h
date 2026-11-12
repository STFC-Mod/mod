#pragma once

// UnityEngine's core types for IL2CPP bindings: math structs, objects and their lifetime, scene hierarchy, camera,
// screen and time, textures and sprites. Nothing here knows about any particular game.

#include <il2cpp/il2cpp.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>

namespace unity
{
struct Vector2 {
  float x, y;
};

struct Vector3 {
  float x, y, z;
};

// Each component times `scale`: the same float multiplies as writing them out.
constexpr Vector2 operator*(Vector2 vector, float scale)
{ return {vector.x * scale, vector.y * scale}; }
constexpr Vector3 operator*(Vector3 vector, float scale)
{ return {vector.x * scale, vector.y * scale, vector.z * scale}; }

struct Quaternion {
  float x, y, z, w;
};

struct Color {
  float r, g, b, a;
};

struct Rect {
  float x, y, width, height;
};

// System.TimeSpan: 100 ns ticks.
struct TimeSpan {
  int64_t ticks;
};

// System.DateTime: 100 ns ticks since 0001-01-01 in the low 62 bits, the Kind in the top two.
struct DateTime {
  uint64_t dateData;

  // Unix seconds, or empty for DateTime.MinValue (unset).
  std::optional<int64_t> UnixSeconds() const
  {
    const auto ticks = static_cast<int64_t>(dateData & 0x3FFFFFFFFFFFFFFFull);
    if (ticks <= 0)
      return std::nullopt;
    return (ticks - 621355968000000000LL) / 10000000LL;
  }
};

// UnityEngine.Object's static members. il2cpp::ObjectClass() is System.Object, a different class.
struct UnityObject {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;
};

// Unity's bool(Object): false for null, and for objects whose native side was destroyed even while a GC handle
// still holds the managed wrapper.
inline bool Alive(Il2CppObject* object)
{ return object && UnityObject::Class::Call<"op_Implicit", bool>(object); }

// The first loaded object of T. Scans every loaded object; keep it off per-frame paths.
template <typename T> T* FindObject()
{
  auto* type = T::Class::TypeObject();
  return type ? static_cast<T*>(UnityObject::Class::Call<"FindObjectOfType", Il2CppObject*>(type)) : nullptr;
}

// Every active loaded T. Scans every loaded object; keep it off per-frame paths.
template <typename T> il2cpp::Array<T*>* FindObjectsOfType()
{
  auto* type = T::Class::TypeObject();
  return type ? UnityObject::Class::Call<"FindObjectsOfType", il2cpp::Array<T*>*>(type) : nullptr;
}

// Every loaded T, active or not, including prefabs and other assets. Scans every loaded object; keep it off
// per-frame paths.
template <typename T> il2cpp::Array<T*>* FindObjectsOfTypeAll()
{
  using Resources = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Resources">;
  auto* type      = T::Class::TypeObject();
  return type ? Resources::Call<"FindObjectsOfTypeAll", il2cpp::Array<T*>*>(type) : nullptr;
}

// Behaviour.isActiveAndEnabled for any MonoBehaviour. The getter resolves once on Behaviour itself, so bindings
// resolved on their runtime class, such as Widget`1, can share it. It goes through the managed getter rather than an
// internal call: Unity 6 registers only get_isActiveAndEnabled_Injected, which takes the native object pointer. False
// when the getter is missing, like any other binding.
inline bool ActiveAndEnabled(il2cpp::Object* behaviour)
{
  using BehaviourClass      = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Behaviour">;
  static const auto* getter = il2cpp::detail::ResolveProperty(BehaviourClass::Raw(), "isActiveAndEnabled",
                                                              il2cpp::detail::SlotOf<bool>(), false, false);
  return il2cpp::detail::Invoke<bool>(getter, behaviour);
}

struct Transform;

struct GameObject : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "GameObject">;

  std::string Name()
  { return il2cpp::ToUtf8(Get<"name", Il2CppString*>()); }
  bool NameIs(std::string_view ascii)
  { return il2cpp::Equals(Get<"name", Il2CppString*>(), ascii); }
  Transform* transform()
  { return Get<"transform", Transform*>(); }
  void SetActive(bool active)
  { Call<"SetActive">(active); }

  // Passing System.Type selects the (Type) overloads over the string and generic ones. A null Type would throw in
  // managed code, so a missing class yields null instead.
  template <typename T> T* GetComponent()
  {
    auto* type = T::Class::TypeObject();
    return type ? Call<"GetComponent", T*>(type) : nullptr;
  }
  // The first active T on this GameObject or below it, depth first. Searches one hierarchy, where FindObject
  // searches every loaded object. This Unity only has the (Type, bool includeInactive) overload.
  template <typename T> T* GetComponentInChildren()
  {
    auto* type = T::Class::TypeObject();
    return type ? Call<"GetComponentInChildren", T*>(type, false) : nullptr;
  }
  template <typename T> T* AddComponent()
  {
    auto* type = T::Class::TypeObject();
    return type ? Call<"AddComponent", T*>(type) : nullptr;
  }
  template <typename T> il2cpp::Array<T*>* GetComponentsInChildren(bool include_inactive)
  {
    auto* type = T::Class::TypeObject();
    return type ? Call<"GetComponentsInChildren", il2cpp::Array<T*>*>(type, include_inactive) : nullptr;
  }

  // A new, active GameObject with only a Transform, at the scene root.
  static GameObject* Create(std::string_view name)
  { return il2cpp::New<GameObject>(il2cpp::NewString(name)); }
  static GameObject* Find(std::string_view path)
  { return Class::Call<"Find", GameObject*>(il2cpp::NewString(path)); }
};

struct Transform : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Transform">;

  int32_t childCount()
  { return Get<"childCount", int32_t>(); }
  Transform* parent()
  { return Get<"parent", Transform*>(); }
  GameObject* gameObject()
  { return Get<"gameObject", GameObject*>(); }
  Vector3 localScale()
  { return Get<"localScale", Vector3>(); }
  void SetLocalScale(Vector3 value)
  { Set<"localScale">(value); }
  void SetLocalEulerAngles(Vector3 value)
  { Set<"localEulerAngles">(value); }

  Transform* GetChild(int32_t index)
  { return Call<"GetChild", Transform*>(index); }
  // The direct children in order, any of which can be null. childCount is read once, so a loop over them must not
  // add or remove children. The count is floored at zero because iota needs bound >= 0.
  auto Children()
  {
    return std::views::iota(0, std::max(childCount(), 0))
           | std::views::transform([this](int32_t i) { return GetChild(i); });
  }
  void SetParent(Transform* parent, bool worldPositionStays)
  { Call<"SetParent">(parent, worldPositionStays); }
  void SetAsFirstSibling()
  { Call<"SetAsFirstSibling">(); }
  // A descendant by slash-separated path below this one, e.g. "Hint/Background".
  Transform* Find(std::string_view path)
  { return Call<"Find", Transform*>(il2cpp::NewString(path)); }

  // This transform or the first descendant, depth first, whose GameObject is called `name`.
  Transform* FindNamed(std::string_view name)
  {
    if (auto* object = gameObject(); object && object->NameIs(name))
      return this;
    for (auto* child : Children())
      if (auto* found = child ? child->FindNamed(name) : nullptr)
        return found;
    return nullptr;
  }

  // The first T on this transform's GameObject or its ancestors, active or not. The walk is bounded so a cycle in
  // a half-destroyed hierarchy cannot hang the game; real UI hierarchies are far shallower.
  template <typename T> T* FindInParents(int32_t limit = 32)
  {
    auto* transform = this;
    for (int32_t depth = 0; transform && depth < limit; ++depth) {
      if (auto* object = transform->gameObject())
        if (auto* found = object->GetComponent<T>())
          return found;
      transform = transform->parent();
    }
    return nullptr;
  }
};

struct RectTransform : Transform {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "RectTransform">;

  Vector2 anchorMin()
  { return Get<"anchorMin", Vector2>(); }
  Vector2 anchorMax()
  { return Get<"anchorMax", Vector2>(); }
  Vector2 offsetMin()
  { return Get<"offsetMin", Vector2>(); }
  Vector2 offsetMax()
  { return Get<"offsetMax", Vector2>(); }
  void SetAnchorMin(Vector2 value)
  { Set<"anchorMin">(value); }
  void SetAnchorMax(Vector2 value)
  { Set<"anchorMax">(value); }
  void SetPivot(Vector2 value)
  { Set<"pivot">(value); }
  void SetOffsetMin(Vector2 value)
  { Set<"offsetMin">(value); }
  void SetOffsetMax(Vector2 value)
  { Set<"offsetMax">(value); }

  void Place(Vector2 anchor_min, Vector2 anchor_max, Vector2 pivot, Vector2 size, Vector2 position)
  {
    Set<"anchorMin">(anchor_min);
    Set<"anchorMax">(anchor_max);
    Set<"pivot">(pivot);
    Set<"sizeDelta">(size);
    Set<"anchoredPosition">(position);
  }
};

// UnityEngine.Component, the base of every MonoBehaviour. Use As<Component>() on other bindings.
struct Component : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Component">;

  Transform* transform()
  { return Get<"transform", Transform*>(); }
  GameObject* gameObject()
  { return Get<"gameObject", GameObject*>(); }

  // The GameObject of this component's parent transform, or null at the root.
  GameObject* ParentObject()
  {
    auto* own    = transform();
    auto* parent = own ? own->parent() : nullptr;
    return parent ? parent->gameObject() : nullptr;
  }
};

// A copy of `original` (a component copies its whole GameObject) under `parent`. Uses the non-generic
// Instantiate(Object, Transform, bool): some clients expose the two-argument overload only as Instantiate<T>, which
// would need generic inflation.
template <typename T> T* Instantiate(T* original, Transform* parent, bool world_position_stays = false)
{
  return static_cast<T*>(UnityObject::Class::Call<"Instantiate", il2cpp::Object*(il2cpp::Object*, Transform*, bool)>(
      original, parent, world_position_stays));
}

// The base of every MonoBehaviour. Bindings for MonoBehaviours derive from it and name their own Class; one without
// a Class would bind to Behaviour's.
struct Behaviour : Component {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Behaviour">;

  void SetEnabled(bool value)
  { Set<"enabled">(value); }
  bool isActiveAndEnabled()
  { return ActiveAndEnabled(this); }
};

struct Camera : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Camera">;

  enum class ClearFlags : int32_t {
    Skybox     = 1,
    SolidColor = 2,
    Depth      = 3,
    Nothing    = 4,
  };

  void SetFarClipPlane(float value)
  { Set<"farClipPlane">(value); }
  void SetClearFlags(ClearFlags value)
  { Set<"clearFlags">(value); }
  void SetBackgroundColor(Color value)
  { Set<"backgroundColor">(value); }
};

struct AnimationCurve : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "AnimationCurve">;

  float Evaluate(float value)
  { return Call<"Evaluate", float>(value); }
};

struct Shader : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Shader">;

  std::string Name()
  { return il2cpp::ToUtf8(Get<"name", Il2CppString*>()); }
};

struct Material : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Material">;

  std::string Name()
  { return il2cpp::ToUtf8(Get<"name", Il2CppString*>()); }
  Shader* shader()
  { return Get<"shader", Shader*>(); }
  float GetFloat(Il2CppString* name)
  { return Call<"GetFloat", float>(name); }
  void SetFloat(Il2CppString* name, float value)
  { Call<"SetFloat">(name, value); }
};

struct Renderer : Component {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Renderer">;

  il2cpp::Array<Material*>* sharedMaterials()
  { return Get<"sharedMaterials", il2cpp::Array<Material*>*>(); }
};

struct Canvas : Behaviour {
  using Class = il2cpp::Class<"UnityEngine.UIModule", "UnityEngine", "Canvas">;
};

struct Screen {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Screen">;

  static int32_t width()
  { return Class::Get<"width", int32_t>(); }
  static int32_t height()
  { return Class::Get<"height", int32_t>(); }
};

// Only hooked.
using Cursor = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Cursor">;

// UnityEngine.Time through its internal calls, which skip the managed wrappers.
struct Time {
  // -1 when Unity lacks the call.
  static int32_t frameCount()
  {
    static const il2cpp::ICall<int32_t()> call{"UnityEngine.Time::get_frameCount()"};
    return call ? call() : -1;
  }
  static float deltaTime()
  {
    static const il2cpp::ICall<float()> call{"UnityEngine.Time::get_deltaTime()"};
    return call();
  }
};

struct Texture2D : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Texture2D">;

  int32_t width()
  { return Get<"width", int32_t>(); }
  int32_t height()
  { return Get<"height", int32_t>(); }

  // A texture decoded from PNG or JPG bytes, or null.
  static Texture2D* Decode(const uint8_t* data, size_t size)
  {
    using ImageConversion = il2cpp::Class<"UnityEngine.ImageConversionModule", "UnityEngine", "ImageConversion">;
    auto* bytes = data && size ? il2cpp::Array<uint8_t>::New(il2cpp::PrimitiveClass<uint8_t>(), size) : nullptr;
    if (!bytes)
      return nullptr;
    std::memcpy(bytes->data(), data, size);
    // Texture2D(width, height, TextureFormat.RGBA32, mipChain); LoadImage resizes it.
    auto* texture = il2cpp::New<Texture2D>(int32_t{2}, int32_t{2}, int32_t{4}, false);
    return texture && ImageConversion::Call<"LoadImage", bool>(texture, bytes, false) ? texture : nullptr;
  }
};

struct Sprite : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Sprite">;

  float pixelsPerUnit()
  { return Get<"pixelsPerUnit", float>(); }

  static Sprite* Create(Texture2D* texture, Rect rect, Vector2 pivot)
  { return Class::Call<"Create", Sprite*>(texture, rect, pivot); }
};
} // namespace unity

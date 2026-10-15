#pragma once

// UnityEngine's core types for IL2CPP bindings: math structs, objects and their lifetime, scene hierarchy, camera,
// screen and time, textures and sprites. Nothing here knows about any particular game.

#include <il2cpp/il2cpp.h>

#include <cstdint>
#include <cstring>
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

using ObjectClass = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;

// Unity's bool(Object): false for null, and for objects whose native side was destroyed even while a GC handle
// still holds the managed wrapper.
inline bool Alive(Il2CppObject* object)
{ return object && ObjectClass::Call<"op_Implicit", bool>(object); }

inline void Destroy(Il2CppObject* object)
{ ObjectClass::Call<"Destroy">(object); }

// The first loaded object of T. Scans every loaded object; keep it off per-frame paths.
template <typename T> T* FindObject()
{
  auto* type = T::Class::TypeObject();
  return type ? static_cast<T*>(ObjectClass::Call<"FindObjectOfType", Il2CppObject*>(type)) : nullptr;
}

// Every active loaded T. Scans every loaded object; keep it off per-frame paths.
template <typename T> il2cpp::Array<T*>* FindObjectsOfType()
{
  auto* type = T::Class::TypeObject();
  return type ? ObjectClass::Call<"FindObjectsOfType", il2cpp::Array<T*>*>(type) : nullptr;
}

// Every loaded T, active or not, including prefabs and other assets. Scans every loaded object; keep it off
// per-frame paths.
template <typename T> il2cpp::Array<T*>* FindObjectsOfTypeAll()
{
  using Resources = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Resources">;
  auto* type      = T::Class::TypeObject();
  return type ? Resources::Call<"FindObjectsOfTypeAll", il2cpp::Array<T*>*>(type) : nullptr;
}

// Behaviour.isActiveAndEnabled through its internal call, for bindings whose managed property may have drifted:
// without the call everything counts as active.
inline bool ActiveAndEnabled(il2cpp::Object* behaviour)
{
  static const il2cpp::ICall<bool(il2cpp::Object*)> call{"UnityEngine.Behaviour::get_isActiveAndEnabled()"};
  return !call || call(behaviour);
}

struct Transform;

struct GameObject : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "GameObject">;

  static GameObject* Find(std::string_view path)
  { return Class::Call<"Find", GameObject*>(il2cpp::NewString(path)); }

  template <typename T> il2cpp::Array<T*>* GetComponentsInChildren(bool include_inactive)
  {
    auto* type = T::Class::TypeObject();
    return type ? Call<"GetComponentsInChildren", il2cpp::Array<T*>*>(type, include_inactive) : nullptr;
  }

  // A new, active GameObject with only a Transform, at the scene root.
  static GameObject* Create(std::string_view name)
  { return il2cpp::New<GameObject>(il2cpp::NewString(name)); }

  std::string Name()
  { return il2cpp::ToUtf8(Get<"name", Il2CppString*>()); }
  bool NameIs(std::string_view ascii)
  { return il2cpp::Equals(Get<"name", Il2CppString*>(), ascii); }
  Transform* transform()
  { return Get<"transform", Transform*>(); }
  void SetActive(bool active)
  { Call<"SetActive">(active); }
  // Whether it and every parent are active.
  bool activeInHierarchy()
  { return Get<"activeInHierarchy", bool>(); }

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
};

struct Transform : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Transform">;

  int32_t childCount()
  { return Get<"childCount", int32_t>(); }
  Transform* GetChild(int32_t index)
  { return Call<"GetChild", Transform*>(index); }
  Transform* parent()
  { return Get<"parent", Transform*>(); }
  // The topmost transform of this hierarchy; itself when it has no parent.
  Transform* root()
  { return Get<"root", Transform*>(); }
  GameObject* gameObject()
  { return Get<"gameObject", GameObject*>(); }
  Vector3 localScale()
  { return Get<"localScale", Vector3>(); }
  void SetLocalScale(Vector3 value)
  { Set<"localScale">(value); }
  void SetLocalEulerAngles(Vector3 value)
  { Set<"localEulerAngles">(value); }
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
    for (int32_t i = 0; i < childCount(); ++i)
      if (auto* child = GetChild(i); auto* found = child ? child->FindNamed(name) : nullptr)
        return found;
    return nullptr;
  }

  // The direct child whose GameObject is called `name`.
  Transform* ChildNamed(std::string_view name)
  {
    for (int32_t i = 0; i < childCount(); ++i)
      if (auto* child = GetChild(i); child && child->gameObject() && child->gameObject()->NameIs(name))
        return child;
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
};

// A copy of `original` (a component copies its whole GameObject) under `parent`. Uses the non-generic
// Instantiate(Object, Transform, bool): some clients expose the two-argument overload only as Instantiate<T>, which
// would need generic inflation.
template <typename T> T* Instantiate(T* original, Transform* parent, bool world_position_stays = false)
{
  return static_cast<T*>(ObjectClass::Call<"Instantiate", il2cpp::Object*(il2cpp::Object*, Transform*, bool)>(
      original, parent, world_position_stays));
}

struct Behaviour : Component {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Behaviour">;

  void SetEnabled(bool value)
  { Set<"enabled">(value); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
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
  bool HasProperty(Il2CppString* name)
  { return Call<"HasProperty", bool>(name); }
  float GetFloat(Il2CppString* name)
  { return Call<"GetFloat", float>(name); }
  void SetFloat(Il2CppString* name, float value)
  { Call<"SetFloat">(name, value); }
};

struct Renderer : Component {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Renderer">;
  il2cpp::Array<Material*>* SharedMaterials()
  { return Get<"sharedMaterials", il2cpp::Array<Material*>*>(); }
};

struct Canvas : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UIModule", "UnityEngine", "Canvas">;
};

struct Screen {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Screen">;

  static int32_t width()
  { return Class::Get<"width", int32_t>(); }
  static int32_t height()
  { return Class::Get<"height", int32_t>(); }
};

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

  static Sprite* Create(Texture2D* texture, Rect rect, Vector2 pivot)
  { return Class::Call<"Create", Sprite*>(texture, rect, pivot); }

  float pixelsPerUnit()
  { return Get<"pixelsPerUnit", float>(); }
};
} // namespace unity

#pragma once

// Unity engine types the bindings share: math structs, scene objects, input.

#include "str_utils.h"
#include <il2cpp/il2cpp.h>

#include <compare>
#include <cstdint>
#include <string>

struct Vector2 {
  float x, y;
};

#pragma pack(push, 1)
struct vec2 {
  float x, y;

  friend auto operator<=>(const vec2&, const vec2&) = default;
};
#pragma pack(pop)

struct Vector3 {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Vector3">;

  float x;
  float y;
  float z;

  static Vector3 zero()
  { return Class::Field<"zeroVector", Vector3>(); }
};

#pragma pack(push, 1)
struct color {
  float r, g, b, a;
};
#pragma pack(pop)

// UnityEngine.Object's statics. Alive is Unity's bool(Object): false for null, and for objects whose native side was
// destroyed even while a GC handle still holds the managed wrapper.
struct UnityObject {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;

  static bool Alive(Il2CppObject* object)
  { return object && Class::Call<"op_Implicit", bool>(object); }
  static void Destroy(Il2CppObject* object)
  { Class::Call<"Destroy">(object); }
  // Scans every loaded object; keep it off per-frame paths.
  template <typename T> static T* Find()
  {
    auto* type = T::Class::TypeObject();
    return type ? static_cast<T*>(Class::Call<"FindObjectOfType", Il2CppObject*>(type)) : nullptr;
  }
};

struct GameObject : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "GameObject">;

  std::string Name()
  {
    auto* name = Get<"name", Il2CppString*>();
    return name ? to_string(name) : std::string{};
  }
  bool activeInHierarchy()
  { return Get<"activeInHierarchy", bool>(); }
  void SetActive(bool active)
  { Call<"SetActive">(active); }

  // Passing System.Type selects the (Type) overloads over the string and generic ones. A null
  // Type would throw in managed code, so a missing class yields null instead.
  template <typename T> T* GetComponent()
  {
    auto* type = T::Class::TypeObject();
    return type ? Call<"GetComponent", T*>(type) : nullptr;
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
  // Finds a descendant by slash-separated path, e.g. "Hint/Background".
  Transform* Find(Il2CppString* path)
  { return Call<"Find", Transform*>(path); }
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
  void SetOffsetMin(Vector2 value)
  { Set<"offsetMin">(value); }
  void SetOffsetMax(Vector2 value)
  { Set<"offsetMax">(value); }
  void SetAnchorMin(Vector2 value)
  { Set<"anchorMin">(value); }
  void SetAnchorMax(Vector2 value)
  { Set<"anchorMax">(value); }
  void SetPivot(Vector2 value)
  { Set<"pivot">(value); }
  void SetSizeDelta(Vector2 value)
  { Set<"sizeDelta">(value); }
  void SetAnchoredPosition(Vector2 value)
  { Set<"anchoredPosition">(value); }

  void Place(Vector2 anchor_min, Vector2 anchor_max, Vector2 pivot, Vector2 size, Vector2 position)
  {
    SetAnchorMin(anchor_min);
    SetAnchorMax(anchor_max);
    SetPivot(pivot);
    SetSizeDelta(size);
    SetAnchoredPosition(position);
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

struct Behaviour : Component {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Behaviour">;

  void SetEnabled(bool value)
  { Set<"enabled">(value); }
};

struct TMP_Text : Component {
  using Class = il2cpp::Class<"Unity.TextMeshPro", "TMPro", "TMP_Text">;

  // TMP_FontAsset
  il2cpp::Object* font()
  { return Get<"font", il2cpp::Object*>(); }
  void SetFont(il2cpp::Object* value)
  { Set<"font">(value); }
  void SetFontSize(float value)
  { Set<"fontSize">(value); }
  // TextAlignmentOptions
  void SetAlignment(int32_t value)
  { Set<"alignment">(value); }
  void SetColor(color value)
  { Set<"color">(value); }
  void SetRaycastTarget(bool value)
  { Set<"raycastTarget">(value); }
  void SetEnableWordWrapping(bool value)
  { Set<"enableWordWrapping">(value); }
  void SetRichText(bool value)
  { Set<"richText">(value); }
  // TextOverflowModes
  void SetOverflowMode(int32_t value)
  { Set<"overflowMode">(value); }
  void SetEnableAutoSizing(bool value)
  { Set<"enableAutoSizing">(value); }
  void SetText(Il2CppString* value)
  { Set<"text">(value); }
};

struct TextMeshProUGUI : TMP_Text {
  using Class = il2cpp::Class<"Unity.TextMeshPro", "TMPro", "TextMeshProUGUI">;
};

struct Camera : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Camera">;

  float farClipPlane()
  { return Get<"farClipPlane", float>(); }
  void SetFarClipPlane(float value)
  { Set<"farClipPlane">(value); }
  float nearClipPlane()
  { return Get<"nearClipPlane", float>(); }
  // UnityEngine.CameraClearFlags
  int32_t clearFlags()
  { return Get<"clearFlags", int32_t>(); }
  void SetClearFlags(int32_t value)
  { Set<"clearFlags">(value); }
  color backgroundColor()
  { return Get<"backgroundColor", color>(); }
  void SetBackgroundColor(color value)
  { Set<"backgroundColor">(value); }
};

struct Canvas : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UIModule", "UnityEngine", "Canvas">;

  float scaleFactor()
  { return Get<"scaleFactor", float>(); }
  void SetScaleFactor(float value)
  { Set<"scaleFactor">(value); }
};

struct CanvasScaler : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.UI", "CanvasScaler">;

  // Writes Canvas.scaleFactor directly. The scaleFactor property is only applied on the scaler's next Update.
  void SetScaleFactor(float value)
  { Call<"SetScaleFactor">(value); }

  float scaleFactor()
  { return Get<"scaleFactor", float>(); }
  // Named after the managed setter because SetScaleFactor is the managed method above.
  void set_scaleFactor(float value)
  { Set<"scaleFactor">(value); }
  vec2 referenceResolution()
  { return Get<"referenceResolution", vec2>(); }
};

struct Sprite : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Sprite">;

  float pixelsPerUnit()
  { return Get<"pixelsPerUnit", float>(); }
};

// UnityEngine.UI.Image; color and raycastTarget come from Graphic.
struct Image : Component {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.UI", "Image">;

  enum class Type : int32_t {
    Simple = 0,
    Sliced = 1,
    Tiled  = 2,
    Filled = 3,
  };

  Sprite* sprite()
  { return Get<"sprite", Sprite*>(); }
  void SetSprite(Sprite* value)
  { Set<"sprite">(value); }
  Sprite* overrideSprite()
  { return Get<"overrideSprite", Sprite*>(); }
  void SetOverrideSprite(Sprite* value)
  { Set<"overrideSprite">(value); }
  void SetType(Type value)
  { Set<"type">(value); }
  void SetPreserveAspect(bool value)
  { Set<"preserveAspect">(value); }
  void SetPixelsPerUnitMultiplier(float value)
  { Set<"pixelsPerUnitMultiplier">(value); }
  void SetColor(color value)
  { Set<"color">(value); }
  void SetRaycastTarget(bool value)
  { Set<"raycastTarget">(value); }
  void SetVerticesDirty()
  { Call<"SetVerticesDirty">(); }
};

struct EventSystem : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.EventSystems", "EventSystem">;

  static EventSystem* current()
  { return Class::Get<"current", EventSystem*>(); }

  GameObject* currentSelectedGameObject()
  { return Get<"currentSelectedGameObject", GameObject*>(); }
  void SetSelectedGameObject(GameObject* object)
  { Call<"SetSelectedGameObject">(object); }
};

struct TMP_InputField : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.TextMeshPro", "TMPro", "TMP_InputField">;

  bool isFocused()
  { return Get<"isFocused", bool>(); }

  void ActivateInputField()
  { Call<"ActivateInputField">(); }
  void DeactivateInputField(bool clearSelection = false)
  { Call<"DeactivateInputField">(clearSelection); }
  void SendOnFocusLost()
  { Call<"SendOnFocusLost">(); }
  void SendOnFocus()
  { Call<"SendOnFocus">(); }
};

enum class TouchPhase : int32_t {
  Began      = 0,
  Moved      = 1,
  Stationary = 2,
  Ended      = 3,
  Canceled   = 4,
};

struct TKTouch : il2cpp::Object {
  using Class = il2cpp::Class<"TouchKit", "", "TKTouch">;

  TouchPhase phase()
  { return Field<"phase", TouchPhase>(); }
  void SetPhase(TouchPhase phase)
  { SetField<"phase">(phase); }
};

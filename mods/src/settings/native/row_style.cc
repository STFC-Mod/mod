#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "row_style.h"
#include "prime/Vector3.h"
#include "value_widget_record.h"
#include <cstring>

namespace mod_settings::native
{
void RestoreTint(RowTint& tint)
{
  try {
    if (auto* image = Target(tint.image)) {
      void* args[] = {&tint.before};
      Call(image, "set_color", 1, args);
    }
  } catch (...) {
    Warn("settings row tint restoration unavailable");
  }
  Free(tint.image);
}
// The build261 settings prefabs put backgrounds on a direct BG child (category
// rows use Background). Do not search arbitrary descendants such as a checkbox.
Il2CppObject* RowImage(Il2CppObject* widget, const char* child)
{
  static auto images  = il2cpp_get_class_helper("UnityEngine.UI", "UnityEngine.UI", "Image");
  static auto objects = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "GameObject");
  static auto get     = objects.GetMethodInfoSpecial("GetComponent", [](auto count, auto params) {
    return count == 1 && Type(params[0], IL2CPP_TYPE_CLASS)
           && std::strcmp(il2cpp_class_get_name(il2cpp_class_from_type(params[0])), "Type") == 0;
  });
  Root        transform(Call(widget, "get_transform"));
  Root        name(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(child)));
  void*       findArgs[] = {name.get()};
  Root        background(Call(transform.get(), "Find", 1, findArgs));
  if (!background.get())
    return nullptr;
  Root  object(Call(background.get(), "get_gameObject"));
  void* args[] = {images.GetType()};
  return Invoke(get, object.get(), args);
}
void TintImage(RowTint& tint, Il2CppObject* image, color value, bool multiply)
{
  RestoreTint(tint);
  if (!image)
    return;
  static auto colors = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Color");
  Root        boxed(Call(image, "get_color"));
  const auto* set = il2cpp_class_get_method_from_name(image->klass, "set_color", 1);
  if (!boxed.get() || boxed.get()->klass != colors.get_cls() || !Instance(set, 1, IL2CPP_TYPE_VOID)
      || set->parameters[0]->byref || il2cpp_class_from_type(set->parameters[0]) != colors.get_cls())
    return;
  tint.before = *static_cast<color*>(il2cpp_object_unbox(boxed.get()));
  tint.image  = il2cpp_gchandle_new_weakref(image, false);
  if (!tint.image)
    return;
  if (multiply) {
    value.r *= tint.before.r;
    value.g *= tint.before.g;
    value.b *= tint.before.b;
  }
  value.a      = tint.before.a;
  void* args[] = {&value};
  Invoke(set, image, args);
}
void TintRow(RowTint& tint, Il2CppObject* widget, color multiplier)
{
  try {
    Root image(RowImage(widget));
    TintImage(tint, image.get(), multiplier);
  } catch (...) {
    RestoreTint(tint);
    Warn("settings row tint unavailable");
  }
}
// Reuse sprites already rendered by these native settings widgets. Weak handles
// do not keep a scene/bundle alive. No asset loading, animation sampling or polling.
Il2CppGCHandle normalChoiceSprite = nullptr, pressedChoiceSprite = nullptr;
void           RememberChoiceSprite(Il2CppObject* background)
{
  if (!background || (Target(normalChoiceSprite) && Target(pressedChoiceSprite)))
    return;
  Root sprite(Call(background, "get_sprite"));
  if (!sprite.get())
    return;
  Root  name(Call(sprite.get(), "get_name"));
  auto& handle = Equals(name.get(), "SelectedBG_raw") ? pressedChoiceSprite : normalChoiceSprite;
  if (!Target(handle)) {
    Free(handle);
    handle = il2cpp_gchandle_new_weakref(sprite.get(), false);
  }
}

bool HasPressedChoiceSprite()
{ return Target(pressedChoiceSprite) != nullptr; }
void ClearChoiceStyle(ValueWidget& view)
{
  RestoreTint(view.checkTint);
  try {
    if (view.choiceStyled) {
      if (auto* background = Target(view.choiceBackground)) {
        Root  before(Target(view.choiceOverrideBefore));
        void* args[] = {before.get()};
        Call(background, "set_overrideSprite", 1, args);
      }
    }
  } catch (...) {
    Warn("settings selection style restoration unavailable");
  }
  Free(view.choiceBackground);
  Free(view.choiceOverrideBefore);
  Free(view.choiceCheck);
  view.choiceStyled = view.pressed = false;
}
void StyleChoice(ValueWidget& view)
{
  auto* widget = Target(view.widget);
  if (!widget || !view.state || !WidgetMeta(widget).selection)
    return;
  if (!view.choiceStyled) {
    Root background(RowImage(widget));
    Root check(RowImage(widget, "Arrow"));
    if (!background.get() || !check.get())
      return;
    RememberChoiceSprite(background.get());
    if (!Target(normalChoiceSprite))
      return;
    Root before(ReadField(background.get(), Field(background.get()->klass, "m_OverrideSprite")));
    view.choiceBackground     = il2cpp_gchandle_new_weakref(background.get(), false);
    view.choiceCheck          = il2cpp_gchandle_new_weakref(check.get(), false);
    view.choiceOverrideBefore = before.get() ? il2cpp_gchandle_new_weakref(before.get(), false) : nullptr;
    if (!view.choiceBackground || !view.choiceCheck || (before.get() && !view.choiceOverrideBefore))
      throw std::runtime_error("settings selection style roots");
    view.choiceStyled = true;
  }
  Root background(Target(view.choiceBackground));
  // Selection animation still updates sprite, geometry and the actual checkmark.
  // Image.overrideSprite changes only the drawn background, so the native isOn
  // animation can keep running. Pointer events provide transient pressed feedback.
  RememberChoiceSprite(background.get());
  Root sprite(Target(view.pressed ? pressedChoiceSprite : normalChoiceSprite));
  if (!sprite.get())
    return;
  void* args[] = {sprite.get()};
  Call(background.get(), "set_overrideSprite", 1, args);
  Root check(Target(view.choiceCheck));
  TintImage(view.checkTint, check.get(), view.pressed ? color{0.22f, 0.22f, 0.22f, 1} : color{0.88f, 0.95f, 0.97f, 1},
            false);
  std::string text = view.state->presentation_label();
  if (view.state->value().value_or(false))
    text = "<b>" + text + "</b>";
  text = std::string(view.pressed ? "<color=#383838>" : "<color=#E1F2F7>") + text + "</color>";
  Root  label(Target(view.label));
  Root  message(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(text.c_str())));
  void* textArgs[] = {message.get()};
  Call(label.get(), "OverrideLocalizedText", 1, textArgs);
  view.overridden = true;
}

void TryStyleChoice(ValueWidget& view)
{
  try {
    StyleChoice(view);
  } catch (...) {
    ClearChoiceStyle(view);
    Warn("settings selection presentation unavailable");
  }
}

struct RowTextOverride {
  Il2CppGCHandle owner = nullptr, label = nullptr;
  RowTint        tint;
  Il2CppGCHandle arrow = nullptr;
  Vector3        arrowBefore{};
};
std::vector<RowTextOverride> textOverrides;
void                         ClearRowText(Il2CppObject* owner)
{
  for (auto it = textOverrides.begin(); it != textOverrides.end();) {
    auto* live = Target(it->owner);
    if (live && live != owner) {
      ++it;
      continue;
    }
    RestoreTint(it->tint);
    try {
      if (auto* arrow = Target(it->arrow)) {
        void* args[] = {&it->arrowBefore};
        Call(arrow, "set_localEulerAngles", 1, args);
      }
    } catch (...) {
      Warn("settings arrow rotation restoration unavailable");
    }
    try {
      if (auto* label = Target(it->label))
        Call(label, "ClearTextOverride");
    } catch (...) {
      Warn("settings label restoration unavailable");
    }
    Free(it->owner);
    Free(it->label);
    Free(it->arrow);
    it = textOverrides.erase(it);
  }
}
void SetRowText(Il2CppObject* owner, Il2CppObject* label, const std::string& text, bool heading,
                std::optional<bool> expanded)
{
  if (!owner || !label)
    throw std::runtime_error("settings text missing");
  ClearRowText(owner);
  RowTextOverride record{il2cpp_gchandle_new_weakref(owner, false), il2cpp_gchandle_new_weakref(label, false)};
  try {
    if (!record.owner || !record.label)
      throw std::runtime_error("settings text weak root");
    if (heading) {
      Root background(RowImage(owner, expanded ? "Background" : "BG"));
      TintImage(record.tint, background.get(), {0.35f, 0.50f, 0.56f, 1.0f});
    }
    if (expanded) {
      Root image(RowImage(owner, "Arrow"));
      if (image.get()) {
        Root        arrow(Call(image.get(), "get_transform"));
        Root        rotation(Call(arrow.get(), "get_localEulerAngles"));
        auto        vectors = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Vector3");
        const auto* set     = il2cpp_class_get_method_from_name(arrow.get()->klass, "set_localEulerAngles", 1);
        if (rotation.get() && rotation.get()->klass == vectors.get_cls() && Instance(set, 1, IL2CPP_TYPE_VOID)
            && !set->parameters[0]->byref && il2cpp_class_from_type(set->parameters[0]) == vectors.get_cls()) {
          record.arrowBefore = *static_cast<Vector3*>(il2cpp_object_unbox(rotation.get()));
          record.arrow       = il2cpp_gchandle_new_weakref(arrow.get(), false);
          if (record.arrow) {
            auto value = record.arrowBefore;
            if (*expanded)
              value.z -= 90.0f;
            void* args[] = {&value};
            Invoke(set, arrow.get(), args);
          }
        }
      }
    }
    textOverrides.push_back(record);
  } catch (...) {
    RestoreTint(record.tint);
    if (auto* arrow = Target(record.arrow)) {
      try {
        void* args[] = {&record.arrowBefore};
        Call(arrow, "set_localEulerAngles", 1, args);
      } catch (...) {
        Warn("settings arrow rollback unavailable");
      }
    }
    Free(record.arrow);
    Free(record.owner);
    Free(record.label);
    throw;
  }
  Root  message(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(text.c_str())));
  void* args[] = {message.get()};
  Call(label, "OverrideLocalizedText", 1, args);
}

} // namespace mod_settings::native
#endif

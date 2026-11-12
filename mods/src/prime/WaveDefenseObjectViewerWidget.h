#pragma once

#include "GenericButtonWidget.h"
#include "ObjectViewerBaseWidget.h"

struct WaveDefenseObjectViewerWidget : public ObjectViewerBaseWidget<WaveDefenseObjectViewerWidget> {
public:
  static Il2CppArraySize* FindActiveViewers()
  {
    static auto object_class = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Object");
    static auto find = object_class.GetMethod<Il2CppArraySize*(void*)>("FindObjectsOfType", 1);
    auto& viewer_class = get_class_helper();
    return find && viewer_class.get_cls() ? find(viewer_class.GetType()) : nullptr;
  }

  void PressPrimaryButton()
  {
    static auto field = get_class_helper().GetField("_primaryButton");
    if (!field.get_info()) {
      return;
    }
    auto widget = *(GenericButtonWidget**)((char*)this + field.offset());
    if (!widget || !widget->isActiveAndEnabled) {
      return;
    }
    auto context = widget->Context;
    auto listener = widget->SemaphoreListener;
    if (context && context->Interactable && listener) {
      if (auto button = listener->TheButton) {
        button->Press();
      }
    }
  }

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.ObjectViewer", "WaveDefenseObjectViewerWidget");
    return class_helper;
  }
};

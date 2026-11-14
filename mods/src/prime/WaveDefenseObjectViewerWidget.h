#pragma once

#include "GenericButtonWidget.h"
#include "ObjectViewerBaseWidget.h"

struct WaveDefenseObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "WaveDefenseObjectViewerWidget">;

  // Every active viewer of this class in the loaded scenes.
  static il2cpp::Array<WaveDefenseObjectViewerWidget*>* FindActiveViewers()
  {
    using UnityObject = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;
    auto* type        = Class::TypeObject();
    return type ? UnityObject::Call<"FindObjectsOfType", il2cpp::Array<WaveDefenseObjectViewerWidget*>*>(type)
                : nullptr;
  }

  GenericButtonWidget* primaryButton()
  { return Field<"_primaryButton", GenericButtonWidget*>(); }

  void PressPrimaryButton()
  {
    auto* widget = primaryButton();
    if (!widget || !widget->isActiveAndEnabled()) {
      return;
    }
    auto* context  = widget->Context();
    auto* listener = widget->SemaphoreListener();
    if (context && context->Interactable() && listener) {
      if (auto* button = listener->TheButton()) {
        button->Press();
      }
    }
  }
};

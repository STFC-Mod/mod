#pragma once

#include <il2cpp/il2cpp_helper.h>

#include "ObjectViewerBaseWidget.h"

struct StarNodeObjectViewerWidget : public ObjectViewerBaseWidget<StarNodeObjectViewerWidget> {
public:
  void OnViewButtonActivation()
  {
    static auto OnViewButtonActivation =
        get_class_helper().GetMethod<void(StarNodeObjectViewerWidget*)>("OnViewButtonActivation");
    OnViewButtonActivation(this);
  }
  void InitiateWarp()
  {
    static auto InitiateWarp = get_class_helper().GetMethod<void(StarNodeObjectViewerWidget*)>("InitiateWarp");
    InitiateWarp(this);
  }

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.ObjectViewer", "StarNodeObjectViewerWidget");
    return class_helper;
  }

private:
  friend class ObjectFinder<StarNodeObjectViewerWidget>;
  friend class ObjectViewerBaseWidget<StarNodeObjectViewerWidget>;
};

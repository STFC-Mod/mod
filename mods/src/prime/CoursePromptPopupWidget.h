#pragma once

#include "CourseData.h"
#include "GenericButtonWidget.h"
#include "ViewController.h"
#include "Widget.h"

#include <il2cpp/il2cpp.h>

struct CoursePromptPopupDataContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "CoursePromptPopupDataContext">;

  bool HasInstantWarp()
  { return Field<"HasInstantWarp", bool>(); }
  CourseData* GetCourseData()
  { return Field<"CourseData", CourseData*>(); }
};

struct CoursePromptPopupWidget : Widget<CoursePromptPopupDataContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "CoursePromptPopupWidget">;

  GenericButtonWidget* LeftButton()
  { return Field<"_leftButton", GenericButtonWidget*>(); }
  GenericButtonWidget* RightButton()
  { return Field<"_rightButton", GenericButtonWidget*>(); }
  void InitiateRegularWarp()
  { Call<"InitiateRegularWarp">(); }
  void OnInstantWarpButtonClick()
  { Call<"OnInstantWarpButtonClick">(); }
};

struct CoursePromptPopupViewController : ViewController<CoursePromptPopupDataContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "CoursePromptPopupViewController">;

  CoursePromptPopupWidget* PopupWidget()
  { return Field<"_coursePromptPopupWidget", CoursePromptPopupWidget*>(); }
};

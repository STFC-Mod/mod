#pragma once

#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "interop.h"
#include "settings/page_catalog.h"

namespace mod_settings::native
{
struct ValueWidgetMetadata {
  bool selection, slider;
  explicit ValueWidgetMetadata(bool selection = false, bool slider = false)
      : selection(selection)
      , slider(slider)
  {
  }
  IL2CppClassHelper director =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "SettingsSectionDirector");
  IL2CppClassHelper widget  = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings",
                                                      slider      ? "SliderOptionWidget"
                                                      : selection ? "SelectionItemOptionWidget"
                                                                  : "ToggleOptionWidget");
  IL2CppClassHelper context = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "SettingsContext");
  IL2CppClassHelper row     = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings",
                                                      slider      ? "SliderOptionContext"
                                                      : selection ? "SelectionItemOptionContext"
                                                                  : "ToggleOptionContext");
  IL2CppClassHelper prefs =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PersistentPrefs", "PersistentPrefsManager");
  const MethodInfo* addGeneral  = director.GetMethodInfo("AddGeneralSettings", 1);
  const MethodInfo* addToggle   = context.GetMethodInfo("AddToggle", 4);
  const MethodInfo* refresh     = widget.GetMethodInfo("SetWidgetData", 0);
  const MethodInfo* changed     = widget.GetMethodInfo(slider ? "OnSliderValueChanged" : "OnToggleValueChanged", 1);
  const MethodInfo* valueLabel  = slider ? widget.GetMethodInfo("UpdateValueLabel", 1) : nullptr;
  const MethodInfo* release     = widget.GetMethodInfo("OnAboutToReleaseContext", 0);
  const MethodInfo* reload      = prefs.GetMethodInfo("RegisterEvents", 0);
  const MethodInfo* session     = prefs.GetMethodInfo("GameSessionStartedEventHandler", 0);
  const MethodInfo* load        = prefs.GetMethodInfo("LoadPersistentPrefsFromCloud", 0);
  const MethodInfo* getContext  = widget.GetMethodInfo("get_Context", 0);
  const MethodInfo* querySetter = row.GetMethodInfo("set_QueryOptionState", 1);
  FieldInfo*        queryField  = Field(row.get_cls(), "<QueryOptionState>k__BackingField");
  FieldInfo*        labelField  = Field(widget.get_cls(), "_label");
  FieldInfo*        toggleField = Field(widget.get_cls(), slider ? "_slider" : "_toggle");
  FieldInfo*        stateField  = Field(widget.get_cls(), slider ? "_valueLabel" : "_toggleStateAnimator");
};

ValueWidgetMetadata&           ToggleMeta();
ValueWidgetMetadata&           SelectionMeta();
ValueWidgetMetadata&           SliderMeta();
ValueWidgetMetadata&           WidgetMeta(Il2CppObject* widget);
bool                           OnUIThread();
bool                           SelectionActive();
bool                           SliderActive();
const MethodInfo*              QueryMethod();
BooleanSetting*                SettingFor(Il2CppObject* context);
std::pair<ChoiceSetting*, int> ChoiceFor(Il2CppObject* context);
SliderSetting*                 SliderFor(Il2CppObject* context);
bool                           OwnsValueContext(Il2CppObject* context);
void AddBooleanRow(Il2CppObject* director, Il2CppObject* context, Il2CppObject* parent, BooleanSetting& setting);
void AddChoiceRows(Il2CppObject* director, Il2CppObject* context, Il2CppObject* parent, ChoiceSetting& setting);
void AddSliderRow(Il2CppObject* director, Il2CppObject* context, Il2CppObject* parent, SliderSetting& setting);
void PrepareValueViews(std::size_t count);
bool ValueWidgetsBusy();
void RefreshViews();
void InstallChoiceAndSliderWidgets();
bool InstallCoreValueWidgets(); // True only for this first successful installation.
} // namespace mod_settings::native

#endif

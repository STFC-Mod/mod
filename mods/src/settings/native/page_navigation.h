#pragma once

#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "value_widgets.h"

namespace mod_settings::native
{
struct PageMetadata {
  IL2CppClassHelper category =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "CategoryOptionContext");
  IL2CppClassHelper categoryWidget =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "CategoryOptionWidget");
  IL2CppClassHelper controller =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "GameSettingsViewController");
  const MethodInfo* add       = ToggleMeta().context.GetMethodInfo("AddCategory", 4);
  const MethodInfo* bind      = categoryWidget.GetMethodInfo("OnDidBindContext", 0);
  const MethodInfo* release   = categoryWidget.GetMethodInfo("OnAboutToReleaseContext", 0);
  const MethodInfo* selected  = controller.GetMethodInfo("OnCategorySelected", 1);
  const MethodInfo* destroyed = controller.GetMethodInfo("OnDestroy", 0);
  FieldInfo*        label     = Field(categoryWidget.get_cls(), "_label");
  FieldInfo*        title     = Field(controller.get_cls(), "_title");
  FieldInfo*        panel     = Field(controller.get_cls(), "_optionTabPanel");
};
struct HeadingMetadata {
  IL2CppClassHelper widget = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "TextOptionWidget");
  IL2CppClassHelper row = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "TextOptionContext");
  const MethodInfo* refresh    = widget.GetMethodInfo("SetWidgetData", 0);
  const MethodInfo* clear      = widget.GetMethodInfo("ClearWidgetData", 0);
  const MethodInfo* add        = ToggleMeta().context.GetMethodInfo("AddText", 4);
  const MethodInfo* getContext = widget.GetMethodInfo("get_Context", 0);
  FieldInfo*        label      = Field(widget.get_cls(), "_label");
  FieldInfo*        queryField = Field(row.get_cls(), "<QueryOptionState>k__BackingField");
};

// Metadata access supports startup signature/overlap checks across hook owners.
PageMetadata&                         PageMeta();
HeadingMetadata&                      HeadingMeta();
bool                                  HeadingsActive();
const std::vector<PageCatalog::Page>& Pages();
bool                                  PagesActive();
void                                  DisablePages();
const PageCatalog::Page*              PageFor(Il2CppObject* context);
bool                                  PageRefreshInProgress();
void                                  ClearSectionPage();
void                                  RefreshPageRows();
void                                  RefreshPageSummaries();
void                                  RefreshConditionalSections();
void                                  AddPages(Il2CppObject* director, Il2CppObject* context);
void                                  InstallPages();
Il2CppString*                         EmptyHeadingValue(Il2CppObject*, const MethodInfo*);
} // namespace mod_settings::native

#endif

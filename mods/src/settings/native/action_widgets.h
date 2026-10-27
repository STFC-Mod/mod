#pragma once

#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "interop.h"
#include "settings/page_catalog.h"

namespace mod_settings::native
{
using ActionRow = std::pair<ActionSetting*, std::size_t>;
bool ActionsActive();
// Valid only during a synchronous action invocation; never retain this pointer.
Il2CppObject* InvokingActionWidget();
// Reuses the normal editor owner scope after checking this slider still owns it.
void          InvokeSliderEditor(Il2CppObject* widget, Il2CppObject* context, SliderSetting& slider);
void          RefreshActions();
void          RefreshActionPresentations();
ActionRow     ActionFor(Il2CppObject* context);
void          AddActionRow(Il2CppObject* director, Il2CppObject* context, Il2CppObject* parent, ActionSetting& action,
                           std::size_t index);
void          SyncActionRows(Il2CppObject* controller, Il2CppObject* context, const PageCatalog::Page& page);
void          InstallActionWidgets();
} // namespace mod_settings::native

#endif

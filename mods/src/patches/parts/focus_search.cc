#include "focus_search.h"

#include "config.h"

#include "prime/AssignShipsWidget.h"
#include "prime/CanvasController.h"
#include "prime/ChatMessageListLocalViewController.h"
#include "prime/InventoryListViewController.h"
#include "prime/OfficerAssignmentViewController.h"

#include "il2cpp/il2cpp_helper.h"

#include <spdlog/spdlog.h>

bool FocusSearchBox()
{
  if (!Config::Get().focus_search) {
    return false;
  }

  // Guard chat input so we don't steal focus from the chat text box
  if (auto chat = ObjectFinder<ChatMessageListLocalViewController>::Get(); chat && chat->_inputFieldSelected) {
#ifdef _MODDBG
    spdlog::info("[FocusSearch] blocked: chat input is selected");
#endif
    return false;
  }

  // InventoryListViewController covers OfficerRosterViewController and other inventory subclasses
  auto inventoryControllers = ObjectFinder<InventoryListViewController>::GetAll();
  for (auto controller : inventoryControllers) {
    if (!controller || !controller->_inputField) {
      continue;
    }

    auto canvas  = controller->canvasController;
    bool visible = canvas && canvas->Visible();
    bool active  = controller->isActiveAndEnabled && controller->_inputField->isActiveAndEnabled;
    spdlog::debug("[FocusSearch] inventory controller={} section={} canvas={} visible={} active={}", (void*)controller,
                 (int32_t)controller->_targetSection, (void*)canvas, visible, active);

    if (visible && active) {
#ifdef _MODDBG
      spdlog::info("[FocusSearch] focusing visible inventory search");
#endif
      controller->_inputField->Focus();
      return true;
    }
  }

  // OfficerAssignmentViewController (separate from inventory list)
  auto officerControllers = ObjectFinder<OfficerAssignmentViewController>::GetAll();
  for (auto controller : officerControllers) {
    if (!controller || !controller->_inputField) {
      continue;
    }

    auto canvas  = controller->canvasController;
    bool visible = canvas && canvas->Visible();
    bool active  = controller->isActiveAndEnabled && controller->_inputField->isActiveAndEnabled;
    spdlog::debug("[FocusSearch] officer assignment controller={} canvas={} visible={} active={}", (void*)controller,
                 (void*)canvas, visible, active);

    if (visible && active) {
#ifdef _MODDBG
      spdlog::info("[FocusSearch] focusing visible officer assignment search");
#endif
      controller->_inputField->Focus();
      return true;
    }
  }

  // AssignShipsWidget (ship selection / assignment)
  auto assignShipWidgets = ObjectFinder<AssignShipsWidget>::GetAll();
  for (auto widget : assignShipWidgets) {
    if (!widget || !widget->_inputField) {
      continue;
    }

    auto canvas  = GetCanvasControllerFromComponent(widget);
    bool visible = canvas && canvas->Visible();
    bool active  = widget->isActiveAndEnabled && widget->_inputField->isActiveAndEnabled;
    spdlog::debug("[FocusSearch] assign ships widget={} canvas={} visible={} active={}", (void*)widget, (void*)canvas,
                 visible, active);

    if (visible && active) {
#ifdef _MODDBG
      spdlog::info("[FocusSearch] focusing visible assign ships search");
#endif
      widget->_inputField->Focus();
      return true;
    }
  }

  return false;
}

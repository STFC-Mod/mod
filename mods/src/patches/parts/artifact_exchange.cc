// disable_exchange_all: hides the artifact shards popup's Exchange All button. Individual exchanges stay available.

#include "config.h"

#include "prime/Component.h"

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace
{
struct InventoryUsePopupViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryUsePopupViewController">;

  // Only artifact bulk conversion uses this button, not the inventory entry button or single exchanges.
  Component* convertAllButton()
  { return Field<"_convertAllButton", Component*>(); }
};

void InventoryUsePopupViewController_OnDidBindCanvasContext_Hook(auto original, InventoryUsePopupViewController* _this)
{
  original(_this);
  if (!_this || !Config::Get().disable_exchange_all)
    return;
  auto* button = _this->convertAllButton();
  if (auto* object = button ? button->gameObject() : nullptr)
    object->SetActive(false);
}
} // namespace

void InstallArtifactExchangeHooks()
{
  if (auto m = InventoryUsePopupViewController::Class::Method("OnDidBindCanvasContext", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), InventoryUsePopupViewController_OnDidBindCanvasContext_Hook);
}

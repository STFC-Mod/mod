#include "config.h"

#include <prime/ui.h>

#include <il2cpp/il2cpp.h>
#include <spud/detour.h>

namespace
{
struct DrawerContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.SharedFeatures.Scripts.UI.Widgets", "DrawerContext">;

  bool Enabled()
  { return Field<"Enabled", bool>(); }
};

struct DrawerWidget : Widget<DrawerContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.SharedFeatures.Scripts.UI.Widgets", "DrawerWidget">;

  void OpenViaButtonCallback()
  { Call<"OnOpenButtonClicked">(); }
};

struct ShopListScrollerViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopListScrollerViewController">;

  DrawerWidget* SelectionDrawer()
  { return Field<"_selectionDrawerWidget", DrawerWidget*>(); }
};

ShopListScrollerViewController* g_active_scroller    = nullptr;
bool                            g_opened_for_landing = false;

void TryAutoOpenDrawer(ShopListScrollerViewController* controller)
{
  if (!Config::Get().ui.auto_open_bulk_claim_flyout || controller == nullptr || g_opened_for_landing) {
    return;
  }

  auto* drawer  = controller->SelectionDrawer();
  auto* context = drawer != nullptr ? drawer->Context() : nullptr;
  if (context == nullptr || !context->Enabled()) {
    return;
  }

  // Mark first so a native callback cannot cause a nested lifecycle update to open the drawer twice.
  g_opened_for_landing = true;
  drawer->OpenViaButtonCallback();
}

void ShopListScrollerViewController_AboutToShow(auto original, ShopListScrollerViewController* self)
{
  g_active_scroller    = self;
  g_opened_for_landing = false;

  original(self);
  TryAutoOpenDrawer(self);
}

void ShopListScrollerViewController_AboutToHide(auto original, ShopListScrollerViewController* self)
{
  if (g_active_scroller == self) {
    g_active_scroller    = nullptr;
    g_opened_for_landing = false;
  }

  original(self);
}

void ShopListScrollerViewController_UpdateDrawer(auto original, ShopListScrollerViewController* self)
{
  original(self);

  if (g_active_scroller == self) {
    TryAutoOpenDrawer(self);
  }
}
} // namespace

void InstallGiftsBulkClaimHooks()
{
  using Scroller = ShopListScrollerViewController::Class;
  if (auto m = Scroller::Method("AboutToShow"))
    SPUD_STATIC_DETOUR(m.Pointer(), ShopListScrollerViewController_AboutToShow);
  if (auto m = Scroller::Method("AboutToHide"))
    SPUD_STATIC_DETOUR(m.Pointer(), ShopListScrollerViewController_AboutToHide);
  if (auto m = Scroller::Method("UpdateDrawer"))
    SPUD_STATIC_DETOUR(m.Pointer(), ShopListScrollerViewController_UpdateDrawer);
}

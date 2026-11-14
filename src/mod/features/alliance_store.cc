// [patches] alliancegamesstorehooks: the Alliance Games store. Its cards get the tooltip actions that inventory
// tooltips have, such as VIEW RESEARCH; once it regenerates its offers, it rebuilds its currency list and binds the
// cards in view.

#include "features/features.h"

#include "game/inventory.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/core.h>

#include <utility>

namespace
{
// --- Tooltip actions ---

// A tooltip widget that has action locale assets: `tooltip` itself, or the first loaded one.
InventoryTooltipTriggerWidget* StoreActionLocaleSource(InventoryTooltipTriggerWidget* tooltip)
{
  if (tooltip->searchActionLocaleSetting())
    return tooltip;
  if (auto* widgets = unity::FindObjectsOfTypeAll<InventoryTooltipTriggerWidget>())
    for (auto* widget : *widgets)
      if (widget && widget->tooltipActionTrigger() && widget->searchActionLocaleSetting())
        return widget;
  static bool missing_logged = false;
  if (!std::exchange(missing_logged, true))
    spdlog::warn("[AllianceGamesStore] no loaded tooltip has action texts; store cards stay without actions");
  return nullptr;
}

// Store cards are built with a plain tooltip trigger and no action assets, so their widget has nothing to put actions
// on. The first time a card shows in the Alliance Games store, it gets an action trigger with the plain one's
// appearance and borrowed assets. From then on the widget's own Refresh picks the actions on every bind, with the
// game's eligibility checks, so nothing here tracks cards.
void BundleDataWidget_OnDidBindContext_Hook(auto original, BundleDataWidget* _this)
{
  original(_this);
  auto* list    = _this->scrollerController();
  auto* tooltip = _this->invTooltip();
  if (!list || !list->Is<AllianceGamesStoreScrollerViewController>() || !tooltip || tooltip->tooltipActionTrigger())
    return;
  auto* plain  = tooltip->tooltipTrigger();
  auto* object = tooltip->gameObject();
  auto* source = plain && object ? StoreActionLocaleSource(tooltip) : nullptr;
  auto* action = source ? object->AddComponent<TooltipActionTrigger>() : nullptr;
  if (!action)
    return;
  action->CopyAppearance(plain);
  plain->SetEnabled(false);
  tooltip->UseActionTrigger(action, source);
  // This bind already ran the widget's own setup on the plain trigger.
  tooltip->Refresh();
  tooltip->SetupActionTooltipTrigger();
}

// --- Regeneration ---

// The store whose regenerated cards may still be spawning. The next regeneration replaces it.
il2cpp::GcHandle alliance_store_spawning;

void ShopListScrollerViewController_RegenerateSortedBundles_Hook(auto original, ShopListScrollerViewController* _this,
                                                                 bool instant)
{
  original(_this, instant);
  if (_this->Is<AllianceGamesStoreScrollerViewController>())
    alliance_store_spawning = il2cpp::GcHandle::Weak(_this);
}

void AllianceGamesStoreFrame()
{
  auto* store    = alliance_store_spawning.Get<AllianceGamesStoreScrollerViewController>();
  auto* scroller = store ? store->smartScroller() : nullptr;
  if (!store || (scroller && scroller->Spawning()))
    return;
  alliance_store_spawning.Reset();
  if (!unity::Alive(store))
    return;
  // The store's context handler is an empty override, so nothing rebuilds the currency list that the section context
  // made before the bundles arrived.
  auto* context = store->AllianceGamesContext();
  if (context && context->HasBundles())
    context->UpdateStoreCurrency();
  // Cards spawned after the regeneration stay unbound until the list scrolls.
  if (scroller)
    scroller->Rebind();
}

bool InstallAllianceGamesStore()
{
  auto bind = BundleDataWidget::Class::Method<void()>("OnDidBindContext");
  // The lookup falls back to Widget`1's, which every widget shares.
  if (bind && bind.Info()->klass != BundleDataWidget::Class::Raw()) {
    spdlog::warn("[AllianceGamesStore] BundleDataWidget no longer overrides OnDidBindContext");
    return false;
  }
  il2cpp::HookSet hooks("AllianceGamesStore");
  IL2CPP_STAGE(hooks, bind, BundleDataWidget_OnDidBindContext_Hook);
  IL2CPP_STAGE(hooks, ShopListScrollerViewController::Class::Method<void(bool)>("RegenerateSortedBundles"),
               ShopListScrollerViewController_RegenerateSortedBundles_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kAllianceGamesStore{"AllianceGamesStore", &Config::Patches::alliancegamesstorehooks,
                                  InstallAllianceGamesStore, AllianceGamesStoreFrame};

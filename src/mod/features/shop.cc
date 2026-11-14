// The shop, gift and inventory popups: auto_open_bulk_claim_flyout on the gifts screen, the chest purchase and
// donation limits, the view button on offers that are cooling down, always_skip_reveal_sequence, and
// disable_exchange_all on the artifact shards popup.

#include "config.h"
#include "features/features.h"

#include "game/inventory.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>

#include <algorithm>
#include <utility>

namespace
{
// --- Gifts: open the bulk claim drawer ---

// The gifts screen that is showing, and whether its drawer was opened since it appeared.
ShopListScrollerViewController* shown_scroller = nullptr;
bool                            drawer_opened  = false;

void OpenBulkClaimDrawer(ShopListScrollerViewController* controller)
{
  if (!Config::Get().ui.auto_open_bulk_claim_flyout || !controller || drawer_opened)
    return;

  auto* drawer  = controller->selectionDrawerWidget();
  auto* context = drawer ? drawer->Context() : nullptr;
  if (!context || !context->Enabled())
    return;

  // Mark first so a native callback cannot cause a nested lifecycle update to open the drawer twice.
  drawer_opened = true;
  drawer->OnOpenButtonClicked();
}

void ShopListScrollerViewController_AboutToShow_Hook(auto original, ShopListScrollerViewController* _this)
{
  shown_scroller = _this;
  drawer_opened  = false;
  original(_this);
  OpenBulkClaimDrawer(_this);
}

void ShopListScrollerViewController_AboutToHide_Hook(auto original, ShopListScrollerViewController* _this)
{
  if (shown_scroller == _this) {
    shown_scroller = nullptr;
    drawer_opened  = false;
  }
  original(_this);
}

// The drawer only becomes usable once the game fills it.
void ShopListScrollerViewController_UpdateDrawer_Hook(auto original, ShopListScrollerViewController* _this)
{
  original(_this);
  if (shown_scroller == _this)
    OpenBulkClaimDrawer(_this);
}

// --- Purchase and donation limits ---

// The most the game's chest purchase popup handles; extend_chest_purchase_max is clamped to it.
constexpr int64_t kMaximumChestPurchaseMax = 160;

// The popup's limits are adjusted where its row is rendered. IsChestPurchase is set too late for the MaxItemsToUse
// setter, and adjusting the context rather than the slider leaves every other slider alone.
void InventoryUseRowWidget_SetWidgetData_Hook(auto original, InventoryUseRowWidget* _this)
{
  auto*       context = _this ? _this->Context() : nullptr;
  const auto& ui      = Config::Get().ui;
  if (context && context->IsChestPurchase()) {
    const auto native_max  = context->MaxItemsToUse();
    const auto bounded_max = std::min(static_cast<int64_t>(ui.extend_chest_purchase_max), kMaximumChestPurchaseMax);
    if (native_max > 0 && bounded_max > native_max)
      context->SetMaxItemsToUse(bounded_max);
  } else if (context && ui.extend_donation_slider && context->IsDonationUse()) {
    // extend_donation_max replaces the game's donation cap; 0 or less means no cap. Either way the slider stops at
    // what the player owns.
    const auto owned  = context->AmountOwned();
    auto       target = ui.extend_donation_max > 0 ? static_cast<int64_t>(ui.extend_donation_max) : owned;
    if (owned > 0)
      target = std::min(target, owned);
    if (target > 0 && target != context->MaxItemsToUse())
      context->SetMaxItemsToUse(target);
  }
  original(_this);
}

// --- Offers on cooldown ---

// An offer that is cooling down cannot be bought; its button opens the offer's details instead.
void BundleDataWidget_OnActionButtonPressedCallback_Hook(auto original, BundleDataWidget* _this)
{
  if (_this->CurrentState() & BundleDataWidget::kCooldownTimerOn)
    _this->AuxViewButtonPressedHandler();
  else
    original(_this);
}

// --- Reveal sequence ---

bool ShopSceneManager_ShouldShowRevealSequence_Hook(auto original, void* _this, bool ignore)
{
  const auto result = original(_this, ignore);
  return result && !Config::Get().ui.always_skip_reveal_sequence;
}

void InstallGiftsAutoOpen()
{
  using Scroller = ShopListScrollerViewController::Class;
  IL2CPP_HOOK(Scroller::Method("AboutToShow"), ShopListScrollerViewController_AboutToShow_Hook);
  IL2CPP_HOOK(Scroller::Method("AboutToHide"), ShopListScrollerViewController_AboutToHide_Hook);
  IL2CPP_HOOK(Scroller::Method("UpdateDrawer"), ShopListScrollerViewController_UpdateDrawer_Hook);
}

void InstallShopLimits()
{
  IL2CPP_HOOK(InventoryUseRowWidget::Class::Method("SetWidgetData", 0), InventoryUseRowWidget_SetWidgetData_Hook);
  IL2CPP_HOOK(BundleDataWidget::Class::Method("OnActionButtonPressedCallback"),
              BundleDataWidget_OnActionButtonPressedCallback_Hook);
}

// --- Artifact Exchange All ---

// Hides the button after the popup binds. Individual exchanges stay available.
void InventoryUsePopupViewController_OnDidBindCanvasContext_Hook(auto original, InventoryUsePopupViewController* _this)
{
  original(_this);
  if (!_this || !Config::Get().ui.disable_exchange_all)
    return;
  auto* button = _this->convertAllButton();
  if (auto* object = button ? button->gameObject() : nullptr)
    object->SetActive(false);
}

bool popup_shown = false;

bool SuppressPopup()
{
  if (!Config::Get().ui.only_show_first_popup)
    return false;
  return std::exchange(popup_shown, true);
}

void InterstitialViewController_AboutToShow_Hook(auto original, void* _this)
{
  if (!SuppressPopup())
    original(_this);
}

void ShopSceneManager_ShowPlcOfferPopup_Hook(auto original, void* _this)
{
  if (!SuppressPopup())
    original(_this);
}

void InstallSkipRevealSequence()
{
  IL2CPP_HOOK(ShopSceneManager::Method("ShouldShowRevealSequence"), ShopSceneManager_ShouldShowRevealSequence_Hook);
  IL2CPP_HOOK(ShopSceneManager::Method("ShowPlcOfferPopup", 0), ShopSceneManager_ShowPlcOfferPopup_Hook);
  IL2CPP_HOOK(InterstitialViewController::Class::Method("AboutToShow", 0), InterstitialViewController_AboutToShow_Hook);
}

void InstallArtifactExchange()
{
  IL2CPP_HOOK(InventoryUsePopupViewController::Class::Method("OnDidBindCanvasContext", 0),
              InventoryUsePopupViewController_OnDidBindCanvasContext_Hook);
}
} // namespace

const Feature kGiftsAutoOpen{"GiftsAutoOpen", &Config::Patches::giftsbulkclaimhooks, InstallGiftsAutoOpen};
const Feature kShopLimits{"ShopLimits", &Config::Patches::miscpatches, InstallShopLimits};
const Feature kSkipRevealSequence{"SkipRevealSequence", &Config::Patches::tempcrashfixes, InstallSkipRevealSequence};
const Feature kArtifactExchange{"ArtifactExchange", &Config::Patches::artifactexchangehooks, InstallArtifactExchange};

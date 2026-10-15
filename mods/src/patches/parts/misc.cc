#include "config.h"
#include "prime/InterstitialViewController.h"

#include <prime/BundleDataWidget.h>
#include <prime/ClientModifierType.h>
#include <prime/Hub.h>
#include <prime/InventoryForPopup.h>
#include <prime/InventoryUseRowWidget.h>
#include <prime/ShopSummaryDirector.h>

#include <il2cpp/il2cpp.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#if _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <prime/ActionQueueManager.h>
#include <prime/InterstitialViewController.h>

namespace
{
constexpr int64_t kMaximumChestPurchaseMax = 160;
}

#if _WIN32
void InventoryForPopup_set_MaxItemsToUse(auto original, InventoryForPopup* a1, int64_t a2)
{
  if (!a1) {
    return;
  }

  if (a1->IsDonationUse() && a2 == 50 && Config::Get().extend_donation_slider) {
    const auto max = Config::Get().extend_donation_max;
    if (max > 0) {
      a2 = max;
    } else {
      // Leave the initial unlimited value in place instead of applying the game's donation cap.
      return;
    }
  }

  original(a1, a2);
}
#endif

// IsChestPurchase is populated too late for the existing MaxItemsToUse setter hook, so adjust the tagged context at
// the row-render seam instead.
void InventoryUseRowWidget_SetWidgetData(auto original, InventoryUseRowWidget* widget)
{
  if (widget) {
    if (auto* context = widget->Context(); context && context->IsChestPurchase()) {
      const auto native_max    = context->MaxItemsToUse();
      const auto requested_max = static_cast<int64_t>(Config::Get().extend_chest_purchase_max);
      const auto bounded_max   = std::min(requested_max, kMaximumChestPurchaseMax);
      if (native_max > 0 && bounded_max > native_max) {
        context->SetMaxItemsToUse(bounded_max);
      }
    }
  }

  original(widget);
}

#if __APPLE__
void UnitySlider_set_maxValue(auto original, void* a1, float a2)
{
  if ((a2 == 50.0f || a2 == 100.0f) && Config::Get().extend_donation_slider && Config::Get().extend_donation_max > 0) {
    a2 = (float)Config::Get().extend_donation_max;
  }
  original(a1, a2);
}
#endif

void BundleDataWidget_OnActionButtonPressedCallback(auto original, BundleDataWidget* _this)
{
  if (_this->CurrentState() & BundleDataWidget::ItemState::CooldownTimerOn) {
    _this->AuxViewButtonPressedHandler();
  } else {
    original(_this);
  }
}

void InstallMiscPatches()
{
#if _WIN32
  if (auto m = InventoryForPopup::Class::Method("set_MaxItemsToUse"))
    SPUD_STATIC_DETOUR(m.Pointer(), InventoryForPopup_set_MaxItemsToUse);
#endif

  if (Config::Get().extend_chest_purchase_max > 0) {
    if (auto m = InventoryUseRowWidget::Class::Method("SetWidgetData", 0))
      SPUD_STATIC_DETOUR(m.Pointer(), InventoryUseRowWidget_SetWidgetData);
  }

#if __APPLE__
  using Slider = il2cpp::Class<"UnityEngine.UI", "UnityEngine.UI", "Slider">;
  if (auto m = Slider::Method("set_maxValue"))
    SPUD_STATIC_DETOUR(m.Pointer(), UnitySlider_set_maxValue);
#endif

  if (auto m = BundleDataWidget::Class::Method("OnActionButtonPressedCallback"))
    SPUD_STATIC_DETOUR(m.Pointer(), BundleDataWidget_OnActionButtonPressedCallback);
}

// List<IBuffData> in and out.
il2cpp::List<il2cpp::Object*>* ExtractBuffsOfType_Hook(auto original, ClientModifierType modifier,
                                                       il2cpp::List<il2cpp::Object*>* list)
{
  if (list) {
    for (auto* item : *list) {
      if (!item) {
        return nullptr;
      }
    }
  }
  return original(modifier, list);
}

bool ShouldShowRevealHook(auto original, void* _this, bool ignore)
{
  auto result = original(_this, ignore);
  if (Config::Get().always_skip_reveal_sequence) {
    return false;
  }
  return result;
}




void ActionQueueManager_AddActionToQueue(auto original, ActionQueueManager* _this, int64_t fleet_id)
{
  spdlog::warn("ActionQueueManager_AddActionToQueue({})", fleet_id);
  original(_this, fleet_id);
}

//   const auto section_data = Hub::get_SectionManager()->_sectionStorage->GetState(sectionID);


std::atomic_bool popup_shown{false};

bool ShouldSuppressAdditionalPopup()
{
  bool expected = false;
  return !popup_shown.compare_exchange_strong(expected, true);
}

void InterstitialViewController_AboutToShow_Hook(auto original, InterstitialViewController* _this)
{
  original(_this);
  if (Config::Get().only_show_first_popup && _this && ShouldSuppressAdditionalPopup())
    _this->CloseWhenReady();
}

void ShopSceneManager_ShowPlcOfferPopup_Hook(auto original, void* _this)
{
  if (!Config::Get().only_show_first_popup || !ShouldSuppressAdditionalPopup())
    original(_this);
}

void InstallTempCrashFixes()
{
  using BuffService = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "BuffService">;
  if (auto m = BuffService::Method("ExtractBuffsOfType"))
    SPUD_STATIC_DETOUR(m.Pointer(), ExtractBuffsOfType_Hook);

  using ShopSceneManager = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopSceneManager">;
  if (auto m = ShopSceneManager::Method("ShouldShowRevealSequence"))
    SPUD_STATIC_DETOUR(m.Pointer(), ShouldShowRevealHook);


  // Debug-only trace, left disabled.
  // if (auto m = ActionQueueManager::Class::Method("AddActionToQueue"))
  //   SPUD_STATIC_DETOUR(m.Pointer(), ActionQueueManager_AddActionToQueue);

  using PopupShop = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopSceneManager">;
  if (auto m = PopupShop::Method("ShowPlcOfferPopup", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), ShopSceneManager_ShowPlcOfferPopup_Hook);
  if (auto m = InterstitialViewController::Class::Method("AboutToShow", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), InterstitialViewController_AboutToShow_Hook);
}

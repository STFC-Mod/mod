#include "config.h"
#include "errormsg.h"

#include <prime/BundleDataWidget.h>
#include <prime/ClientModifierType.h>
#include <prime/Hub.h>
#include <prime/IList.h>
#include <prime/InventoryForPopup.h>
#include <prime/InventoryUseRowWidget.h>
#include <prime/ShopSummaryDirector.h>

#include <il2cpp/il2cpp_helper.h>

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

  if (a1->IsDonationUse && a2 == 50 && Config::Get().extend_donation_slider) {
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
    if (auto* context = widget->Context; context && context->IsChestPurchase) {
      const auto native_max    = context->MaxItemsToUse;
      const auto requested_max = static_cast<int64_t>(Config::Get().extend_chest_purchase_max);
      const auto bounded_max   = std::min(requested_max, kMaximumChestPurchaseMax);
      if (native_max > 0 && bounded_max > native_max) {
        context->MaxItemsToUse = bounded_max;
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
  if (_this->CurrentState & BundleDataWidget::ItemState::CooldownTimerOn) {
    _this->AuxViewButtonPressedHandler();
  } else {
    original(_this);
  }
}

void InstallMiscPatches()
{
#if _WIN32
  auto h = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Inventories", "InventoryForPopup");
  if (!h.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.Inventories", "InventoryForPopup");
  } else {
    auto ptr = h.GetMethod("set_MaxItemsToUse");
    if (!ptr) {
      ErrorMsg::MissingMethod("InventoryForPopup", "set_MaxItemsToUse");
    } else {
      SPUD_STATIC_DETOUR(ptr, InventoryForPopup_set_MaxItemsToUse);
    }
  }
#endif

  if (Config::Get().extend_chest_purchase_max > 0) {
    auto row_widget = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Inventories", "InventoryUseRowWidget");
    if (!row_widget.isValidHelper()) {
      ErrorMsg::MissingHelper("Digit.Prime.Inventories", "InventoryUseRowWidget");
    } else {
      auto ptr = row_widget.GetMethod("SetWidgetData", 0);
      if (!ptr) {
        ErrorMsg::MissingMethod("InventoryUseRowWidget", "SetWidgetData");
      } else {
        SPUD_STATIC_DETOUR(ptr, InventoryUseRowWidget_SetWidgetData);
      }
    }
  }

#if __APPLE__
  auto unity_slider = il2cpp_get_class_helper("UnityEngine.UI", "UnityEngine.UI", "Slider");
  if (!unity_slider.isValidHelper()) {
    ErrorMsg::MissingHelper("UnityEngine.UI", "Slider");
  } else {
    auto set_max_ptr = unity_slider.GetMethod("set_maxValue");
    if (!set_max_ptr) {
      ErrorMsg::MissingMethod("Slider", "set_maxValue");
    } else {
      SPUD_STATIC_DETOUR(set_max_ptr, UnitySlider_set_maxValue);
    }
  }
#endif

  auto bundle_data_widget = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Shop", "BundleDataWidget");
  if (!bundle_data_widget.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.Shop", "BundleDataWidget");
  } else {
    auto ptr = bundle_data_widget.GetMethod("OnActionButtonPressedCallback");
    if (!ptr) {
      ErrorMsg::MissingMethod("BundleDataWidget", "OnActionButtonPressedCallback");
    } else
      SPUD_STATIC_DETOUR(ptr, BundleDataWidget_OnActionButtonPressedCallback);
  }
}

IList* ExtractBuffsOfType_Hook(auto original, ClientModifierType modifier, IList* list)
{
  if (list) {
    for (int i = 0; i < list->Count; ++i) {
      auto item = list->Get(i);
      if (item == 0) {
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

struct ShopCategory {
public:
  __declspec(property(get = __get__flagValue)) int Value;

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.Prime.Shop", "ShopCategory");
    return class_helper;
  }

public:
  int __get__flagValue()
  {
    static auto field = get_class_helper().GetProperty("Value");
    return *field.GetUnboxedSelf<int>(this);
  }
};

struct CurrencyType {
public:
  __declspec(property(get = __get__flagValue)) int Value;
  //

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimePlatform.Content", "CurrencyType");
    return class_helper;
  }

public:
  int __get__flagValue()
  {
    static auto field = get_class_helper().GetProperty("value");
    return *field.GetUnboxedSelf<int>(this);
  }
};

struct BundleGroupConfig {
public:
  __declspec(property(get = __get__category)) int _category;
  __declspec(property(get = __get__currency)) int _currency;

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Shop", "BundleGroupConfig");
    return class_helper;
  }

public:
  int __get__category()
  {
    static auto field = get_class_helper().GetField("_category");
    return *(int*)((ptrdiff_t)this + field.offset());
  }

  int __get__currency()
  {
    static auto field = get_class_helper().GetField("_currency");
    return *(int*)((ptrdiff_t)this + field.offset());
  }
};

class ShopSectionContext
{
public:
  __declspec(property(get = __get__bundleConfig)) BundleGroupConfig* _bundleConfig;

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Shop", "ShopSectionContext");
    return class_helper;
  }

public:
  BundleGroupConfig* __get__bundleConfig()
  {
    static auto field = get_class_helper().GetProperty("BundleGroup");
    return field.GetRaw<BundleGroupConfig>(this);
  }
};

static std::atomic_bool g_first_popup_shown{false};

bool ShouldSuppressAdditionalPopup()
{
  bool expected = false;
  return !g_first_popup_shown.compare_exchange_strong(expected, true);
}

void InterstitialViewController_AboutToShow(auto original, InterstitialViewController* _this)
{
  original(_this);
  if (Config::Get().only_show_first_popup && _this != nullptr) {
    if (ShouldSuppressAdditionalPopup()) {
      spdlog::debug("InterstitialViewController_AboutToShow: suppressing interstitial popup (already shown one)");
      _this->CloseWhenReady();
    } else {
      spdlog::debug("InterstitialViewController_AboutToShow: allowing first popup of the session to show");
    }
  }
}

void ShopSceneManager_ShowPlcOfferPopup(auto original, void* _this)
{
  if (Config::Get().only_show_first_popup && ShouldSuppressAdditionalPopup()) {
    spdlog::debug("ShopSceneManager_ShowPlcOfferPopup: suppressing PLC offer popup (already shown one)");
    return;
  }
  original(_this);
}

void ActionQueueManager_AddActionToQueue(auto original, ActionQueueManager* _this, long fleet_id)
{
  spdlog::warn("ActionQueueManager_AddActionToQueue({})", fleet_id);
  original(_this, fleet_id);
}

//   const auto section_data = Hub::get_SectionManager()->_sectionStorage->GetState(sectionID);

void InstallTempCrashFixes()
{
  auto BuffService_helper =
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "BuffService");
  if (!BuffService_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Services", "BuffService");
  } else {
    auto ptr_extract_buffs_of_type = BuffService_helper.GetMethod("ExtractBuffsOfType");
    if (ptr_extract_buffs_of_type == nullptr) {
      ErrorMsg::MissingMethod("BuffService", "ExtractBuffsOfType");
    } else {
      SPUD_STATIC_DETOUR(ptr_extract_buffs_of_type, ExtractBuffsOfType_Hook);
    }
  }

  static auto shop_scene_manager = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Shop", "ShopSceneManager");
  if (!shop_scene_manager.isValidHelper()) {
    ErrorMsg::MissingHelper("Shop", "ShopSceneManager");
  } else {
    auto reveal_show = shop_scene_manager.GetMethod("ShouldShowRevealSequence");
    if (reveal_show == nullptr) {
      ErrorMsg::MissingMethod("ShopSceneManager", "ShouldShowRevealSequence");
    } else {
      SPUD_STATIC_DETOUR(reveal_show, ShouldShowRevealHook);
    }

    auto show_plc = shop_scene_manager.GetMethod("ShowPlcOfferPopup", 0);
    if (!show_plc) {
      ErrorMsg::MissingMethod("ShopSceneManager", "ShowPlcOfferPopup");
    } else {
      SPUD_STATIC_DETOUR(show_plc, ShopSceneManager_ShowPlcOfferPopup);
    }
  }

  static auto interstitial_controller =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Interstitial", "InterstitialViewController");
  if (!interstitial_controller.isValidHelper()) {
    ErrorMsg::MissingHelper("Interstitial", "InterstitialViewController");
  } else {
    auto interstitial_show = interstitial_controller.GetMethod("AboutToShow");
    if (interstitial_show == nullptr) {
      ErrorMsg::MissingMethod("InterstitialViewController", "AboutToShow");
    } else {
      SPUD_STATIC_DETOUR(interstitial_show, InterstitialViewController_AboutToShow);
    }
  }

  static auto actionqueue_manager =
      il2cpp_get_class_helper("Assembly-CSharp", "Prime.ActionQueue", "ActionQueueManager");
  if (!actionqueue_manager.isValidHelper()) {
    ErrorMsg::MissingHelper("ActionQueue", "ActionQueueManager");
  } else {
    auto addtoqueue_method = actionqueue_manager.GetMethod("AddActionToQueue");
    if (addtoqueue_method == nullptr) {
      ErrorMsg::MissingMethod("ActionQueueManager", "AddActionToQueue");
    } else {
      // SPUD_STATIC_DETOUR(addtoqueue_method, ActionQueueManager_AddActionToQueue);
    }
  }
}

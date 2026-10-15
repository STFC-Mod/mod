// Small game tweaks: purchase and donation limits, bundle cooldown buttons, crash guards, the reveal sequence,
// the cursor, settings URL overrides, and the action queue toggle.

#include "config.h"
#include "prime/ui.h"
#include "patches/patches.h"

#include <prime/Hub.h>
#include <prime/fleet.h>
#include <prime/inventory.h>
#include <prime/server.h>
#include <prime/ui.h>
#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <atomic>

#if _WIN32
#include <Windows.h>
#endif

namespace
{
// The most the game's chest purchase popup handles; extend_chest_purchase_max is clamped to it.
constexpr int64_t kMaximumChestPurchaseMax = 160;

// The popup's limits are adjusted where its row is rendered. IsChestPurchase is set too late for the
// MaxItemsToUse setter, and adjusting the context rather than the slider leaves every other slider alone.
void InventoryUseRowWidget_SetWidgetData(auto original, InventoryUseRowWidget* widget)
{
  auto*       context = widget ? widget->Context() : nullptr;
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
  original(widget);
}

void BundleDataWidget_OnActionButtonPressedCallback(auto original, BundleDataWidget* _this)
{
  if (_this->CurrentState() & BundleDataWidget::ItemState::CooldownTimerOn) {
    _this->AuxViewButtonPressedHandler();
  } else {
    original(_this);
  }
}
} // namespace

void InstallMiscPatches()
{
  if (auto m = InventoryUseRowWidget::Class::Method("SetWidgetData", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), InventoryUseRowWidget_SetWidgetData);

  if (auto m = BundleDataWidget::Class::Method("OnActionButtonPressedCallback"))
    SPUD_STATIC_DETOUR(m.Pointer(), BundleDataWidget_OnActionButtonPressedCallback);
}

namespace
{
// List<IBuffData> in and out. A list with a null buff crashes the game, so it is answered with no buffs instead.
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
  if (Config::Get().ui.always_skip_reveal_sequence) {
    return false;
  }
  return result;
}
} // namespace


std::atomic_bool popup_shown{false};

bool ShouldSuppressAdditionalPopup()
{
  bool expected = false;
  return !popup_shown.compare_exchange_strong(expected, true);
}

void InterstitialViewController_AboutToShow_Hook(auto original, InterstitialViewController* _this)
{
  original(_this);
  if (Config::Get().ui.only_show_first_popup && _this && ShouldSuppressAdditionalPopup())
    _this->CloseWhenReady();
}

void ShopSceneManager_ShowPlcOfferPopup_Hook(auto original, void* _this)
{
  if (!Config::Get().ui.only_show_first_popup || !ShouldSuppressAdditionalPopup())
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

  // TODO: disable_first_popup closed the first interstitial via InterstitialViewController.AboutToShow and
  // CloseWhenReady. It was switched off and the hook only passed through, so it is no longer installed.

  using PopupShop = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopSceneManager">;
  if (auto m = PopupShop::Method("ShowPlcOfferPopup", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), ShopSceneManager_ShowPlcOfferPopup_Hook);
  if (auto m = InterstitialViewController::Class::Method("AboutToShow", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), InterstitialViewController_AboutToShow_Hook);
}

// --- "test" patches: cursor, settings URL overrides, and the action queue toggle ---

namespace
{
struct AppConfig : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Core", "AppConfig">;

  void SetPlatformSettingsUrl(Il2CppString* value)
  { Set<"PlatformSettingsUrl">(value); }
  void SetAssetUrlOverride(Il2CppString* value)
  { Set<"AssetUrlOverride">(value); }
};

struct Model : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Core", "Model">;

  AppConfig* appConfig()
  { return Field<"_appConfig", AppConfig*>(); }
};

#if _WIN32
// allow_cursor = false: the plain arrow, never clipped to the window.
void Cursor_SetCursor(auto original, void* _this, ptrdiff_t texture, Vector2* hotspot, int mode)
{
  if (Config::Get().graphics.allow_cursor)
    return original(_this, texture, hotspot, mode);
  SetCursor(LoadCursor(NULL, IDC_ARROW));
  ClipCursor(nullptr);
}
#endif

void Model_LoadConfigs(auto original, Model* _this)
{
  original(_this);
  auto* config = _this->appConfig();
  if (!config)
    return;
  const auto& game = Config::Get().game;
  if (!game.settings_url.empty())
    config->SetPlatformSettingsUrl(il2cpp_string_new(game.settings_url.c_str()));
  if (!game.assets_url_override.empty())
    config->SetAssetUrlOverride(il2cpp_string_new(game.assets_url_override.c_str()));
}

bool IsQueueEnabled(auto original, void* _this)
{ return Config::Get().control.queue_enabled && original(_this); }
} // namespace

void InstallTestPatches()
{
#if _WIN32
  using Cursor = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Cursor">;
  if (auto m = Cursor::Method("SetCursor_Injected"))
    SPUD_STATIC_DETOUR(m.Pointer(), Cursor_SetCursor);
#endif

  if (auto m = Model::Class::Method("LoadConfigs", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), Model_LoadConfigs);

  if (auto m = ActionQueueManager::Class::Method("IsQueueUnlocked"))
    SPUD_STATIC_DETOUR(m.Pointer(), IsQueueEnabled);
}

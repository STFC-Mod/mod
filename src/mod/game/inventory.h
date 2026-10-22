#pragma once

// The inventory, its use and purchase popups, and the shop and gift screens.

#include "server.h"
#include "ui.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>
#include <optional>

struct InterstitialViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Interstitial", "InterstitialViewController">;
};

// The context of a use, purchase or donation popup.
struct InventoryForPopup : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryForPopup">;

  int64_t MaxItemsToUse()
  { return Get<"MaxItemsToUse", int64_t>(); }
  void SetMaxItemsToUse(int64_t value)
  { Set<"MaxItemsToUse">(value); }
  bool IsDonationUse()
  { return Get<"IsDonationUse", bool>(); }
  int64_t AmountOwned()
  { return Get<"AmountOwned", int64_t>(); }
  bool IsChestPurchase()
  { return Get<"IsChestPurchase", bool>(); }
};

struct InventoryUseRowWidget : Widget<InventoryForPopup> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryUseRowWidget">;
};

// The use popup, which also exchanges artifact shards.
struct InventoryUsePopupViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryUsePopupViewController">;

  // Only artifact bulk conversion uses this button, not the inventory entry button or single exchanges.
  unity::Component* convertAllButton()
  { return Field<"_convertAllButton", unity::Component*>(); }
};

// Also the base of the officer roster and the other inventory-style lists.
struct InventoryListViewController : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryListViewController">;

  InputFieldWidget* inputField()
  { return Field<"_inputField", InputFieldWidget*>(); }
};

struct Bundle : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimePlatform.Content", "Bundle">;
};

struct ShopListScrollerViewController;
struct InventoryTooltipTriggerWidget;

// A shop offer.
struct BundleDataWidget : Widget<Bundle> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "BundleDataWidget">;

  // ItemState flags; only the one the mod reads is named.
  static constexpr int32_t kCooldownTimerOn = 16384;

  int32_t currentState()
  { return Field<"_currentState", int32_t>(); }
  void AuxViewButtonPressedHandler()
  { Call<"AuxViewButtonPressedHandler">(); }

  // Set by OnDidBindContext after the card set up its tooltip.
  inline ShopListScrollerViewController* scrollerController();
  inline InventoryTooltipTriggerWidget*  invTooltip();
};

struct DrawerContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.SharedFeatures.Scripts.UI.Widgets", "DrawerContext">;

  bool Enabled()
  { return Field<"Enabled", bool>(); }
};

struct DrawerWidget : Widget<DrawerContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.SharedFeatures.Scripts.UI.Widgets", "DrawerWidget">;

  void OnOpenButtonClicked()
  { Call<"OnOpenButtonClicked">(); }
};

// The gifts screen; its selection drawer holds bulk claim.
struct ShopListScrollerViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopListScrollerViewController">;

  DrawerWidget* selectionDrawerWidget()
  { return Field<"_selectionDrawerWidget", DrawerWidget*>(); }
  // Declared as some scroller; the store only uses a SmartScrollerBase.
  il2cpp::Object* smartScroller()
  { return Field<"_smartScroller", il2cpp::Object*>(); }
};

// Digit.Client.Serialization.OverridableBool: the serialized flag and the runtime override.
struct OverridableBool {
  bool serialized, current, modified;
};

struct ActionLocaleSetting : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Actions", "ActionLocaleSetting">;
};

struct ActionLocaleSettingsList : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Actions", "ActionLocaleSettingsList">;
};

struct TooltipLoadAndShow : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Tooltip", "TooltipLoadAndShow">;
};

// TryEnabled and TrySetEnabled tell a failed call from the value, so the store can undo a half-made swap.
struct TooltipTrigger : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Tooltip", "TooltipTrigger">;

  TooltipLoadAndShow* loader()
  { return Field<"_loader", TooltipLoadAndShow*>(); }
  std::optional<bool> TryEnabled()
  { return TryCall<"get_enabled", bool()>(); }
  bool TrySetEnabled(bool value)
  { return TryCall<"set_enabled", void(bool)>(value); }

  // Every serialized appearance field: what to show, where, and how it loads.
  void CopyAppearance(TooltipTrigger* source)
  {
    SetField<"_showHeader">(source->Field<"_showHeader", OverridableBool>());
    SetField<"_header">(source->Field<"_header", LocaleTextContext*>());
    SetField<"_showDescription">(source->Field<"_showDescription", OverridableBool>());
    SetField<"_description">(source->Field<"_description", LocaleTextContext*>());
    SetField<"_overrideLocale">(source->Field<"_overrideLocale", bool>());
    // Digit.Client.Tooltip.TooltipAnchor; -1 is Undefined.
    SetField<"_anchorOverride">(source->Field<"_anchorOverride", int32_t>());
    SetField<"_sizeOverride">(source->Field<"_sizeOverride", unity::Vector2>());
    SetField<"_positionOverrideTarget">(source->Field<"_positionOverrideTarget", unity::Transform*>());
    SetField<"_positionOverrideOffset">(source->Field<"_positionOverrideOffset", unity::Vector2>());
    SetField<"_isInteractable">(source->Field<"_isInteractable", bool>());
    SetField<"<DescriptionOverrideText>k__BackingField">(
        source->Field<"<DescriptionOverrideText>k__BackingField", Il2CppString*>());
    SetField<"_loader">(source->loader());
  }
};

struct TooltipActionTrigger : TooltipTrigger {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Tooltip", "TooltipActionTrigger">;
};

// What a store tooltip shows. Its members resolve on the runtime class.
struct TooltipItem : il2cpp::Object {
  // ResourceSpec.Id; 0 when there is none.
  int64_t ResourceId()
  {
    auto* spec = Get<"ResourceSpec", il2cpp::Object*>();
    return spec ? spec->Get<"Id", int64_t>() : 0;
  }
};

// The tooltip on an inventory or shop item. The Alliance Games store swaps a card's plain trigger for an action
// trigger that carries the same appearance plus the native research actions, and swaps it back on release.
struct InventoryTooltipTriggerWidget : unity::Component {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryTooltipTriggerWidget">;

  TooltipItem* Context()
  { return Get<"Context", TooltipItem*>(); }

  TooltipTrigger* tooltipTrigger()
  { return Field<"_tooltipTrigger", TooltipTrigger*>(); }
  void SetTooltipTrigger(TooltipTrigger* value)
  { SetField<"_tooltipTrigger">(value); }
  TooltipActionTrigger* tooltipActionTrigger()
  { return Field<"_tooltipActionTrigger", TooltipActionTrigger*>(); }
  void SetTooltipActionTrigger(TooltipActionTrigger* value)
  { SetField<"_tooltipActionTrigger">(value); }
  ActionLocaleSetting* searchActionLocaleSetting()
  { return Field<"_searchActionLocaleSetting", ActionLocaleSetting*>(); }
  void SetSearchActionLocaleSetting(ActionLocaleSetting* value)
  { SetField<"_searchActionLocaleSetting">(value); }
  ActionLocaleSetting* actionLocaleSetting()
  { return Field<"_actionLocaleSetting", ActionLocaleSetting*>(); }
  void SetActionLocaleSetting(ActionLocaleSetting* value)
  { SetField<"_actionLocaleSetting">(value); }
  ActionLocaleSettingsList* settings()
  { return Field<"_settings", ActionLocaleSettingsList*>(); }
  void SetSettings(ActionLocaleSettingsList* value)
  { SetField<"_settings">(value); }

  bool UpdateTooltipDescription()
  { return TryCall<"UpdateTooltipDescription", void()>(); }
  bool SetupActionTooltipTrigger()
  { return TryCall<"SetupActionTooltipTrigger", void()>(); }

  // The plain trigger on this GameObject; null when it is missing or is already an action trigger.
  TooltipTrigger* PlainTrigger()
  {
    auto* object  = gameObject();
    auto* trigger = object ? object->GetComponent<TooltipTrigger>() : nullptr;
    return trigger && trigger->klass == TooltipTrigger::Class::Raw() ? trigger : nullptr;
  }
  // The action trigger on this GameObject. Empty when the lookup failed, which is not the same as there being none.
  std::optional<TooltipActionTrigger*> ActionTrigger()
  {
    auto* object = gameObject();
    auto* type   = TooltipActionTrigger::Class::TypeObject();
    if (!object || !type)
      return std::nullopt;
    return object->TryCall<"GetComponent", TooltipActionTrigger*(Il2CppReflectionType*)>(type);
  }
  TooltipActionTrigger* AddActionTrigger()
  {
    auto* object = gameObject();
    return object ? object->AddComponent<TooltipActionTrigger>() : nullptr;
  }
};

// The smart scroller's card pool and layout.
struct SmartScrollerBase : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SmartScrollerBase">;

  // The spawned card transforms.
  il2cpp::List<unity::Component*>* spawnedItems()
  { return Field<"_spawnedItems", il2cpp::List<unity::Component*>*>(); }
  // Cards are still spawning while any of these is set.
  bool regeneratingContent()
  { return Field<"_regeneratingContent", bool>(); }
  il2cpp::Object* spawnerCoroutine()
  { return Field<"_spawnerCoroutine", il2cpp::Object*>(); }
  il2cpp::Object* dataHeldWhilstInitializing()
  { return Field<"_dataHeldWhilstInitializing", il2cpp::Object*>(); }

  std::optional<unity::Vector2> NormalizedPosition()
  { return TryCall<"get_NormalizedPosition", unity::Vector2()>(); }
  // Lays the visible cards out again for `position`, as a scroll to it would.
  bool OnValueChangedCallback(unity::Vector2 position)
  { return TryCall<"OnValueChangedCallback", void(unity::Vector2)>(position); }
  std::optional<int32_t> VisibleItemsCount()
  { return TryCall<"get_VisibleItemsCount", int32_t()>(); }
  std::optional<float> ScrollPosition()
  { return TryCall<"get_ScrollPosition", float()>(); }

  // The tooltip widget on a spawned card, active or not.
  static InventoryTooltipTriggerWidget* CardTooltip(unity::Component* card)
  {
    auto* type = InventoryTooltipTriggerWidget::Class::TypeObject();
    return type ? card->Call<"GetComponentInChildren", InventoryTooltipTriggerWidget*(Il2CppReflectionType*, bool)>(
                      type, true)
                : nullptr;
  }
};

// The store's context. Its members resolve on the runtime class.
struct AllianceGamesStoreContext : il2cpp::Object {
  bool IsShopViewActive()
  { return Get<"IsShopViewActive", bool>(); }
  std::optional<bool> IsFetchingBundlesInProgress()
  { return TryCall<"get_IsFetchingBundlesInProgress", bool()>(); }
  int32_t BundlesCount()
  { return Get<"BundlesCount", int32_t>(); }
  // How many soft currencies the store shows.
  std::optional<int32_t> CurrencyCount()
  {
    auto* currencies = Get<"StoreCurrency", il2cpp::Object*>();
    auto* soft       = currencies ? currencies->Get<"SoftResources", il2cpp::List<il2cpp::Object*>*>() : nullptr;
    return soft ? soft->TryCall<"get_Count", int32_t()>() : std::nullopt;
  }
  // Rebuilds the store's currency list from its bundles.
  bool UpdateStoreCurrency()
  { return TryCall<"UpdateStoreCurrency", void()>(); }
};

struct AllianceGamesStoreScrollerViewController : ShopListScrollerViewController {
  using Class =
      il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Alliances.Games", "AllianceGamesStoreScrollerViewController">;

  AllianceGamesStoreContext* AllianceGamesContext()
  { return Get<"AllianceGamesContext", AllianceGamesStoreContext*>(); }
};

ShopListScrollerViewController* BundleDataWidget::scrollerController()
{ return Field<"_scrollerController", ShopListScrollerViewController*>(); }
InventoryTooltipTriggerWidget* BundleDataWidget::invTooltip()
{ return Field<"_invTooltip", InventoryTooltipTriggerWidget*>(); }

// Shows the shop's reveal sequences and offer popups. Only hooked.
using ShopSceneManager = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopSceneManager">;

// The forbidden tech inventory tiles and the large selected-item panel.
struct ForbiddenTechInventoryItemWidget : Widget<InventoryItem> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechInventoryItemWidget">;

  // ForbiddenTechInventoryItemWidget.WidgetType.
  enum class Type : int32_t {
    ScrollerEntry = 0,
    LargeShowcase = 1,
  };

  Type widgetType()
  { return Field<"_widgetType", Type>(); }
  // SmartScroller
  unity::Component* buffListScroller()
  { return Field<"_buffListScroller", unity::Component*>(); }
  TextLocalizer* name()
  { return Field<"_name", TextLocalizer*>(); }
};

#pragma once

// The inventory, its use and purchase popups, and the shop and gift screens.

#include "server.h"
#include "ui.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

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

// Digit.Client.Serialization.OverridableBool: the serialized flag and the runtime override.
struct OverridableBool {
  bool serialized, current, modified;
};

struct TooltipTrigger : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Tooltip", "TooltipTrigger">;

  // Every appearance field, so a trigger added beside this one looks and loads the same.
  void CopyAppearance(TooltipTrigger* source)
  {
    SetField<"_showHeader">(source->Field<"_showHeader", OverridableBool>());
    SetField<"_header">(source->Field<"_header", LocaleTextContext*>());
    SetField<"_showDescription">(source->Field<"_showDescription", OverridableBool>());
    SetField<"_description">(source->Field<"_description", LocaleTextContext*>());
    SetField<"_overrideLocale">(source->Field<"_overrideLocale", bool>());
    SetField<"_anchorOverride">(source->Field<"_anchorOverride", int32_t>()); // TooltipAnchor
    SetField<"_sizeOverride">(source->Field<"_sizeOverride", unity::Vector2>());
    SetField<"_positionOverrideTarget">(source->Field<"_positionOverrideTarget", unity::Transform*>());
    SetField<"_positionOverrideOffset">(source->Field<"_positionOverrideOffset", unity::Vector2>());
    SetField<"_isInteractable">(source->Field<"_isInteractable", bool>());
    SetField<"<DescriptionOverrideText>k__BackingField">(
        source->Field<"<DescriptionOverrideText>k__BackingField", Il2CppString*>());
    SetField<"_loader">(source->Field<"_loader", il2cpp::Object*>());
  }
};

// A tooltip trigger with action buttons, such as VIEW RESEARCH.
struct TooltipActionTrigger : TooltipTrigger {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Tooltip", "TooltipActionTrigger">;
};

// The tooltip on an inventory or shop item. Refresh writes the item into `_tooltipTrigger` and sets up the actions on
// `_tooltipActionTrigger`, labelled from its action locale assets, when it has one.
struct InventoryTooltipTriggerWidget : unity::Component {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryTooltipTriggerWidget">;

  TooltipTrigger* tooltipTrigger()
  { return Field<"_tooltipTrigger", TooltipTrigger*>(); }
  TooltipActionTrigger* tooltipActionTrigger()
  { return Field<"_tooltipActionTrigger", TooltipActionTrigger*>(); }
  il2cpp::Object* searchActionLocaleSetting()
  { return Field<"_searchActionLocaleSetting", il2cpp::Object*>(); }
  // Shows the tooltip through `action`, with the action locale assets of `source`.
  void UseActionTrigger(TooltipActionTrigger* action, InventoryTooltipTriggerWidget* source)
  {
    SetField<"_tooltipTrigger">(action);
    SetField<"_tooltipActionTrigger">(action);
    SetField<"_actionLocaleSetting">(source->Field<"_actionLocaleSetting", il2cpp::Object*>());
    SetField<"_searchActionLocaleSetting">(source->searchActionLocaleSetting());
    SetField<"_settings">(source->Field<"_settings", il2cpp::Object*>());
  }
  // Private, and only its own methods can reach it, so the widget's bind path sets up the actions; which of its
  // methods calls it is not visible in the metadata.
  void SetupActionTooltipTrigger()
  { Call<"SetupActionTooltipTrigger">(); }
  void Refresh()
  { Call<"Refresh">(); }
};

// A shop offer.
struct BundleDataWidget : Widget<Bundle> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "BundleDataWidget">;

  // ItemState flags; only the one the mod reads is named.
  static constexpr int32_t kCooldownTimerOn = 16384;

  int32_t currentState()
  { return Field<"_currentState", int32_t>(); }
  InventoryTooltipTriggerWidget* invTooltip()
  { return Field<"_invTooltip", InventoryTooltipTriggerWidget*>(); }
  // The list showing the card. OnDidBindContext sets it after the card's tooltip is bound.
  il2cpp::Object* scrollerController()
  { return Field<"_scrollerController", il2cpp::Object*>(); }

  void AuxViewButtonPressedHandler()
  { Call<"AuxViewButtonPressedHandler">(); }
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

// A recycling list. Scrolling calls OnValueChangedCallback, which binds the cards in view.
struct SmartScrollerBase : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SmartScrollerBase">;

  // Regenerated content can spawn its cards over several frames.
  bool Spawning()
  {
    return Field<"_regeneratingContent", bool>() || Field<"_spawnerCoroutine", il2cpp::Object*>()
           || Field<"_dataHeldWhilstInitializing", il2cpp::Object*>();
  }
  // Binds the cards in view again, as a scroll to where the list already is would.
  void Rebind()
  { Call<"OnValueChangedCallback">(Get<"NormalizedPosition", unity::Vector2>()); }
};

// The gifts screen; its selection drawer holds bulk claim.
struct ShopListScrollerViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopListScrollerViewController">;

  DrawerWidget* selectionDrawerWidget()
  { return Field<"_selectionDrawerWidget", DrawerWidget*>(); }
  SmartScrollerBase* smartScroller()
  { return Field<"_smartScroller", SmartScrollerBase*>(); }
};

// The Alliance Games screen's context. It wraps the shop's context and keeps the store's currency list.
struct AllianceGamesSectionContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Alliances.Games", "AllianceGamesSectionContext">;

  bool HasBundles()
  { return !Get<"IsFetchingBundlesInProgress", bool>() && Get<"BundlesCount", int32_t>() > 0; }
  // Rebuilds the currency list from the store's bundles.
  void UpdateStoreCurrency()
  { Call<"UpdateStoreCurrency">(); }
};

// Its OnContextUpdatedEventCallback override is empty, so it does none of the regular store's context handling.
struct AllianceGamesStoreScrollerViewController : ShopListScrollerViewController {
  using Class =
      il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Alliances.Games", "AllianceGamesStoreScrollerViewController">;

  AllianceGamesSectionContext* AllianceGamesContext()
  { return Get<"AllianceGamesContext", AllianceGamesSectionContext*>(); }
};

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

// Alliance Games store: native material research tooltips and currency refresh
// after bundle readiness. One feature owner, no claim-trace or file probe.
#include "patches/native_hook_extent.h"
#include "patches/screen_update_hook.h"

#include <il2cpp/il2cpp.h>
#include <il2cpp/method_contract.h>
#include <prime/BundleDataWidget.h>
#include <prime/Component.h>
#include <prime/GameObject.h>
#include <prime/Hub.h>
#include <prime/LocaleTextContext.h>
#include <prime/Transform.h>
#include <prime/Vector2.h>

#include <spud/detour.h>
#if _WIN32
#include <Windows.h>
#endif
#include <spdlog/spdlog.h>
#include <thread>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

struct UnityObject {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;
};
struct UnityResources {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Resources">;
};

// Digit.Client.Serialization.OverridableBool.
struct OverridableBool {
  bool serialized, current, modified;
};
static_assert(sizeof(OverridableBool) == 3);

struct ActionLocaleSetting : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Actions", "ActionLocaleSetting">;
};
struct ActionLocaleSettingsList : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Actions", "ActionLocaleSettingsList">;
};
struct TooltipLoadAndShow : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Tooltip", "TooltipLoadAndShow">;
};

// A MonoBehaviour; the appearance fields are copied onto an action trigger that replaces it.
struct TooltipTrigger : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Tooltip", "TooltipTrigger">;

  OverridableBool showHeader()
  { return Field<"_showHeader", OverridableBool>(); }
  LocaleTextContext* header()
  { return Field<"_header", LocaleTextContext*>(); }
  OverridableBool showDescription()
  { return Field<"_showDescription", OverridableBool>(); }
  LocaleTextContext* description()
  { return Field<"_description", LocaleTextContext*>(); }
  bool overrideLocale()
  { return Field<"_overrideLocale", bool>(); }
  // Digit.Client.Tooltip.TooltipAnchor; -1 is Undefined.
  int32_t anchorOverride()
  { return Field<"_anchorOverride", int32_t>(); }
  Vector2 sizeOverride()
  { return Field<"_sizeOverride", Vector2>(); }
  Transform* positionOverrideTarget()
  { return Field<"_positionOverrideTarget", Transform*>(); }
  Vector2 positionOverrideOffset()
  { return Field<"_positionOverrideOffset", Vector2>(); }
  bool isInteractable()
  { return Field<"_isInteractable", bool>(); }
  Il2CppString* descriptionOverrideText()
  { return Field<"<DescriptionOverrideText>k__BackingField", Il2CppString*>(); }
  TooltipLoadAndShow* loader()
  { return Field<"_loader", TooltipLoadAndShow*>(); }

  // Behaviour.enabled, distinguishing a failed call from the value.
  std::optional<bool> Enabled()
  { return TryCall<"get_enabled", bool()>(); }
  bool SetEnabled(bool value)
  { return TryCall<"set_enabled", void(bool)>(value); }
};

struct TooltipActionTrigger : TooltipTrigger {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Tooltip", "TooltipActionTrigger">;

  void CopyAppearance(TooltipTrigger* plain)
  {
    SetField<"_showHeader">(plain->showHeader());
    SetField<"_header">(plain->header());
    SetField<"_showDescription">(plain->showDescription());
    SetField<"_description">(plain->description());
    SetField<"_overrideLocale">(plain->overrideLocale());
    SetField<"_anchorOverride">(plain->anchorOverride());
    SetField<"_sizeOverride">(plain->sizeOverride());
    SetField<"_positionOverrideTarget">(plain->positionOverrideTarget());
    SetField<"_positionOverrideOffset">(plain->positionOverrideOffset());
    SetField<"_isInteractable">(plain->isInteractable());
    SetField<"<DescriptionOverrideText>k__BackingField">(plain->descriptionOverrideText());
    SetField<"_loader">(plain->loader());
  }
};

// The tooltip on a shop card. Its context is the card's inventory item.
struct InventoryTooltipTriggerWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryTooltipTriggerWidget">;

  il2cpp::Object* Context()
  { return Get<"Context", il2cpp::Object*>(); }
  GameObject* gameObject()
  { return Get<"gameObject", GameObject*>(); }

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
};

// Digit.Client.UI.SmartScrollerBase, the store's card pool.
struct SmartScrollerBase : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SmartScrollerBase">;

  il2cpp::List<il2cpp::Object*>* spawnedItems()
  { return Field<"_spawnedItems", il2cpp::List<il2cpp::Object*>*>(); }
  bool regeneratingContent()
  { return Field<"_regeneratingContent", bool>(); }
  il2cpp::Object* spawnerCoroutine()
  { return Field<"_spawnerCoroutine", il2cpp::Object*>(); }
  il2cpp::Object* dataHeldWhilstInitializing()
  { return Field<"_dataHeldWhilstInitializing", il2cpp::Object*>(); }

  std::optional<Vector2> NormalizedPosition()
  { return TryCall<"get_NormalizedPosition", Vector2()>(); }
  bool OnValueChangedCallback(Vector2 value)
  { return TryCall<"OnValueChangedCallback", void(Vector2)>(value); }
  std::optional<int32_t> VisibleItemsCount()
  { return TryCall<"get_VisibleItemsCount", int32_t()>(); }
  std::optional<float> ScrollPosition()
  { return TryCall<"get_ScrollPosition", float()>(); }
};

struct ShopListScrollerViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopListScrollerViewController">;

  // Declared as some scroller; only a SmartScrollerBase is used.
  il2cpp::Object* smartScroller()
  { return Field<"_smartScroller", il2cpp::Object*>(); }
};

// The store's context; its members resolve on the runtime class.
struct AllianceGamesStoreContext : il2cpp::Object {
  bool IsShopViewActive()
  { return Get<"IsShopViewActive", bool>(); }
  std::optional<bool> IsFetchingBundlesInProgress()
  { return TryCall<"get_IsFetchingBundlesInProgress", bool()>(); }
  int32_t BundlesCount()
  { return Get<"BundlesCount", int32_t>(); }
  il2cpp::Object* StoreCurrency()
  { return Get<"StoreCurrency", il2cpp::Object*>(); }
  bool UpdateStoreCurrency()
  { return TryCall<"UpdateStoreCurrency", void()>(); }
};

struct AllianceGamesStoreScrollerViewController : ShopListScrollerViewController {
  using Class =
      il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Alliances.Games", "AllianceGamesStoreScrollerViewController">;

  AllianceGamesStoreContext* AllianceGamesContext()
  { return Get<"AllianceGamesContext", AllianceGamesStoreContext*>(); }
};

InventoryTooltipTriggerWidget* BundleTooltip(BundleDataWidget* card)
{ return card->Field<"_invTooltip", InventoryTooltipTriggerWidget*>(); }
ShopListScrollerViewController* BundleOwner(BundleDataWidget* card)
{ return card->Field<"_scrollerController", ShopListScrollerViewController*>(); }

// Upstream refuses to install when the native field contract changed: the hooks write these fields, and a missing
// one would read as zero.
template <typename Binding, il2cpp::Name N, typename T> bool StoreHasField()
{ return il2cpp::detail::ResolveField(Binding::Class::Raw(), N.View(), il2cpp::detail::SlotOf<T>(), false); }

bool StoreFieldsResolve()
{
  using Widget   = InventoryTooltipTriggerWidget;
  using Trigger  = TooltipTrigger;
  using Scroller = SmartScrollerBase;
  bool ok        = true;
  ok &= StoreHasField<Widget, "_tooltipTrigger", TooltipTrigger*>();
  ok &= StoreHasField<Widget, "_tooltipActionTrigger", TooltipActionTrigger*>();
  ok &= StoreHasField<Widget, "_searchActionLocaleSetting", ActionLocaleSetting*>();
  ok &= StoreHasField<Widget, "_actionLocaleSetting", ActionLocaleSetting*>();
  ok &= StoreHasField<Widget, "_settings", ActionLocaleSettingsList*>();
  ok &= StoreHasField<Trigger, "_showHeader", OverridableBool>();
  ok &= StoreHasField<Trigger, "_showDescription", OverridableBool>();
  ok &= StoreHasField<Trigger, "_header", LocaleTextContext*>();
  ok &= StoreHasField<Trigger, "_description", LocaleTextContext*>();
  ok &= StoreHasField<Trigger, "_overrideLocale", bool>();
  ok &= StoreHasField<Trigger, "_anchorOverride", int32_t>();
  ok &= StoreHasField<Trigger, "_sizeOverride", Vector2>();
  ok &= StoreHasField<Trigger, "_positionOverrideOffset", Vector2>();
  ok &= StoreHasField<Trigger, "_positionOverrideTarget", Transform*>();
  ok &= StoreHasField<Trigger, "_isInteractable", bool>();
  ok &= StoreHasField<Trigger, "<DescriptionOverrideText>k__BackingField", Il2CppString*>();
  ok &= StoreHasField<Trigger, "_loader", TooltipLoadAndShow*>();
  ok &= StoreHasField<ShopListScrollerViewController, "_smartScroller", il2cpp::Object*>();
  ok &= StoreHasField<Scroller, "_spawnedItems", il2cpp::List<il2cpp::Object*>*>();
  ok &= StoreHasField<BundleDataWidget, "_invTooltip", InventoryTooltipTriggerWidget*>();
  ok &= StoreHasField<BundleDataWidget, "_scrollerController", ShopListScrollerViewController*>();
  ok &= StoreHasField<Scroller, "_regeneratingContent", bool>();
  ok &= StoreHasField<Scroller, "_spawnerCoroutine", il2cpp::Object*>();
  ok &= StoreHasField<Scroller, "_dataHeldWhilstInitializing", il2cpp::Object*>();
  return ok;
}

// Unity's bool(Object): false for null and for destroyed native objects.
bool Alive(il2cpp::Object* object)
{ return object && UnityObject::Class::Call<"op_Implicit", bool>(object); }

bool IsStore(il2cpp::Object* object)
{ return object && object->Is<AllianceGamesStoreScrollerViewController>(); }

// Native research eligibility of the card's resource. Empty when it cannot be determined.
std::optional<bool> Eligible(il2cpp::Object* item, il2cpp::GcRoots& roots)
{
  auto* spec = roots.Keep(item->Get<"ResourceSpec", il2cpp::Object*>());
  auto  id   = spec ? spec->Get<"Id", int64_t>() : 0;
  if (id <= 0)
    return std::nullopt;
  auto* manager = roots.Keep(Hub::Class::Get<"ResearchManager", il2cpp::Object*>());
  // Digit.PrimeServer.Models.ResearchTreeType; 0 is the main tree.
  enum class ResearchTreeType : int32_t {};
  return manager ? manager->TryCall<"IsResourceRequiredByVisibleResearches", bool(int64_t, ResearchTreeType)>(
                       id, ResearchTreeType{})
                 : std::nullopt;
}

std::optional<int32_t> CurrencyCount(AllianceGamesStoreContext* context, il2cpp::GcRoots& roots)
{
  auto* currencies = roots.Keep(context->StoreCurrency());
  auto* soft = currencies ? roots.Keep(currencies->Get<"SoftResources", il2cpp::List<il2cpp::Object*>*>()) : nullptr;
  return soft ? soft->TryCall<"get_Count", int32_t()>() : std::nullopt;
}

// The action trigger on the widget's GameObject; with `create`, added when there is none. Null when the lookup
// failed, so a failed lookup never adds a second component.
TooltipActionTrigger* ActionComponent(InventoryTooltipTriggerWidget* widget, bool create, il2cpp::GcRoots& roots)
{
  auto* object = roots.Keep(widget->gameObject());
  auto* type   = object ? roots.Keep(TooltipActionTrigger::Class::TypeObject()) : nullptr;
  if (!type)
    return nullptr;
  const auto found = object->TryCall<"GetComponent", TooltipActionTrigger*(Il2CppReflectionType*)>(type);
  if (!found)
    return nullptr;
  if (*found)
    return roots.Keep(*found);
  return create ? roots.Keep(object->AddComponent<TooltipActionTrigger>()) : nullptr;
}

// The widget's own plain trigger, not an action trigger.
TooltipTrigger* PlainComponent(InventoryTooltipTriggerWidget* widget, il2cpp::GcRoots& roots)
{
  auto* object = roots.Keep(widget->gameObject());
  auto* value  = object ? roots.Keep(object->GetComponent<TooltipTrigger>()) : nullptr;
  return value && value->klass == TooltipTrigger::Class::Raw() ? value : nullptr;
}

// A loaded tooltip widget that carries native research actions, whose locale settings the store cards borrow.
InventoryTooltipTriggerWidget* Template(il2cpp::GcRoots& roots)
{
  auto* type  = roots.Keep(InventoryTooltipTriggerWidget::Class::TypeObject());
  auto* found = type ? roots.Keep(UnityResources::Class::Call<"FindObjectsOfTypeAll", il2cpp::Array<il2cpp::Object*>*>(
                           type))
                     : nullptr;
  if (!found)
    return nullptr;
  for (auto* object : *found) {
    auto* widget = object ? object->As<InventoryTooltipTriggerWidget>() : nullptr;
    if (widget && widget->tooltipActionTrigger() && widget->searchActionLocaleSetting())
      return roots.Keep(widget);
  }
  return nullptr;
}

bool            alliance_store_active = false;
std::thread::id alliance_store_thread;
bool            StoreThread()
{ return alliance_store_active && std::this_thread::get_id() == alliance_store_thread; }

struct AllianceStoreWork {
  il2cpp::GcHandle  source;
  uint64_t          epoch   = 0;
  bool              pending = false, currency = false, refreshing_currency = false, layout = false;
  Clock::time_point due{};
} alliance_store_work;

enum class ResearchUpdate { Unchanged, Upgraded, Restored, Unavailable, Failed };

bool SameStoreItem(InventoryTooltipTriggerWidget* tooltip, il2cpp::Object* item)
{ return Alive(tooltip) && tooltip->Context() == item; }

// Weak component identity survives pooled reuse without retaining a scene. Only
// the original serialized assets stay rooted while our replacement is bound.
struct OwnedStoreTooltip {
  il2cpp::GcHandle widget, plain, action, store;
  il2cpp::GcHandle search, locale, settings;
  bool             bound = false, plain_enabled = true, retiring = false;
  uint64_t         generation = 0;
};
std::vector<std::shared_ptr<OwnedStoreTooltip>> alliance_store_tooltips;

std::shared_ptr<OwnedStoreTooltip> OwnedTooltip(InventoryTooltipTriggerWidget* tooltip)
{
  for (auto& entry : alliance_store_tooltips)
    if (entry->widget.Get() == tooltip)
      return entry;
  return nullptr;
}

void ReleaseStoreAssets(OwnedStoreTooltip& entry)
{
  entry.bound = false;
  entry.search.Reset();
  entry.locale.Reset();
  entry.settings.Reset();
}

bool RestoreStoreTooltip(OwnedStoreTooltip& entry)
{
  if (!entry.bound)
    return true;
  if (entry.retiring)
    return false;
  ++entry.generation; // Any in-flight setup loses publication authority.
  entry.retiring = true;
  struct ClearRetiring {
    bool& value;
    ~ClearRetiring()
    { value = false; }
  } clear{entry.retiring};
  il2cpp::GcRoots roots;
  auto*           widget = roots.Keep(entry.widget.Get<InventoryTooltipTriggerWidget>());
  auto*           plain  = roots.Keep(entry.plain.Get<TooltipTrigger>());
  auto*           action = roots.Keep(entry.action.Get<TooltipActionTrigger>());
  if (!roots.healthy() || !widget || !plain || !action)
    return false;
  if (!Alive(widget)) {
    ReleaseStoreAssets(entry);
    return true;
  }
  auto* trigger = widget->tooltipTrigger();
  if ((trigger != plain && trigger != action)
      || (widget->tooltipActionTrigger() && widget->tooltipActionTrigger() != action)) {
    // A new native owner replaced our arrangement. Disable only our component;
    // never overwrite the replacement owner's fields or enabled state.
    (void)action->SetEnabled(false);
    ReleaseStoreAssets(entry);
    return true;
  }
  bool ok = action->SetEnabled(false);
  widget->SetTooltipTrigger(plain);
  widget->SetTooltipActionTrigger(nullptr);
  widget->SetSearchActionLocaleSetting(entry.search.Get<ActionLocaleSetting>());
  widget->SetActionLocaleSetting(entry.locale.Get<ActionLocaleSetting>());
  widget->SetSettings(entry.settings.Get<ActionLocaleSettingsList>());
  ok &= widget->UpdateTooltipDescription();
  ok &= plain->SetEnabled(entry.plain_enabled);
  if (ok)
    ReleaseStoreAssets(entry);
  return ok;
}

ResearchUpdate ReconcileStoreTooltip(InventoryTooltipTriggerWidget* tooltip, InventoryTooltipTriggerWidget*& donor,
                                     il2cpp::GcRoots& pass_roots)
{
  il2cpp::GcRoots roots;
  if (!roots.Keep(tooltip))
    return ResearchUpdate::Unavailable;
  const auto epoch     = alliance_store_work.epoch;
  auto*      store     = roots.Keep(alliance_store_work.source.Get<AllianceGamesStoreScrollerViewController>());
  auto*      item      = Alive(tooltip) ? roots.Keep(tooltip->Context()) : nullptr;
  auto       same_item = [&] {
    if (!store || alliance_store_work.epoch != epoch || alliance_store_work.source.Get() != store)
      return false;
    return Alive(store) && SameStoreItem(tooltip, item) && alliance_store_work.epoch == epoch
           && alliance_store_work.source.Get() == store;
  };
  auto* plain = item ? PlainComponent(tooltip, roots) : nullptr;
  if (!plain)
    return ResearchUpdate::Unchanged;
  auto* old_action  = roots.Keep(tooltip->tooltipActionTrigger());
  auto* old_trigger = roots.Keep(tooltip->tooltipTrigger());
  auto  owned       = OwnedTooltip(tooltip);
  // Existing native action components and unrelated arrangements remain native.
  if (!roots.healthy() || (old_action && (!owned || owned->action.Get() != old_action))
      || (old_action ? old_trigger != old_action : old_trigger != plain))
    return ResearchUpdate::Unchanged;
  const auto eligible = Eligible(item, roots);
  if (!eligible || !same_item())
    return ResearchUpdate::Unavailable;
  if (!*eligible)
    return owned && owned->bound ? (RestoreStoreTooltip(*owned) ? ResearchUpdate::Restored : ResearchUpdate::Failed)
                                 : ResearchUpdate::Unchanged;
  if (!donor)
    donor = Template(pass_roots);
  if (!donor)
    return ResearchUpdate::Unavailable;
  auto*      search        = roots.Keep(donor->searchActionLocaleSetting());
  auto*      locale        = roots.Keep(donor->actionLocaleSetting());
  auto*      settings      = roots.Keep(donor->settings());
  auto*      loader        = roots.Keep(plain->loader());
  const auto plain_enabled = plain->Enabled();
  if (!roots.healthy() || !search || !loader || !plain_enabled || !same_item())
    return ResearchUpdate::Unavailable;
  auto* action = ActionComponent(tooltip, false, roots);
  // Reuse only the disabled component this feature previously added.
  if (action && (!owned || owned->action.Get() != action))
    return ResearchUpdate::Unchanged;
  if (!action)
    action = ActionComponent(tooltip, true, roots);
  if (!action)
    return ResearchUpdate::Unavailable;
  if (!owned) {
    auto entry    = std::make_shared<OwnedStoreTooltip>();
    entry->widget = il2cpp::GcHandle::Weak(tooltip);
    entry->plain  = il2cpp::GcHandle::Weak(plain);
    entry->action = il2cpp::GcHandle::Weak(action);
    if (entry->widget.Get() != tooltip || entry->plain.Get() != plain || entry->action.Get() != action) {
      (void)action->SetEnabled(false);
      return ResearchUpdate::Unavailable;
    }
    alliance_store_tooltips.push_back(std::move(entry));
    owned = alliance_store_tooltips.back();
  }
  const bool previously_bound = owned->bound;
  if (!owned->bound) {
    auto retain = [](il2cpp::Object* value, il2cpp::GcHandle& handle) {
      handle = il2cpp::GcHandle::Strong(value);
      return !value || handle.Get() == value;
    };
    if (!retain(tooltip->searchActionLocaleSetting(), owned->search)
        || !retain(tooltip->actionLocaleSetting(), owned->locale) || !retain(tooltip->settings(), owned->settings)) {
      ReleaseStoreAssets(*owned);
      (void)action->SetEnabled(false);
      return ResearchUpdate::Unavailable;
    }
    owned->store = il2cpp::GcHandle::Weak(store);
    if (owned->store.Get() != store) {
      ReleaseStoreAssets(*owned);
      (void)action->SetEnabled(false);
      return ResearchUpdate::Unavailable;
    }
    owned->plain_enabled = *plain_enabled;
    owned->bound         = true;
  }
  const auto generation = ++owned->generation;
  auto current = [&] { return same_item() && owned->bound && !owned->retiring && owned->generation == generation; };
  struct Rollback {
    std::shared_ptr<OwnedStoreTooltip> entry;
    bool                               committed = false;
    ~Rollback()
    {
      if (!committed)
        (void)RestoreStoreTooltip(*entry);
    }
  } rollback{owned};

  if (!action->SetEnabled(false))
    return ResearchUpdate::Failed;
  action->CopyAppearance(plain);
  if (!current())
    return ResearchUpdate::Failed;
  tooltip->SetSearchActionLocaleSetting(search);
  tooltip->SetActionLocaleSetting(locale);
  tooltip->SetSettings(settings);
  tooltip->SetTooltipTrigger(action);
  tooltip->SetTooltipActionTrigger(action);
  if (!tooltip->UpdateTooltipDescription() || !current() || !tooltip->SetupActionTooltipTrigger() || !current()
      || !plain->SetEnabled(false) || !action->SetEnabled(true) || !current())
    return ResearchUpdate::Failed;
  const auto enabled = action->Enabled();
  if (!current() || !enabled || !*enabled || tooltip->tooltipTrigger() != action
      || tooltip->tooltipActionTrigger() != action)
    return ResearchUpdate::Failed;
  rollback.committed = true;
  return previously_bound ? ResearchUpdate::Unchanged : ResearchUpdate::Upgraded;
}

void QueueStore(AllianceGamesStoreScrollerViewController* source, bool currency)
{
  auto& work = alliance_store_work;
  if (work.source.Get() != source) {
    work.source = il2cpp::GcHandle::Weak(source);
    ++work.epoch;
  }
  if (work.source.Get() != source)
    return;
  // Debounce through separate-frame native card spawning.
  work.due     = Clock::now() + std::chrono::milliseconds(250);
  work.pending = true;
  work.currency |= currency && !work.refreshing_currency;
  work.layout |= currency && !work.refreshing_currency;
}

void AllianceStoreScroller_OnDidBindCanvasContext_Hook(auto original, AllianceGamesStoreScrollerViewController* self)
{
  if (!StoreThread()) {
    original(self);
    return;
  }
  original(self);
  std::erase_if(alliance_store_tooltips, [](auto& entry) { return !entry->widget.Get(); });
  if (IsStore(self))
    QueueStore(self, true);
}

void AllianceStoreScroller_OnAboutToReleaseCanvasContext_Hook(auto                                      original,
                                                              AllianceGamesStoreScrollerViewController* self)
{
  if (!StoreThread()) {
    original(self);
    return;
  }
  auto& work = alliance_store_work;
  if (work.source.Get() == self) {
    work.source.Reset();
    work.pending = work.currency = work.layout = false;
    ++work.epoch;
  }
  // Native restoration can reenter bind/pruning. Keep stable entry custody and
  // invalidate the source before the first such callback.
  const auto entries = alliance_store_tooltips;
  for (auto& entry : entries)
    if (entry->store.Get() == self)
      (void)RestoreStoreTooltip(*entry);
  original(self);
}

void ShopListScrollerViewController_RegenerateSortedBundles_Hook(auto original, ShopListScrollerViewController* self,
                                                                 bool instant)
{
  if (!StoreThread()) {
    original(self, instant);
    return;
  }
  original(self, instant);
  if (IsStore(self) && !alliance_store_work.refreshing_currency)
    QueueStore(static_cast<AllianceGamesStoreScrollerViewController*>(self), true);
}

void BundleDataWidget_OnAboutToReleaseContext_Hook(auto original, BundleDataWidget* self)
{
  if (!StoreThread()) {
    original(self);
    return;
  }
  if (auto entry = OwnedTooltip(BundleTooltip(self)))
    (void)RestoreStoreTooltip(*entry);
  original(self);
}

void QueueStoreCard(BundleDataWidget* self)
{
  // Native OnDidBindContext sets this AFTER SetupTooltip. Never use the
  // RefreshBundle argument or a pre-bind owner as current-card identity.
  auto* owner = BundleOwner(self);
  if (owner && alliance_store_work.source.Get() == owner && IsStore(owner))
    QueueStore(static_cast<AllianceGamesStoreScrollerViewController*>(owner), false);
}
void BundleDataWidget_RefreshBundle_Hook(auto original, BundleDataWidget* self, Bundle* bundle)
{
  if (!StoreThread()) {
    original(self, bundle);
    return;
  }
  original(self, bundle);
  QueueStoreCard(self);
}
void BundleDataWidget_OnDidBindContext_Hook(auto original, BundleDataWidget* self)
{
  if (!StoreThread()) {
    original(self);
    return;
  }
  original(self);
  QueueStoreCard(self);
}

std::string StoreCount(const auto& value)
{ return value ? std::to_string(*value) : "unavailable"; }

void AllianceStoreFrame()
{
  if (!StoreThread())
    return;
  auto& work = alliance_store_work;
  if (!work.pending || Clock::now() < work.due)
    return;
  const bool currency = std::exchange(work.currency, false);
  const bool layout   = std::exchange(work.layout, false);
  work.pending        = false;
  const auto      epoch = work.epoch;
  il2cpp::GcRoots roots;
  auto*           source  = roots.Keep(work.source.Get<AllianceGamesStoreScrollerViewController>());
  auto*           context = source && Alive(source) ? roots.Keep(source->AllianceGamesContext()) : nullptr;
  if (!context || !context->IsShopViewActive())
    return;
  if (work.epoch != epoch || work.source.Get() != source)
    return;
  auto* scroller = roots.Keep(source->smartScroller());
  auto* smart    = scroller ? scroller->As<SmartScrollerBase>() : nullptr;
  if (smart && (smart->regeneratingContent() || smart->spawnerCoroutine() || smart->dataHeldWhilstInitializing())) {
    work.pending = true;
    work.currency |= currency;
    work.layout |= layout;
    work.due = Clock::now() + std::chrono::milliseconds(250);
    return;
  }
  bool position_notified = false;
  if (layout && smart) {
    const auto position = smart->NormalizedPosition();
    if (position && Alive(smart) && work.epoch == epoch && work.source.Get() == source
        && source->smartScroller() == smart) {
      position_notified = smart->OnValueChangedCallback(*position);
    }
  }
  if (work.epoch != epoch || work.source.Get() != source || source->smartScroller() != scroller)
    return;
  auto*      pool  = smart ? roots.Keep(smart->spawnedItems()) : nullptr;
  const auto count = pool ? pool->TryCall<"get_Count", int32_t()>() : std::nullopt;
  if (!count || *count < 0)
    return;
  int                            upgraded = 0, restored = 0, unavailable = 0, failed = 0;
  InventoryTooltipTriggerWidget* donor        = nullptr;
  auto*                          tooltip_type = roots.Keep(InventoryTooltipTriggerWidget::Class::TypeObject());
  if (!tooltip_type)
    return;
  for (int32_t index = 0; index < *count; ++index) {
    if (work.epoch != epoch || work.source.Get() != source)
      return;
    il2cpp::GcRoots card_roots;
    auto*           rect = card_roots.Keep((*pool)[index]);
    if (!rect || !Alive(rect))
      continue;
    auto* tooltip =
        card_roots.Keep(rect->Call<"GetComponentInChildren", InventoryTooltipTriggerWidget*(Il2CppReflectionType*, bool)>(
            tooltip_type, true));
    switch (tooltip ? ReconcileStoreTooltip(tooltip, donor, roots) : ResearchUpdate::Unchanged) {
      case ResearchUpdate::Upgraded:
        ++upgraded;
        break;
      case ResearchUpdate::Restored:
        ++restored;
        break;
      case ResearchUpdate::Unavailable:
        ++unavailable;
        break;
      case ResearchUpdate::Failed:
        ++failed;
        break;
      default:
        break;
    }
  }
  bool                   refreshed = false;
  std::optional<int32_t> currency_before, currency_after;
  // Native bundle-derived discovery, not a fixed list of currency IDs. An
  // empty/loading store waits for its real data/regeneration event.
  const auto fetching = context->IsFetchingBundlesInProgress();
  if (currency && work.epoch == epoch && work.source.Get() == source && fetching && !*fetching
      && context->BundlesCount() > 0) {
    work.refreshing_currency = true;
    struct Reset {
      bool& value;
      ~Reset()
      { value = false; }
    } reset{work.refreshing_currency};
    currency_before = CurrencyCount(context, roots);
    if (source->AllianceGamesContext() == context && Alive(source) && work.epoch == epoch
        && work.source.Get() == source) {
      refreshed      = context->UpdateStoreCurrency();
      currency_after = CurrencyCount(context, roots);
    }
  }
  if (layout)
    spdlog::info("[AllianceGamesStore] settled layout notification={} (position unchanged)", position_notified);
  const auto visible  = smart ? smart->VisibleItemsCount() : std::nullopt;
  const auto position = smart ? smart->ScrollPosition() : std::nullopt;
  spdlog::info("[AllianceGamesStore] pool={} upgraded={} restored={} unavailable={} failed={} currency_refresh={} "
               "currencies={}->{} visible={} position={}",
               *count, upgraded, restored, unavailable, failed, refreshed, StoreCount(currency_before),
               StoreCount(currency_after), StoreCount(visible), StoreCount(position));
}

bool StoreHookFits(const MethodInfo* method)
{
  if (!method || !method->methodPointer)
    return false;
  auto* entry = reinterpret_cast<void*>(method->methodPointer);
#if _WIN32 && defined(_M_X64)
  DWORD64 base     = 0;
  auto*   function = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entry), &base, nullptr);
  if (!function || base + function->BeginAddress != reinterpret_cast<DWORD64>(entry)
      || function->EndAddress <= function->BeginAddress)
    return false;
  const size_t extent = function->EndAddress - function->BeginAddress;
  const auto   plan   = spud::inspect_detour(entry, spud::Arch::kHost, extent);
  return plan.ok() && plan.resolved_target == reinterpret_cast<uintptr_t>(entry) && plan.far_jump_size != 0
         && plan.far_required_patch_bytes >= plan.far_jump_size && plan.far_required_patch_bytes <= extent;
#elif __APPLE__
  return native_hooks::MacHookFits(entry);
#else
  return false;
#endif
}
} // namespace

void InstallAllianceGamesStoreHooks()
{
  static bool attempted = false;
  if (std::exchange(attempted, true))
    return;
  auto*            store = AllianceGamesStoreScrollerViewController::Class::Raw();
  auto*            shop  = ShopListScrollerViewController::Class::Raw();
  auto*            card  = BundleDataWidget::Class::Raw();
  const std::array targets{
      method_contract::Resolve(store, "OnDidBindCanvasContext", false, "System.Void", {}),
      method_contract::Resolve(store, "OnAboutToReleaseCanvasContext", false, "System.Void", {}),
      method_contract::Resolve(shop, "RegenerateSortedBundles", false, "System.Void", {"System.Boolean"}),
      method_contract::Resolve(card, "RefreshBundle", false, "System.Void", {"Digit.PrimePlatform.Content.Bundle"}),
      method_contract::Resolve(card, "OnAboutToReleaseContext", false, "System.Void", {}),
      method_contract::Resolve(card, "OnDidBindContext", false, "System.Void", {})};
  if (!StoreFieldsResolve()) {
    spdlog::warn("[AllianceGamesStore] unavailable: native field contract changed");
    return;
  }
  for (size_t i = 0; i < targets.size(); ++i) {
    if (!StoreHookFits(targets[i])) {
      spdlog::warn("[AllianceGamesStore] unavailable: hook signature/extent {}", i);
      return;
    }
    for (size_t j = 0; j < i; ++j)
      if (targets[i]->methodPointer == targets[j]->methodPointer) {
        spdlog::warn("[AllianceGamesStore] unavailable: shared hook target");
        return;
      }
  }
  if (!install_screen_manager_update_hook() || !register_screen_manager_update_callback(AllianceStoreFrame)) {
    spdlog::warn("[AllianceGamesStore] unavailable: shared frame delivery");
    return;
  }
  // Old SPUD has no hook transaction/rollback API. Preflight every target first,
  // attempt once, and leave all installed callbacks native-only unless complete.
  const bool complete =
      SPUD_STATIC_DETOUR(method_contract::Pointer(targets[0]), AllianceStoreScroller_OnDidBindCanvasContext_Hook)
      && SPUD_STATIC_DETOUR(method_contract::Pointer(targets[1]),
                            AllianceStoreScroller_OnAboutToReleaseCanvasContext_Hook)
      && SPUD_STATIC_DETOUR(method_contract::Pointer(targets[2]),
                            ShopListScrollerViewController_RegenerateSortedBundles_Hook)
      && SPUD_STATIC_DETOUR(method_contract::Pointer(targets[3]), BundleDataWidget_RefreshBundle_Hook)
      && SPUD_STATIC_DETOUR(method_contract::Pointer(targets[4]), BundleDataWidget_OnAboutToReleaseContext_Hook)
      && SPUD_STATIC_DETOUR(method_contract::Pointer(targets[5]), BundleDataWidget_OnDidBindContext_Hook);
  alliance_store_thread = std::this_thread::get_id();
  alliance_store_active = complete;
  if (!complete)
    spdlog::warn("[AllianceGamesStore] unavailable: incomplete detour installation; adapters inactive");
}

// alliance_store: native material research tooltips in the Alliance Games store, and its currency refresh once its
// bundles are ready. One feature owner, no claim-trace or file probe. [patches] alliancegamesstorehooks.

#include "features/features.h"

#include "game/app.h"
#include "game/inventory.h"
#include "game/native_hook_extent.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <il2cpp/method_contract.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>
#include <unity/core.h>

#if _WIN32
#include <Windows.h>
#endif

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

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

bool IsStore(il2cpp::Object* object)
{ return object && object->Is<AllianceGamesStoreScrollerViewController>(); }

bool SameStoreItem(InventoryTooltipTriggerWidget* tooltip, TooltipItem* item)
{ return unity::Alive(tooltip) && tooltip->Context() == item; }

// Native research eligibility of the card's resource. Empty when it cannot be determined.
std::optional<bool> Eligible(TooltipItem* item)
{
  const auto id = item->ResourceId();
  if (id <= 0)
    return std::nullopt;
  auto* manager = Hub::Research();
  return manager ? manager->IsResourceRequiredByVisibleResearches(id) : std::nullopt;
}

// A loaded tooltip widget that carries native research actions, whose locale settings the store cards borrow.
InventoryTooltipTriggerWidget* Template(il2cpp::GcRoots& roots)
{
  auto* found = roots.Keep(unity::FindObjectsOfTypeAll<InventoryTooltipTriggerWidget>());
  if (!found)
    return nullptr;
  for (auto* widget : *found)
    if (widget && widget->Is<InventoryTooltipTriggerWidget>() && widget->tooltipActionTrigger()
        && widget->searchActionLocaleSetting())
      return roots.Keep(widget);
  return nullptr;
}

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
  if (!unity::Alive(widget)) {
    ReleaseStoreAssets(entry);
    return true;
  }
  auto* trigger = widget->tooltipTrigger();
  if ((trigger != plain && trigger != action)
      || (widget->tooltipActionTrigger() && widget->tooltipActionTrigger() != action)) {
    // A new native owner replaced our arrangement. Disable only our component;
    // never overwrite the replacement owner's fields or enabled state.
    (void)action->TrySetEnabled(false);
    ReleaseStoreAssets(entry);
    return true;
  }
  bool ok = action->TrySetEnabled(false);
  widget->SetTooltipTrigger(plain);
  widget->SetTooltipActionTrigger(nullptr);
  widget->SetSearchActionLocaleSetting(entry.search.Get<ActionLocaleSetting>());
  widget->SetActionLocaleSetting(entry.locale.Get<ActionLocaleSetting>());
  widget->SetSettings(entry.settings.Get<ActionLocaleSettingsList>());
  ok &= widget->UpdateTooltipDescription();
  ok &= plain->TrySetEnabled(entry.plain_enabled);
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
  auto*      item      = unity::Alive(tooltip) ? roots.Keep(tooltip->Context()) : nullptr;
  auto       same_item = [&] {
    if (!store || alliance_store_work.epoch != epoch || alliance_store_work.source.Get() != store)
      return false;
    return unity::Alive(store) && SameStoreItem(tooltip, item) && alliance_store_work.epoch == epoch
           && alliance_store_work.source.Get() == store;
  };
  auto* plain = item ? roots.Keep(tooltip->PlainTrigger()) : nullptr;
  if (!plain)
    return ResearchUpdate::Unchanged;
  auto* old_action  = roots.Keep(tooltip->tooltipActionTrigger());
  auto* old_trigger = roots.Keep(tooltip->tooltipTrigger());
  auto  owned       = OwnedTooltip(tooltip);
  // Existing native action components and unrelated arrangements remain native.
  if (!roots.healthy() || (old_action && (!owned || owned->action.Get() != old_action))
      || (old_action ? old_trigger != old_action : old_trigger != plain))
    return ResearchUpdate::Unchanged;
  const auto eligible = Eligible(item);
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
  const auto plain_enabled = plain->TryEnabled();
  if (!roots.healthy() || !search || !loader || !plain_enabled || !same_item())
    return ResearchUpdate::Unavailable;
  // A failed lookup must not add a second component.
  const auto existing = tooltip->ActionTrigger();
  if (!existing)
    return ResearchUpdate::Unavailable;
  auto* action = roots.Keep(*existing);
  // Reuse only the disabled component this feature previously added.
  if (action && (!owned || owned->action.Get() != action))
    return ResearchUpdate::Unchanged;
  if (!action)
    action = roots.Keep(tooltip->AddActionTrigger());
  if (!action)
    return ResearchUpdate::Unavailable;
  if (!owned) {
    auto entry    = std::make_shared<OwnedStoreTooltip>();
    entry->widget = il2cpp::GcHandle::Weak(tooltip);
    entry->plain  = il2cpp::GcHandle::Weak(plain);
    entry->action = il2cpp::GcHandle::Weak(action);
    if (entry->widget.Get() != tooltip || entry->plain.Get() != plain || entry->action.Get() != action) {
      (void)action->TrySetEnabled(false);
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
      (void)action->TrySetEnabled(false);
      return ResearchUpdate::Unavailable;
    }
    owned->store = il2cpp::GcHandle::Weak(store);
    if (owned->store.Get() != store) {
      ReleaseStoreAssets(*owned);
      (void)action->TrySetEnabled(false);
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

  if (!action->TrySetEnabled(false))
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
      || !plain->TrySetEnabled(false) || !action->TrySetEnabled(true) || !current())
    return ResearchUpdate::Failed;
  const auto enabled = action->TryEnabled();
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
  if (auto entry = OwnedTooltip(self->invTooltip()))
    (void)RestoreStoreTooltip(*entry);
  original(self);
}

void QueueStoreCard(BundleDataWidget* self)
{
  // Native OnDidBindContext sets this AFTER SetupTooltip. Never use the
  // RefreshBundle argument or a pre-bind owner as current-card identity.
  auto* owner = self->scrollerController();
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

void AllianceGamesStoreFrame()
{
  if (!StoreThread())
    return;
  auto& work = alliance_store_work;
  if (!work.pending || Clock::now() < work.due)
    return;
  const bool currency   = std::exchange(work.currency, false);
  const bool layout     = std::exchange(work.layout, false);
  work.pending          = false;
  const auto      epoch = work.epoch;
  il2cpp::GcRoots roots;
  auto*           source  = roots.Keep(work.source.Get<AllianceGamesStoreScrollerViewController>());
  auto*           context = source && unity::Alive(source) ? roots.Keep(source->AllianceGamesContext()) : nullptr;
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
    if (position && unity::Alive(smart) && work.epoch == epoch && work.source.Get() == source
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
  InventoryTooltipTriggerWidget* donor = nullptr;
  for (int32_t index = 0; index < *count; ++index) {
    if (work.epoch != epoch || work.source.Get() != source)
      return;
    il2cpp::GcRoots card_roots;
    auto*           card = card_roots.Keep((*pool)[index]);
    if (!card || !unity::Alive(card))
      continue;
    auto* tooltip = card_roots.Keep(SmartScrollerBase::CardTooltip(card));
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
    currency_before = context->CurrencyCount();
    if (source->AllianceGamesContext() == context && unity::Alive(source) && work.epoch == epoch
        && work.source.Get() == source) {
      refreshed      = context->UpdateStoreCurrency();
      currency_after = context->CurrencyCount();
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

void InstallAllianceGamesStore()
{
  using il2cpp::contract::Resolve;
  auto*            store = AllianceGamesStoreScrollerViewController::Class::Raw();
  auto*            shop  = ShopListScrollerViewController::Class::Raw();
  auto*            card  = BundleDataWidget::Class::Raw();
  const std::array targets{Resolve(store, "OnDidBindCanvasContext", false, "System.Void", {}),
                           Resolve(store, "OnAboutToReleaseCanvasContext", false, "System.Void", {}),
                           Resolve(shop, "RegenerateSortedBundles", false, "System.Void", {"System.Boolean"}),
                           Resolve(card, "RefreshBundle", false, "System.Void", {"Digit.PrimePlatform.Content.Bundle"}),
                           Resolve(card, "OnAboutToReleaseContext", false, "System.Void", {}),
                           Resolve(card, "OnDidBindContext", false, "System.Void", {})};
  if (!AllianceGamesStoreFieldsPresent()) {
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
  // Every target is checked first; the detours are attempted once, and the hooks stay native-only unless all went
  // in. The frame work runs from the frame tick; without the tick the hooks only record pending work.
  const bool complete =
      IL2CPP_DETOUR(targets[0]->methodPointer, AllianceStoreScroller_OnDidBindCanvasContext_Hook,
                    "AllianceGamesStoreScrollerViewController.OnDidBindCanvasContext")
      && IL2CPP_DETOUR(targets[1]->methodPointer, AllianceStoreScroller_OnAboutToReleaseCanvasContext_Hook,
                       "AllianceGamesStoreScrollerViewController.OnAboutToReleaseCanvasContext")
      && IL2CPP_DETOUR(targets[2]->methodPointer, ShopListScrollerViewController_RegenerateSortedBundles_Hook,
                       "ShopListScrollerViewController.RegenerateSortedBundles")
      && IL2CPP_DETOUR(targets[3]->methodPointer, BundleDataWidget_RefreshBundle_Hook, "BundleDataWidget.RefreshBundle")
      && IL2CPP_DETOUR(targets[4]->methodPointer, BundleDataWidget_OnAboutToReleaseContext_Hook,
                       "BundleDataWidget.OnAboutToReleaseContext")
      && IL2CPP_DETOUR(targets[5]->methodPointer, BundleDataWidget_OnDidBindContext_Hook,
                       "BundleDataWidget.OnDidBindContext");
  alliance_store_thread = std::this_thread::get_id();
  alliance_store_active = complete;
  if (!complete)
    spdlog::warn("[AllianceGamesStore] unavailable: incomplete detour installation; adapters inactive");
}
} // namespace

const Feature kAllianceGamesStore{"AllianceGamesStore", &Config::Patches::alliancegamesstorehooks,
                                  InstallAllianceGamesStore, AllianceGamesStoreFrame};

// [patches] alliancegamesstorehooks: the Alliance Games store. Material offers get the native Research actions in
// their tooltips, and the store's currencies and visible offers refresh once its bundles are ready.

#include "features/features.h"

#include "game/app.h"
#include "game/inventory.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/core.h>

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
using StoreClock = std::chrono::steady_clock;

// --- Pending work ---

// The thread that installed the hooks, the game's main thread. The hooks leave calls from any other thread alone.
std::thread::id alliance_store_thread;
bool            OnStoreThread()
{ return std::this_thread::get_id() == alliance_store_thread; }

// The store waiting for its debounced pass. The source is weak, so a closed store is not kept alive, and `epoch`
// changes with it, so a pass that began for one store stops when another takes its place.
struct AllianceStoreWork {
  il2cpp::GcHandle       source;
  uint64_t               epoch   = 0;
  bool                   pending = false, currency = false, refreshing_currency = false, layout = false;
  StoreClock::time_point due{};
} alliance_store_work;

// --- Tooltip swap ---

enum class StoreTooltipUpdate { Unchanged, Upgraded, Restored, Unavailable, Failed };

bool IsAllianceStore(il2cpp::Object* object)
{ return object && object->Is<AllianceGamesStoreScrollerViewController>(); }

bool SameStoreItem(InventoryTooltipTriggerWidget* tooltip, TooltipItem* item)
{ return unity::Alive(tooltip) && tooltip->Context() == item; }

// Native research eligibility of the card's resource. Empty when it cannot be determined.
std::optional<bool> ResearchEligible(TooltipItem* item)
{
  const auto id = item->ResourceId();
  if (id <= 0)
    return std::nullopt;
  auto* manager = Hub::Research();
  return manager ? manager->IsResourceRequiredByVisibleResearches(id) : std::nullopt;
}

// A loaded tooltip widget that carries native research actions, whose locale settings the store cards borrow.
InventoryTooltipTriggerWidget* ResearchTemplate(il2cpp::GcRoots& roots)
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

// Weak component identity survives pooled reuse without retaining a scene. Only the original serialized assets stay
// rooted while our replacement is bound.
struct OwnedStoreTooltip {
  il2cpp::GcHandle widget, plain, action, store;
  il2cpp::GcHandle search, locale, settings;
  bool             bound = false, plain_enabled = true, retiring = false;
  uint64_t         generation = 0;
};
std::vector<std::shared_ptr<OwnedStoreTooltip>> alliance_store_tooltips;

std::shared_ptr<OwnedStoreTooltip> FindOwnedTooltip(InventoryTooltipTriggerWidget* tooltip)
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
    // A new native owner replaced our arrangement. Disable only our component; never overwrite the replacement owner's
    // fields or enabled state.
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

StoreTooltipUpdate ReconcileStoreTooltip(InventoryTooltipTriggerWidget* tooltip, InventoryTooltipTriggerWidget*& donor,
                                         il2cpp::GcRoots& pass_roots)
{
  il2cpp::GcRoots roots;
  if (!roots.Keep(tooltip))
    return StoreTooltipUpdate::Unavailable;
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
    return StoreTooltipUpdate::Unchanged;
  auto* old_action  = roots.Keep(tooltip->tooltipActionTrigger());
  auto* old_trigger = roots.Keep(tooltip->tooltipTrigger());
  auto  owned       = FindOwnedTooltip(tooltip);
  // Existing native action components and unrelated arrangements remain native.
  if (!roots.healthy() || (old_action && (!owned || owned->action.Get() != old_action))
      || (old_action ? old_trigger != old_action : old_trigger != plain))
    return StoreTooltipUpdate::Unchanged;
  const auto eligible = ResearchEligible(item);
  if (!eligible || !same_item())
    return StoreTooltipUpdate::Unavailable;
  if (!*eligible)
    return owned && owned->bound
               ? (RestoreStoreTooltip(*owned) ? StoreTooltipUpdate::Restored : StoreTooltipUpdate::Failed)
               : StoreTooltipUpdate::Unchanged;
  if (!donor)
    donor = ResearchTemplate(pass_roots);
  if (!donor)
    return StoreTooltipUpdate::Unavailable;
  auto*      search        = roots.Keep(donor->searchActionLocaleSetting());
  auto*      locale        = roots.Keep(donor->actionLocaleSetting());
  auto*      settings      = roots.Keep(donor->settings());
  auto*      loader        = roots.Keep(plain->loader());
  const auto plain_enabled = plain->TryEnabled();
  if (!roots.healthy() || !search || !loader || !plain_enabled || !same_item())
    return StoreTooltipUpdate::Unavailable;
  // A failed lookup must not add a second component.
  const auto existing = tooltip->ActionTrigger();
  if (!existing)
    return StoreTooltipUpdate::Unavailable;
  auto* action = roots.Keep(*existing);
  // Reuse only the disabled component this feature previously added.
  if (action && (!owned || owned->action.Get() != action))
    return StoreTooltipUpdate::Unchanged;
  if (!action)
    action = roots.Keep(tooltip->AddActionTrigger());
  if (!action)
    return StoreTooltipUpdate::Unavailable;
  if (!owned) {
    auto entry    = std::make_shared<OwnedStoreTooltip>();
    entry->widget = il2cpp::GcHandle::Weak(tooltip);
    entry->plain  = il2cpp::GcHandle::Weak(plain);
    entry->action = il2cpp::GcHandle::Weak(action);
    if (entry->widget.Get() != tooltip || entry->plain.Get() != plain || entry->action.Get() != action) {
      (void)action->TrySetEnabled(false);
      return StoreTooltipUpdate::Unavailable;
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
      return StoreTooltipUpdate::Unavailable;
    }
    owned->store = il2cpp::GcHandle::Weak(store);
    if (owned->store.Get() != store) {
      ReleaseStoreAssets(*owned);
      (void)action->TrySetEnabled(false);
      return StoreTooltipUpdate::Unavailable;
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
    return StoreTooltipUpdate::Failed;
  action->CopyAppearance(plain);
  if (!current())
    return StoreTooltipUpdate::Failed;
  tooltip->SetSearchActionLocaleSetting(search);
  tooltip->SetActionLocaleSetting(locale);
  tooltip->SetSettings(settings);
  tooltip->SetTooltipTrigger(action);
  tooltip->SetTooltipActionTrigger(action);
  if (!tooltip->UpdateTooltipDescription() || !current() || !tooltip->SetupActionTooltipTrigger() || !current()
      || !plain->TrySetEnabled(false) || !action->TrySetEnabled(true) || !current())
    return StoreTooltipUpdate::Failed;
  const auto enabled = action->TryEnabled();
  if (!current() || !enabled || !*enabled || tooltip->tooltipTrigger() != action
      || tooltip->tooltipActionTrigger() != action)
    return StoreTooltipUpdate::Failed;
  rollback.committed = true;
  return previously_bound ? StoreTooltipUpdate::Unchanged : StoreTooltipUpdate::Upgraded;
}

// --- Hooks ---

void QueueStoreWork(AllianceGamesStoreScrollerViewController* source, bool currency)
{
  auto& work = alliance_store_work;
  if (work.source.Get() != source) {
    work.source = il2cpp::GcHandle::Weak(source);
    ++work.epoch;
  }
  if (work.source.Get() != source)
    return;
  // Debounce through separate-frame native card spawning.
  work.due     = StoreClock::now() + std::chrono::milliseconds(250);
  work.pending = true;
  work.currency |= currency && !work.refreshing_currency;
  work.layout |= currency && !work.refreshing_currency;
}

void AllianceGamesStoreScrollerViewController_OnDidBindCanvasContext_Hook(
    auto original, AllianceGamesStoreScrollerViewController* _this)
{
  if (!OnStoreThread()) {
    original(_this);
    return;
  }
  original(_this);
  std::erase_if(alliance_store_tooltips, [](auto& entry) { return !entry->widget.Get(); });
  if (IsAllianceStore(_this))
    QueueStoreWork(_this, true);
}

void AllianceGamesStoreScrollerViewController_OnAboutToReleaseCanvasContext_Hook(
    auto original, AllianceGamesStoreScrollerViewController* _this)
{
  if (!OnStoreThread()) {
    original(_this);
    return;
  }
  auto& work = alliance_store_work;
  if (work.source.Get() == _this) {
    work.source.Reset();
    work.pending = work.currency = work.layout = false;
    ++work.epoch;
  }
  // Native restoration can reenter bind/pruning. Keep stable entry custody and invalidate the source before the first
  // such callback.
  const auto entries = alliance_store_tooltips;
  for (auto& entry : entries)
    if (entry->store.Get() == _this)
      (void)RestoreStoreTooltip(*entry);
  original(_this);
}

void ShopListScrollerViewController_RegenerateSortedBundles_Hook(auto original, ShopListScrollerViewController* _this,
                                                                 bool instant)
{
  if (!OnStoreThread()) {
    original(_this, instant);
    return;
  }
  original(_this, instant);
  if (IsAllianceStore(_this) && !alliance_store_work.refreshing_currency)
    QueueStoreWork(static_cast<AllianceGamesStoreScrollerViewController*>(_this), true);
}

void BundleDataWidget_OnAboutToReleaseContext_Hook(auto original, BundleDataWidget* _this)
{
  if (!OnStoreThread()) {
    original(_this);
    return;
  }
  if (auto entry = FindOwnedTooltip(_this->invTooltip()))
    (void)RestoreStoreTooltip(*entry);
  original(_this);
}

void QueueStoreCard(BundleDataWidget* card)
{
  // Native OnDidBindContext sets this AFTER SetupTooltip. Never use the RefreshBundle argument or a pre-bind owner as
  // current-card identity.
  auto* owner = card->scrollerController();
  if (owner && alliance_store_work.source.Get() == owner && IsAllianceStore(owner))
    QueueStoreWork(static_cast<AllianceGamesStoreScrollerViewController*>(owner), false);
}
void BundleDataWidget_RefreshBundle_Hook(auto original, BundleDataWidget* _this, Bundle* bundle)
{
  if (!OnStoreThread()) {
    original(_this, bundle);
    return;
  }
  original(_this, bundle);
  QueueStoreCard(_this);
}
void BundleDataWidget_OnDidBindContext_Hook(auto original, BundleDataWidget* _this)
{
  if (!OnStoreThread()) {
    original(_this);
    return;
  }
  original(_this);
  QueueStoreCard(_this);
}

// --- Frame work ---

std::string StoreCountText(const auto& value)
{ return value ? std::to_string(*value) : "unavailable"; }

void AllianceGamesStoreFrame()
{
  if (!OnStoreThread())
    return;
  auto& work = alliance_store_work;
  if (!work.pending || StoreClock::now() < work.due)
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
    work.due = StoreClock::now() + std::chrono::milliseconds(250);
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
  const auto count = pool ? smart->SpawnedCount() : std::nullopt;
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
    switch (tooltip ? ReconcileStoreTooltip(tooltip, donor, roots) : StoreTooltipUpdate::Unchanged) {
      case StoreTooltipUpdate::Upgraded:
        ++upgraded;
        break;
      case StoreTooltipUpdate::Restored:
        ++restored;
        break;
      case StoreTooltipUpdate::Unavailable:
        ++unavailable;
        break;
      case StoreTooltipUpdate::Failed:
        ++failed;
        break;
      default:
        break;
    }
  }
  bool                   refreshed = false;
  std::optional<int32_t> currency_before, currency_after;
  // Native bundle-derived discovery, not a fixed list of currency IDs. An empty/loading store waits for its real
  // data/regeneration event.
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
               *count, upgraded, restored, unavailable, failed, refreshed, StoreCountText(currency_before),
               StoreCountText(currency_after), StoreCountText(visible), StoreCountText(position));
}

bool InstallAllianceGamesStore()
{
  using Store = AllianceGamesStoreScrollerViewController::Class;
  using Shop  = ShopListScrollerViewController::Class;
  using Card  = BundleDataWidget::Class;
  const std::array<il2cpp::Method, 6> targets{
      Store::Method<void()>("OnDidBindCanvasContext"),     Store::Method<void()>("OnAboutToReleaseCanvasContext"),
      Shop::Method<void(bool)>("RegenerateSortedBundles"), Card::Method<void(Bundle*)>("RefreshBundle"),
      Card::Method<void()>("OnAboutToReleaseContext"),     Card::Method<void()>("OnDidBindContext")};
  const std::array owners{Store::Raw(), Store::Raw(), Shop::Raw(), Card::Raw(), Card::Raw(), Card::Raw()};
  // Each target must be the class's own method: the lookup falls back to a base class's, and a detour there would run
  // for every subclass. Nor may two share the body SPUD patches, or one hook would also run for the other's receiver.
  std::array<void*, 6> bodies{};
  for (size_t i = 0; i < targets.size(); ++i) {
    if (!targets[i])
      continue; // Reported by the lookup, and keeps the set out.
    if (targets[i].Info()->klass != owners[i]) {
      spdlog::warn("[AllianceGamesStore] unavailable: {} is inherited", targets[i].Name());
      return false;
    }
    bodies[i] = il2cpp::DetourTarget(targets[i]);
    for (size_t j = 0; j < i; ++j)
      if (bodies[i] == bodies[j]) {
        spdlog::warn("[AllianceGamesStore] unavailable: shared hook target");
        return false;
      }
  }
  alliance_store_thread = std::this_thread::get_id();
  // One transaction: every hook or none. The frame work runs from the frame tick; without the tick the hooks only
  // record pending work.
  il2cpp::HookSet hooks("AllianceGamesStore");
  IL2CPP_STAGE(hooks, targets[0], AllianceGamesStoreScrollerViewController_OnDidBindCanvasContext_Hook);
  IL2CPP_STAGE(hooks, targets[1], AllianceGamesStoreScrollerViewController_OnAboutToReleaseCanvasContext_Hook);
  IL2CPP_STAGE(hooks, targets[2], ShopListScrollerViewController_RegenerateSortedBundles_Hook);
  IL2CPP_STAGE(hooks, targets[3], BundleDataWidget_RefreshBundle_Hook);
  IL2CPP_STAGE(hooks, targets[4], BundleDataWidget_OnAboutToReleaseContext_Hook);
  IL2CPP_STAGE(hooks, targets[5], BundleDataWidget_OnDidBindContext_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kAllianceGamesStore{"AllianceGamesStore", &Config::Patches::alliancegamesstorehooks,
                                  InstallAllianceGamesStore, AllianceGamesStoreFrame};

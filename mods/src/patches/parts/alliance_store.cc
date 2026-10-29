// Alliance Games store: native material research tooltips and currency refresh
// after bundle readiness. One feature owner, no claim-trace or file probe.
#include "patches/alliance_store_bindings.h"
#include "patches/native_hook_extent.h"
#include "patches/screen_update_hook.h"

#include <spud/detour.h>
#if _WIN32
#include <Windows.h>
#endif
#include <spdlog/spdlog.h>
#include <thread>

#include <chrono>
#include <memory>
#include <utility>

namespace
{
namespace store_game                  = alliance_store_game;
using Clock                           = std::chrono::steady_clock;
using StoreInventoryTooltip           = Il2CppObject;
using StoreTooltipTrigger             = Il2CppObject;
using StoreTooltipAction              = Il2CppObject;
using StoreActionLocale               = Il2CppObject;
using StoreActionLocales              = Il2CppObject;
using AllianceStoreScroller           = Il2CppObject;
using InventoryItem                   = Il2CppObject;
using AllianceStoreContext            = Il2CppObject;
using ShopListScrollerViewController  = Il2CppObject;
using BundleDataWidget                = Il2CppObject;
using Bundle                          = Il2CppObject;
bool            alliance_store_active = false;
std::thread::id alliance_store_thread;
bool            StoreThread()
{ return alliance_store_active && std::this_thread::get_id() == alliance_store_thread; }

struct AllianceStoreWork {
  store_game::Handle source;
  uint64_t           epoch   = 0;
  bool               pending = false, currency = false, refreshing_currency = false, layout = false;
  Clock::time_point  due{};
} alliance_store_work;

enum class ResearchUpdate { Unchanged, Upgraded, Restored, Unavailable, Failed };

bool SameStoreItem(StoreInventoryTooltip* tooltip, InventoryItem* item)
{
  const auto alive = store_game::Alive(tooltip);
  if (!alive || !alive.value)
    return false;
  const auto current = store_game::Call<Il2CppObject*>(tooltip, "get_Context");
  return current && current.value == item;
}

// Weak component identity survives pooled reuse without retaining a scene. Only
// the original serialized assets stay rooted while our replacement is bound.
struct OwnedStoreTooltip {
  store_game::Handle widget, plain, action, store;
  store_game::Handle search, locale, settings;
  bool               bound = false, plain_enabled = true, retiring = false;
  uint64_t           generation = 0;
};
std::vector<std::shared_ptr<OwnedStoreTooltip>> alliance_store_tooltips;

std::shared_ptr<OwnedStoreTooltip> OwnedTooltip(StoreInventoryTooltip* tooltip)
{
  for (auto& entry : alliance_store_tooltips)
    if (entry->widget.Get() == tooltip)
      return entry;
  return nullptr;
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
  store_game::Roots roots;
  auto*             widget = roots.Keep(entry.widget.Get<StoreInventoryTooltip>());
  auto*             plain  = roots.Keep(entry.plain.Get<StoreTooltipTrigger>());
  auto*             action = roots.Keep(entry.action.Get<StoreTooltipAction>());
  const auto        alive  = store_game::Alive(widget);
  if (!roots.healthy || !widget || !plain || !action || !alive)
    return false;
  if (!alive.value) {
    entry.bound = false;
    entry.search.Reset();
    entry.locale.Reset();
    entry.settings.Reset();
    return true;
  }
  auto& f = store_game::Members();
  if ((f.trigger.Read(widget) != plain && f.trigger.Read(widget) != action)
      || (f.action.Read(widget) && f.action.Read(widget) != action)) {
    // A new native owner replaced our arrangement. Disable only our component;
    // never overwrite the replacement owner's fields or enabled state.
    (void)store_game::Enabled(action, false);
    entry.bound = false;
    entry.search.Reset();
    entry.locale.Reset();
    entry.settings.Reset();
    return true;
  }
  bool ok = store_game::Enabled(action, false);
  ok &= f.trigger.Write(widget, plain);
  ok &= f.action.Write(widget, nullptr);
  ok &= f.search.Write(widget, entry.search.Get<StoreActionLocale>());
  ok &= f.locale.Write(widget, entry.locale.Get<StoreActionLocale>());
  ok &= f.settings.Write(widget, entry.settings.Get<StoreActionLocales>());
  ok &= store_game::Call<void>(widget, "UpdateTooltipDescription").succeeded;
  ok &= store_game::Enabled(plain, entry.plain_enabled);
  if (ok) {
    entry.bound = false;
    entry.search.Reset();
    entry.locale.Reset();
    entry.settings.Reset();
  }
  return ok;
}

ResearchUpdate ReconcileStoreTooltip(StoreInventoryTooltip* tooltip, StoreInventoryTooltip*& donor,
                                     store_game::Roots& pass_roots)
{
  store_game::Roots roots;
  if (!roots.Keep(tooltip))
    return ResearchUpdate::Unavailable;
  const auto epoch     = alliance_store_work.epoch;
  auto*      store     = roots.Keep(alliance_store_work.source.Get<AllianceStoreScroller>());
  const auto alive     = store_game::Alive(tooltip);
  const auto observed  = alive && alive.value ? store_game::Call<Il2CppObject*>(tooltip, "get_Context")
                                              : store_game::Result<InventoryItem*>{};
  auto*      item      = observed ? roots.Keep(observed.value) : nullptr;
  auto       same_item = [&] {
    if (!store || alliance_store_work.epoch != epoch || alliance_store_work.source.Get() != store)
      return false;
    const auto live_store = store_game::Alive(store);
    return live_store && live_store.value && SameStoreItem(tooltip, item) && alliance_store_work.epoch == epoch
           && alliance_store_work.source.Get() == store;
  };
  auto* plain = item ? store_game::PlainComponent(tooltip, roots) : nullptr;
  if (!plain)
    return ResearchUpdate::Unchanged;
  auto& f           = store_game::Members();
  auto* old_action  = roots.Keep(f.action.Read(tooltip));
  auto* old_trigger = roots.Keep(f.trigger.Read(tooltip));
  auto  owned       = OwnedTooltip(tooltip);
  // Existing native action components and unrelated arrangements remain native.
  if (!roots.healthy || (old_action && (!owned || owned->action.Get() != old_action))
      || (old_action ? old_trigger != old_action : old_trigger != plain))
    return ResearchUpdate::Unchanged;
  const auto eligible = store_game::Eligible(item);
  if (!eligible || !same_item())
    return ResearchUpdate::Unavailable;
  if (!eligible.value)
    return owned && owned->bound ? (RestoreStoreTooltip(*owned) ? ResearchUpdate::Restored : ResearchUpdate::Failed)
                                 : ResearchUpdate::Unchanged;
  if (!donor)
    donor = store_game::Template(pass_roots);
  if (!donor)
    return ResearchUpdate::Unavailable;
  auto*      search        = roots.Keep(f.search.Read(donor));
  auto*      locale        = roots.Keep(f.locale.Read(donor));
  auto*      settings      = roots.Keep(f.settings.Read(donor));
  auto*      loader        = roots.Keep(f.loader.Read(plain));
  const auto plain_enabled = store_game::Call<bool>(plain, "get_enabled");
  if (!roots.healthy || !search || !loader || !plain_enabled || !same_item())
    return ResearchUpdate::Unavailable;
  auto* action = store_game::ActionComponent(tooltip, false, roots);
  // Reuse only the disabled component this feature previously added.
  if (action && (!owned || owned->action.Get() != action))
    return ResearchUpdate::Unchanged;
  if (!action)
    action = store_game::ActionComponent(tooltip, true, roots);
  if (!action)
    return ResearchUpdate::Unavailable;
  if (!owned) {
    auto entry    = std::make_shared<OwnedStoreTooltip>();
    entry->widget = store_game::Handle::Weak(tooltip);
    entry->plain  = store_game::Handle::Weak(plain);
    entry->action = store_game::Handle::Weak(action);
    if (!entry->widget || !entry->plain || !entry->action) {
      store_game::Enabled(action, false);
      return ResearchUpdate::Unavailable;
    }
    alliance_store_tooltips.push_back(std::move(entry));
    owned = alliance_store_tooltips.back();
  }
  const bool previously_bound = owned->bound;
  if (!owned->bound) {
    auto retain = []<typename T>(T* value, store_game::Handle& handle) {
      handle = store_game::Handle::Strong(value);
      return !value || (handle && handle.Get() == value);
    };
    if (!retain(f.search.Read(tooltip), owned->search) || !retain(f.locale.Read(tooltip), owned->locale)
        || !retain(f.settings.Read(tooltip), owned->settings)) {
      owned->search.Reset();
      owned->locale.Reset();
      owned->settings.Reset();
      store_game::Enabled(action, false);
      return ResearchUpdate::Unavailable;
    }
    owned->store = store_game::Handle::Weak(store);
    if (!owned->store || owned->store.Get() != store) {
      owned->search.Reset();
      owned->locale.Reset();
      owned->settings.Reset();
      store_game::Enabled(action, false);
      return ResearchUpdate::Unavailable;
    }
    owned->plain_enabled = plain_enabled.value;
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

  if (!store_game::Enabled(action, false) || !store_game::CopyAppearance(plain, action, roots) || !current()
      || !f.search.Write(tooltip, search) || !f.locale.Write(tooltip, locale) || !f.settings.Write(tooltip, settings)
      || !f.trigger.Write(tooltip, action) || !f.action.Write(tooltip, action)
      || !store_game::Call<void>(tooltip, "UpdateTooltipDescription") || !current()
      || !store_game::Call<void>(tooltip, "SetupActionTooltipTrigger") || !current()
      || !store_game::Enabled(plain, false) || !store_game::Enabled(action, true) || !current())
    return ResearchUpdate::Failed;
  const auto enabled = store_game::Call<bool>(action, "get_enabled");
  if (!current() || !enabled || !enabled.value || f.trigger.Read(tooltip) != action || f.action.Read(tooltip) != action)
    return ResearchUpdate::Failed;
  rollback.committed = true;
  return previously_bound ? ResearchUpdate::Unchanged : ResearchUpdate::Upgraded;
}

void QueueStore(AllianceStoreScroller* source, bool currency)
{
  if (!store_game::IsStore(source))
    return;
  auto& work = alliance_store_work;
  if (work.source.Get() != source) {
    work.source = store_game::Handle::Weak(source);
    ++work.epoch;
  }
  if (!work.source)
    return;
  // Debounce through separate-frame native card spawning.
  work.due     = Clock::now() + std::chrono::milliseconds(250);
  work.pending = true;
  work.currency |= currency && !work.refreshing_currency;
  work.layout |= currency && !work.refreshing_currency;
}

void AllianceStoreScroller_OnDidBindCanvasContext_Hook(auto original, AllianceStoreScroller* self)
{
  if (!StoreThread()) {
    original(self);
    return;
  }
  original(self);
  std::erase_if(alliance_store_tooltips, [](auto& entry) { return !entry->widget.Get(); });
  QueueStore(self, true);
}

void AllianceStoreScroller_OnAboutToReleaseCanvasContext_Hook(auto original, AllianceStoreScroller* self)
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
  if (store_game::IsStore(self) && !alliance_store_work.refreshing_currency)
    QueueStore(reinterpret_cast<AllianceStoreScroller*>(self), true);
}

void BundleDataWidget_OnAboutToReleaseContext_Hook(auto original, BundleDataWidget* self)
{
  if (!StoreThread()) {
    original(self);
    return;
  }
  auto* tooltip = store_game::Members().bundle_tooltip.Read(self);
  if (auto entry = OwnedTooltip(tooltip))
    (void)RestoreStoreTooltip(*entry);
  original(self);
}

void QueueStoreCard(BundleDataWidget* self)
{
  // Native OnDidBindContext sets this AFTER SetupTooltip. Never use the
  // RefreshBundle argument or a pre-bind owner as current-card identity.
  auto* owner = store_game::Members().bundle_owner.Read(self);
  if (owner && alliance_store_work.source.Get() == owner)
    QueueStore(reinterpret_cast<AllianceStoreScroller*>(owner), false);
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

void AllianceStoreFrame()
{
  if (!StoreThread())
    return;
  auto& work = alliance_store_work;
  if (!work.pending || Clock::now() < work.due)
    return;
  const bool currency     = std::exchange(work.currency, false);
  const bool layout       = std::exchange(work.layout, false);
  work.pending            = false;
  const auto        epoch = work.epoch;
  store_game::Roots roots;
  auto*             source   = roots.Keep(work.source.Get<AllianceStoreScroller>());
  const auto        alive    = store_game::Alive(source);
  const auto        observed = source && alive && alive.value
                                   ? store_game::Call<Il2CppObject*>(source, "get_AllianceGamesContext")
                                   : store_game::Result<AllianceStoreContext*>{};
  auto*             context  = observed ? roots.Keep(observed.value) : nullptr;
  const auto active = context ? store_game::Call<bool>(context, "get_IsShopViewActive") : store_game::Result<bool>{};
  if (!context || !active || !active.value)
    return;
  if (work.epoch != epoch || work.source.Get() != source)
    return;
  auto& fields = store_game::Members();
  auto* smart  = roots.Keep(fields.scroller.Read(source));
  if (smart && (fields.regenerating.Read(smart) || fields.spawning.Read(smart) || fields.initializing.Read(smart))) {
    work.pending = true;
    work.currency |= currency;
    work.layout |= layout;
    work.due = Clock::now() + std::chrono::milliseconds(250);
    return;
  }
  bool position_notified = false;
  if (layout && smart) {
    const auto position   = store_game::NormalizedPosition(smart);
    const auto live_smart = store_game::Alive(smart);
    if (position && live_smart && live_smart.value && work.epoch == epoch && work.source.Get() == source
        && fields.scroller.Read(source) == smart) {
      position_notified = store_game::NotifyPosition(smart, position.value);
    }
  }
  if (work.epoch != epoch || work.source.Get() != source || fields.scroller.Read(source) != smart)
    return;
  auto*      pool  = smart ? roots.Keep(fields.spawned.Read(smart)) : nullptr;
  const auto count = pool ? store_game::Call<int32_t>(pool, "get_Count") : store_game::Result<int32_t>{};
  if (!count || count.value < 0)
    return;
  int                    upgraded = 0, restored = 0, unavailable = 0, failed = 0;
  StoreInventoryTooltip* donor        = nullptr;
  auto*                  tooltip_type = roots.Keep(store_game::TypeObject(store_game::TooltipClass()));
  if (!tooltip_type)
    return;
  for (int32_t index = 0; index < count.value; ++index) {
    if (work.epoch != epoch || work.source.Get() != source)
      return;
    store_game::Roots card_roots;
    const auto entry = store_game::Call<Il2CppObject*>(pool, "get_Item", {store_game::Scalar(index, "System.Int32")});
    auto*      rect  = entry ? card_roots.Keep(entry.value) : nullptr;
    const auto live  = store_game::Alive(rect);
    if (!rect || !live || !live.value)
      continue;
    const auto tooltip = store_game::Result<Il2CppObject*>{
        true, store_game::Component(rect, "GetComponentInChildren", store_game::TooltipClass(), card_roots, true)};
    switch (tooltip && tooltip.value ? ReconcileStoreTooltip(tooltip.value, donor, roots) : ResearchUpdate::Unchanged) {
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
  bool                        refreshed = false;
  store_game::Result<int32_t> currency_before, currency_after;
  // Native bundle-derived discovery, not a fixed list of currency IDs. An
  // empty/loading store waits for its real data/regeneration event.
  const auto fetching = store_game::Call<bool>(context, "get_IsFetchingBundlesInProgress");
  const auto bundles  = store_game::Call<int32_t>(context, "get_BundlesCount");
  if (currency && work.epoch == epoch && work.source.Get() == source && fetching && !fetching.value && bundles
      && bundles.value > 0) {
    work.refreshing_currency = true;
    struct Reset {
      bool& value;
      ~Reset()
      { value = false; }
    } reset{work.refreshing_currency};
    currency_before        = store_game::CurrencyCount(context, roots);
    const auto current     = store_game::Call<Il2CppObject*>(source, "get_AllianceGamesContext");
    const auto live_source = store_game::Alive(source);
    if (current && current.value == context && live_source && live_source.value && work.epoch == epoch
        && work.source.Get() == source) {
      refreshed      = store_game::Call<void>(context, "UpdateStoreCurrency").succeeded;
      currency_after = store_game::CurrencyCount(context, roots);
    }
  }
  if (layout)
    spdlog::info("[AllianceGamesStore] settled layout notification={} (position unchanged)", position_notified);
  const auto visible  = store_game::Call<int32_t>(smart, "get_VisibleItemsCount");
  const auto position = store_game::Call<float>(smart, "get_ScrollPosition");
  spdlog::info("[AllianceGamesStore] pool={} upgraded={} restored={} unavailable={} failed={} currency_refresh={} "
               "currencies={}->{} visible={} position={}",
               count.value, upgraded, restored, unavailable, failed, refreshed,
               currency_before ? std::to_string(currency_before.value) : "unavailable",
               currency_after ? std::to_string(currency_after.value) : "unavailable",
               visible ? std::to_string(visible.value) : "unavailable",
               position ? std::to_string(position.value) : "unavailable");
}

bool StoreHookFits(const MethodInfo* method)
{
  if (!method || !method->methodPointer)
    return false;
  auto* entry = reinterpret_cast<void*>(method->methodPointer);
#if _WIN32 && defined(_M_X64)
  DWORD64 base     = 0;
  auto*   function = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(entry), &base, nullptr);
  return function && base + function->BeginAddress == reinterpret_cast<DWORD64>(entry)
         && function->EndAddress > function->BeginAddress
         && spud::has_detour_prologue(entry, function->EndAddress - function->BeginAddress);
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
  auto*            store = store_game::Class("Digit.Prime.Alliances.Games", "AllianceGamesStoreScrollerViewController");
  auto*            shop  = store_game::Class("Digit.Prime.Shop", "ShopListScrollerViewController");
  auto*            card  = store_game::Class("Digit.Prime.Shop", "BundleDataWidget");
  const std::array targets{
      method_contract::Resolve(store, "OnDidBindCanvasContext", false, "System.Void", {}),
      method_contract::Resolve(store, "OnAboutToReleaseCanvasContext", false, "System.Void", {}),
      method_contract::Resolve(shop, "RegenerateSortedBundles", false, "System.Void", {"System.Boolean"}),
      method_contract::Resolve(card, "RefreshBundle", false, "System.Void", {"Digit.PrimePlatform.Content.Bundle"}),
      method_contract::Resolve(card, "OnAboutToReleaseContext", false, "System.Void", {}),
      method_contract::Resolve(card, "OnDidBindContext", false, "System.Void", {})};
  if (!store_game::Members().Resolve()) {
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

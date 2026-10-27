#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "page_navigation.h"
#include "action_widgets.h"
#include "patches/parts/fc_confirmation_reset.h"
#include "patches/runtime_config.h"
#include "row_style.h"
#include "settings/maintenance.h"
#include "settings/mod_pages.h"
#include "settings/native_boolean_callback.h"
#include "settings/page_sections.h"
#include "settings/text_entry.h"
#include "settings_search.h"
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>
#include <spud/detour.h>
#include <tuple>

namespace mod_settings::native
{
namespace
{
  constexpr auto                 saveNoticeId = "community_mod.save_notice";
  std::vector<PageCatalog::Page> pagePlan;
  std::vector<Il2CppGCHandle>    categoryWidgets;
  bool                           pagesActive = false;
} // namespace
const std::vector<PageCatalog::Page>& Pages()
{ return pagePlan; }
bool PagesActive()
{ return pagesActive; }
void DisablePages()
{ pagesActive = false; }
PageMetadata& PageMeta()
{
  static PageMetadata metadata;
  return metadata;
}
const PageCatalog::Page* PageFor(Il2CppObject* context)
{
  if (!context)
    return nullptr;
  for (const auto& page : Pages())
    if (HasLabel(context, page.id.c_str()))
      return &page;
  return nullptr;
}

// One open page, with visit-local expansion state. The native context retains
// every child; only the list's presentation is filtered. Back and save ownership
// remain native, and a fresh page visit starts collapsed.
struct SectionPage {
  const PageCatalog::Page*   page       = nullptr;
  Il2CppGCHandle             controller = nullptr, context = nullptr;
  Il2CppGCHandle             searchReturn = nullptr; // Only this visit came from a search result.
  PageSections               sections;
  std::vector<Il2CppObject*> shown; // Comparison only; native context/panel owns rows.
  bool                       conditional = false;
} sectionPage;
using SectionRefreshScope = PageSections::RefreshScope;
void ClearSectionPage()
{

  CloseSettingsSearch();
  CloseTextEntry();
  const auto* leaving = std::exchange(sectionPage.page, nullptr);
  Free(sectionPage.controller);
  Free(sectionPage.context);
  Free(sectionPage.searchReturn);
  sectionPage.sections.ExpandAll();
  sectionPage.shown.clear();
  sectionPage.conditional = false;
  try {
    if (leaving && leaving->leave)
      leaving->leave();
  } catch (...) {
    Warn("settings page cleanup unavailable");
  }
}
const PageCatalog::Heading* CollapsibleHeadingFor(Il2CppObject* context)
{
  if (!context || context->klass != PageMeta().category.get_cls())
    return nullptr;
  auto* callback = reinterpret_cast<Il2CppDelegate*>(ReadField(context, ToggleMeta().queryField));
  if (!callback || callback->method != QueryMethod() || callback->method_ptr != QueryMethod()->methodPointer)
    return nullptr;
  Root parent(Call(context, "get_Parent"));
  if (const auto* page = PageFor(parent.get()))
    for (const auto& item : page->items)
      if (const auto* heading = std::get_if<PageCatalog::Heading>(&item);
          heading && heading->collapsible && HasLabel(context, heading->id.c_str()))
        return heading;
  return nullptr;
}
bool Collapsed(const PageCatalog::Heading& heading)
{ return sectionPage.sections.Collapsed(heading); }
// Native OnDidBindContext writes Toggle.isOn after its appearance refresh.
// Reconcile after the complete panel bind, including partial-bind failures.
struct MultiSelectBindScope {
  ~MultiSelectBindScope()
  {
    try {
      SynchronizeMultiSelectToggles();
    } catch (...) {
      Warn("multi-select post-bind reconciliation unavailable");
    }
  }
};
void ShowSections(Il2CppObject* controller, Il2CppObject* context, const PageCatalog::Page& page, bool force = false)
{
  MultiSelectBindScope reconcile;
  SyncActionRows(controller, context, page);
  Root children(Call(context, "get_Children"));
  struct OrderedRow {
    Il2CppObject* row;
    std::size_t   position, index;
  };
  std::vector<OrderedRow> ordered;
  for (int i = 0, count = Count(children.get()); i < count; ++i) {
    auto*       row = Item(children.get(), i); // Rooted by the unchanged native children.
    std::string id;
    std::size_t index = 0;
    if (OwnsValueContext(row)) {
      if (const auto choice = ChoiceFor(row); choice.first) {
        id    = choice.first->state().id();
        index = choice.second + 1; // The group title precedes option zero.
      } else if (auto* slider = SliderFor(row)) {
        id = slider->state().id();
      } else if (auto* setting = SettingFor(row)) {
        id = setting->id();
      }
    }
    if (ActionsActive())
      if (auto action = ActionFor(row); action.first) {
        if (!action.first->Read(action.second).visible)
          continue;
        bool inlineEditor = false;
        for (auto* slider : page.Controls<SliderSetting>())
          if (slider->exact_editor == action.first)
            for (int j = 0, count = Count(children.get()); j < count; ++j) {
              auto* candidate = Item(children.get(), j);
              if (SliderFor(candidate) == slider && InlineSliderEditorActive(candidate))
                inlineEditor = true;
            }
        if (inlineEditor)
          continue;
        id    = action.first->id();
        index = action.second;
      }
    if (id.empty() && row->klass == HeadingMeta().row.get_cls())
      for (auto* choice : page.Controls<ChoiceSetting>())
        if (HasLabel(row, choice->title_id().c_str())) {
          id = choice->state().id();
          break;
        }
    if (id.empty())
      for (const auto& item : page.items)
        if (const auto* heading = std::get_if<PageCatalog::Heading>(&item);
            heading && HasLabel(row, heading->id.c_str())) {
          id = heading->id;
          break;
        }
    if (FilterSettingsSearch(page, id) && sectionPage.sections.Visible(page, id))
      ordered.push_back({row, page.PositionFor(id), index});
  }
  // Repeated rows may have been appended after other native children. Keep the
  // catalog's presentation order without rewriting the native ownership list.
  std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
    return std::tie(a.position, a.index) < std::tie(b.position, b.index);
  });
  std::vector<Il2CppObject*> visible;
  for (const auto& row : ordered)
    visible.push_back(row.row);
  if (!force && visible == sectionPage.shown)
    return;
  auto options = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "OptionContext");
  Root list(reinterpret_cast<Il2CppObject*>(il2cpp_array_new(options.get_cls(), visible.size())));
  if (!list.get())
    throw std::runtime_error("settings section list allocation");
  for (std::size_t i = 0; i < visible.size(); ++i) {
    auto* array = reinterpret_cast<Il2CppArraySize*>(list.get());
    il2cpp_gc_wbarrier_set_field(list.get(), reinterpret_cast<void**>(&array->vector[i]), visible[i]);
  }
  Root        panel(ReadField(controller, PageMeta().panel));
  static auto widgets = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.UI", "Widget");
  static auto panels  = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "OptionTabPanelWidget");
  static const auto* schema = widgets.GetMethodInfo("BindDataContext", 2);
  auto*              lists  = il2cpp_class_from_name(il2cpp_get_corlib(), "System.Collections", "IList");
  if (!panel.get() || panel.get()->klass != panels.get_cls() || !Instance(schema, 2, IL2CPP_TYPE_VOID)
      || !(schema->flags & METHOD_ATTRIBUTE_VIRTUAL) || !Reference(schema->parameters[0])
      || !Type(schema->parameters[1], IL2CPP_TYPE_OBJECT) || !lists
      || !il2cpp_class_is_assignable_from(lists, list.get()->klass))
    throw std::runtime_error("settings section list schema");
  // Resolve the non-generic Widget virtual slot, not the same-arity typed
  // Widget<IList> overload. This is the object overload used by the game itself.
  const auto* bind = il2cpp_object_get_virtual_method(panel.get(), schema);
  if (!Instance(bind, 2, IL2CPP_TYPE_VOID) || bind->slot != schema->slot
      || !Type(bind->parameters[1], IL2CPP_TYPE_OBJECT)
      || il2cpp_class_from_type(bind->parameters[0]) != il2cpp_class_from_type(schema->parameters[0]))
    throw std::runtime_error("settings section virtual binding");
  // The same provider/null + IList bind used by native OnCategorySelected.
  // Native release/bind owns pooled widgets and their event subscriptions.
  void* args[] = {nullptr, list.get()};
  Invoke(bind, panel.get(), args);
  sectionPage.shown = std::move(visible);
}

bool PageRefreshInProgress()
{ return sectionPage.sections.Refreshing(); }
void RenderCategory(Il2CppObject* widget)
{
  try {
    Root context(Call(widget, "get_Context"));
    if (auto* page = PageFor(context.get())) {
      try {
        Root background(RowImage(widget, "Background"));
        RememberChoiceSprite(background.get());
      } catch (...) {
        Warn("settings background unavailable");
      }
      Root       label(ReadField(widget, PageMeta().label));
      const auto summary = page->summary ? page->summary() : std::string{};
      SetRowText(widget, label.get(), page->label + (summary.empty() ? "" : " — " + summary));
    } else if (const auto* heading = CollapsibleHeadingFor(context.get())) {
      Root       label(ReadField(widget, PageMeta().label));
      const auto summary = Collapsed(*heading) && heading->summary ? heading->summary() : std::string{};
      SetRowText(widget, label.get(),
                 "<b><size=115%><color=#9ADBE7>" + heading->label + "</color></size></b>"
                     + (summary.empty() ? "" : " — " + summary),
                 true, !Collapsed(*heading));
    }
  } catch (...) {
    Warn("settings category rendering unavailable");
  }
}
void ForgetCategory(Il2CppObject* widget)
{
  for (auto& handle : categoryWidgets)
    if (!Target(handle) || Target(handle) == widget)
      Free(handle);
}
void CategoryBindHook(auto original, Il2CppObject* widget)
{
  if (OnUIThread()) {
    ForgetCategory(widget);
    ClearRowText(widget);
  }
  original(widget);
  if (!OnUIThread() || !pagesActive)
    return;
  try {
    Root context(Call(widget, "get_Context"));
    if (!PageFor(context.get()) && !CollapsibleHeadingFor(context.get()))
      return;
    auto slot = std::find(categoryWidgets.begin(), categoryWidgets.end(), nullptr);
    if (slot == categoryWidgets.end()) {
      categoryWidgets.push_back(nullptr);
      slot = categoryWidgets.end() - 1;
    }
    *slot = il2cpp_gchandle_new_weakref(widget, false);
    RenderCategory(widget);
  } catch (...) {
    Warn("settings category presentation unavailable");
  }
}
void RefreshPageSummaries()
{
  if (!OnUIThread() || !pagesActive)
    return;
  // Weak records are reused on bind/release. No scene search or idle polling.
  // Keep indices across native text callbacks, which may rebind a pooled row.
  for (std::size_t i = 0, count = categoryWidgets.size(); i < count; ++i) {
    Root widget(Target(categoryWidgets[i]));
    if (widget.get())
      RenderCategory(widget.get());
    else
      Free(categoryWidgets[i]);
  }
}
void CategoryReleaseHook(auto original, Il2CppObject* widget)
{
  if (OnUIThread()) {
    ForgetCategory(widget);
    ClearRowText(widget);
  }
  original(widget);
}
void PageSelectedHook(auto original, Il2CppObject* controller, Il2CppObject* context)
{
  MultiSelectBindScope reconcile;
  // Keep a redirected context rooted through ClearSectionPage and native bind.
  Root searchReturn(OnUIThread() ? Target(sectionPage.searchReturn) : nullptr);
  if (OnUIThread() && pagesActive) {
    bool sectionClick = false;
    try {
      if (const auto* heading = CollapsibleHeadingFor(context)) {
        sectionClick = true;
        if (sectionPage.sections.Refreshing() || Target(sectionPage.controller) != controller)
          return;
        Root parent(Call(context, "get_Parent"));
        Root canvas(Call(controller, "get_CanvasContext"));
        Root selected(Call(canvas.get(), "get_SelectedOption"));
        if (!parent.get() || parent.get() != Target(sectionPage.context) || selected.get() != parent.get())
          return; // An old pooled heading cannot navigate or change this page.
        SectionRefreshScope scope(sectionPage.sections);
        const auto          before = sectionPage.sections.Snapshot();
        sectionPage.sections.Toggle(*heading);
        try {
          ShowSections(controller, parent.get(), *PageFor(parent.get()), true);
        } catch (...) {
          sectionPage.sections.Restore(before);
          try {
            ShowSections(controller, parent.get(), *PageFor(parent.get()), true);
          } catch (...) {
          }
          throw;
        }
        return; // A section click refreshes this page; it is not navigation.
      }
    } catch (const std::exception& error) {
      // Invoke converts managed failures to fixed messages, without game data.
      // Keep the concrete lookup/binding reason; a generic warning hid the
      // incorrect SetContext lookup that prevented sections from folding.
      Warn("settings section unavailable");
      if (sectionClick)
        return;
    } catch (...) {
      Warn("settings section unavailable");
      if (sectionClick)
        return;
    }
    try {
      // Native Back passes SelectedOption.Parent to OnCategorySelected. Redirect
      // only that transition for this controller's search-opened visit; section
      // toggles above do not consume it. Do not reparent the native option tree.
      if (searchReturn.get() && Target(sectionPage.controller) == controller) {
        Root current(Target(sectionPage.context));
        Root canvas(Call(controller, "get_CanvasContext"));
        Root selected(Call(canvas.get(), "get_SelectedOption"));
        if (current.get() && selected.get() == current.get()) {
          Root parent(Call(current.get(), "get_Parent"));
          if (context == parent.get())
            context = searchReturn.get();
        }
      }
    } catch (...) {
      Warn("search return unavailable; using normal Back destination");
    }
    ClearSectionPage();
    try {
      if (const auto* page = PageFor(context)) {
        sectionPage.page       = page;
        sectionPage.controller = il2cpp_gchandle_new_weakref(controller, false);
        sectionPage.context    = il2cpp_gchandle_new_weakref(context, false);
        if (!sectionPage.controller || !sectionPage.context)
          ClearSectionPage();
        else {
          sectionPage.conditional = page->HasConditionalSections();
          sectionPage.sections.Begin(*page);
        }
      }
    } catch (...) {
      ClearSectionPage();
      Warn("settings section owner unavailable");
    }
  }
  if (OnUIThread())
    ClearRowText(controller);
  if (OnUIThread() && pagesActive) {
    // Native navigation binds rows before returning. Callbacks from those rows
    // must not rebind the panel while its initial population is still running.
    SectionRefreshScope scope(sectionPage.sections);
    original(controller, context);
  } else {
    original(controller, context);
  }
  if (!OnUIThread() || !pagesActive)
    return;
  try {
    if (auto* page = PageFor(context)) {
      Root label(ReadField(controller, PageMeta().title));
      SetRowText(controller, label.get(), page->label);
      if (page->id == "community_mod.settings")
        OpenSettingsSearch(controller);
      if (Target(sectionPage.controller) == controller && Target(sectionPage.context) == context
          && !sectionPage.sections.Refreshing()) {
        // Let native navigation establish the page and Back target, then apply
        // the initial folded presentation in the same call, before a frame draws.
        SectionRefreshScope scope(sectionPage.sections);
        try {
          ShowSections(controller, context, *page, true);
        } catch (...) {
          // If folding is unavailable, keep the controls accessible and the
          // heading arrows consistent with the expanded fallback.
          sectionPage.sections.ExpandAll();
          try {
            ShowSections(controller, context, *page);
          } catch (...) {
          }
          throw;
        }
      }
    }
  } catch (...) {
    Warn("selected settings page rendering unavailable");
  }
}
void PageDestroyedHook(auto original, Il2CppObject* controller)
{
  if (OnUIThread()) {
    ClearRowText(controller);
    if (Target(sectionPage.controller) == controller)
      ClearSectionPage();
  }
  original(controller);
}

HeadingMetadata& HeadingMeta()
{
  static HeadingMetadata metadata;
  return metadata;
}
NativeCallback<Il2CppString*> headingGetter;
bool                          headingsActive = false;
Il2CppString*                 EmptyHeadingValue(Il2CppObject*, const MethodInfo*)
{ return il2cpp_string_new(""); }
const std::string* HeadingLabelFor(Il2CppObject* context)
{
  if (!context || context->klass != HeadingMeta().row.get_cls())
    return nullptr;
  auto* callback = reinterpret_cast<Il2CppDelegate*>(ReadField(context, HeadingMeta().queryField));
  if (!callback || callback->method != QueryMethod() || callback->method_ptr != QueryMethod()->methodPointer)
    return nullptr;
  for (const auto& page : Pages()) {
    for (const auto& item : page.items)
      if (auto* heading = std::get_if<PageCatalog::Heading>(&item); heading && HasLabel(context, heading->id.c_str()))
        return &heading->label;
    for (auto* choice : page.Controls<ChoiceSetting>())
      if (HasLabel(context, choice->title_id().c_str()))
        return &choice->state().label();
  }
  return nullptr;
}
void HeadingRefreshHook(auto original, Il2CppObject* widget)
{
  if (OnUIThread())
    ClearRowText(widget);
  original(widget);
  if (!OnUIThread() || !headingsActive || !pagesActive)
    return;
  try {
    Root context(Invoke(HeadingMeta().getContext, widget));
    if (const auto* heading = HeadingLabelFor(context.get())) {
      Root label(ReadField(widget, HeadingMeta().label));
      // Rich text stays inside the existing local override and is cleared with
      // it. A darker bar and larger, bold label distinguish a heading from input.
      SetRowText(widget, label.get(), "<b><size=115%><color=#9ADBE7>" + *heading + "</color></size></b>", true);
    }
  } catch (...) {
    Warn("settings heading unavailable");
  }
}
void HeadingClearHook(auto original, Il2CppObject* widget)
{
  if (OnUIThread())
    ClearRowText(widget);
  original(widget);
}

void RefreshPageRows()
{
  if (!OnUIThread() || !pagesActive)
    return;
  if (sectionPage.sections.Refreshing())
    return;
  // Value-change observers run inside the write guard. Rebinding there could
  // release the requesting row before it has consumed its authoritative result.
  if (ValueWidgetsBusy())
    return;
  try {
    Root controller(Target(sectionPage.controller)), context(Target(sectionPage.context));
    if (controller.get() && context.get()) {
      Root canvas(Call(controller.get(), "get_CanvasContext"));
      Root selected(Call(canvas.get(), "get_SelectedOption"));
      if (const auto* page = PageFor(context.get()); page && selected.get() == context.get()) {
        SectionRefreshScope scope(sectionPage.sections);
        ShowSections(controller.get(), context.get(), *page);
      }
    }
  } catch (...) {
    Warn("settings page list refresh unavailable");
  }
}
void NavigateToSearchResult(std::string_view pageId, std::string_view itemId)
{
  Root controller(Target(sectionPage.controller));
  if (!controller.get() || !sectionPage.page || sectionPage.page->id != "community_mod.settings")
    return;
  Root returnToSearch(Target(sectionPage.context));
  if (!returnToSearch.get())
    return;
  Root canvas(Call(controller.get(), "get_CanvasContext"));
  Root root(Call(canvas.get(), "get_RootOption"));
  // Follow catalog ancestry, bounding discovery to direct native children.
  std::vector<std::string> path;
  for (std::string id(pageId); !id.empty();) {
    const auto page = std::ranges::find(Pages(), id, &PageCatalog::Page::id);
    if (page == Pages().end() || path.size() >= Pages().size())
      return;
    path.push_back(page->id);
    id = page->parent;
  }
  auto* target = root.get();
  for (auto it = path.rbegin(); it != path.rend(); ++it) {
    Root children(Call(target, "get_Children"));
    target = nullptr;
    for (int i = 0, count = Count(children.get()); i < count; ++i)
      if (HasLabel(Item(children.get(), i), it->c_str())) {
        target = Item(children.get(), i);
        break;
      }
    if (!target)
      return;
  }
  Root  selected(target);
  void* args[]{selected.get()};
  Invoke(PageMeta().selected, controller.get(), args);
  Root active(Call(canvas.get(), "get_SelectedOption"));
  if (sectionPage.page && sectionPage.page->id == pageId && active.get() == selected.get()
      && Target(sectionPage.controller) == controller.get()) {
    sectionPage.searchReturn = il2cpp_gchandle_new_weakref(returnToSearch.get(), false);
    if (!sectionPage.searchReturn)
      Warn("search return unavailable; using normal Back destination");
    if (const auto* heading = sectionPage.page->SectionFor(itemId); heading && Collapsed(*heading))
      sectionPage.sections.Toggle(*heading);
    RefreshActions();
  }
}
void RefreshConditionalSections()
{
  if (sectionPage.conditional)
    RefreshPageRows();
}
void AddHeadingRow(Il2CppObject* director, Il2CppObject* context, Il2CppObject* parent,
                   const PageCatalog::Heading& heading)
{
  if (heading.collapsible) {
    Root  id(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(heading.id.c_str())));
    Root  state(MakeDelegate(il2cpp_class_from_type(PageMeta().add->parameters[3]), director, QueryMethod()));
    void* args[] = {parent, id.get(), id.get(), state.get()};
    Root  row(Invoke(PageMeta().add, context, args));
    if (!CollapsibleHeadingFor(row.get()))
      throw std::runtime_error("settings section identity");
    return;
  }
  if (!headingsActive)
    throw std::runtime_error("settings heading adapter missing");
  const auto* add = HeadingMeta().add;
  Root        children(Call(parent, "get_Children"));
  const int   before = Count(children.get());
  if (before == PageCatalog::NativeChildLimit)
    throw std::runtime_error("settings heading capacity");
  Root  label(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(heading.id.c_str())));
  Root  get(MakeDelegate(il2cpp_class_from_type(add->parameters[2]), director, headingGetter.method()));
  Root  state(MakeDelegate(il2cpp_class_from_type(add->parameters[3]), director, QueryMethod()));
  void* args[] = {parent, label.get(), get.get(), state.get()};
  Invoke(add, context, args);
  if (Count(children.get()) != before + 1)
    throw std::runtime_error("settings heading insertion");
  Root row(Item(children.get(), before));
  if (!HeadingLabelFor(row.get()) || !HasLabel(row.get(), heading.id.c_str()))
    throw std::runtime_error("settings heading identity");
}

void AddPages(Il2CppObject* director, Il2CppObject* context)
{
  if (!pagesActive || Pages().empty())
    return;

  Root root(Call(context, "get_RootOption"));
  Root children(Call(root.get(), "get_Children"));
  for (int i = 0, count = Count(children.get()); i < count; ++i)
    if (HasLabel(Item(children.get(), i), Pages().front().id.c_str()))
      return;
  std::map<std::string, Il2CppObject*> parents;
  Il2CppObject*                        addedRoot = nullptr;
  std::optional<Root>                  addedRootGuard;
  try {
    for (const auto& page : Pages()) {
      auto* parent = page.parent.empty() ? root.get() : parents.at(page.parent);
      Root  id(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(page.id.c_str())));
      Root  state(MakeDelegate(il2cpp_class_from_type(PageMeta().add->parameters[3]), director, QueryMethod()));
      void* args[] = {parent, id.get(), id.get(), state.get()};
      Root  category(Invoke(PageMeta().add, context, args));
      if (!category.get())
        throw std::runtime_error("settings category construction");
      if (!addedRoot) {
        addedRoot = category.get();
        addedRootGuard.emplace(addedRoot);
      }
      if (!HasLabel(category.get(), page.id.c_str()))
        throw std::runtime_error("settings category identity");
      parents.emplace(page.id, category.get()); // Native root owns all added contexts.
      for (const auto& item : page.items)
        std::visit(
            [&](const auto& value) {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<T, PageCatalog::Heading>)
                AddHeadingRow(director, context, category.get(), value);
              else if constexpr (std::is_same_v<T, BooleanSetting*>)
                AddBooleanRow(director, context, category.get(), *value);
              else if constexpr (std::is_same_v<T, ChoiceSetting*>)
                AddChoiceRows(director, context, category.get(), *value);
              else if constexpr (std::is_same_v<T, ActionSetting*>) {
                for (std::size_t index = 0, count = value->Count(); index < count; ++index)
                  AddActionRow(director, context, category.get(), *value, index);
              } else
                AddSliderRow(director, context, category.get(), *value);
            },
            item);
    }
    // Unsupported leaf adapters can leave empty groups; remove them bottom-up.
    for (auto it = Pages().rbegin(); it != Pages().rend(); ++it) {
      auto*      category = parents.at(it->id);
      Root       items(Call(category, "get_Children"));
      const auto count  = Count(items.get());
      const auto action = count == 1 && ActionsActive() ? ActionFor(Item(items.get(), 0)).first : nullptr;
      // A failure-only notice is not supported content. If a widget family was
      // unavailable, prune notice-only leaves and then their notice-only parents.
      if (count != 0 && !(action && action->id() == saveNoticeId))
        continue;
      auto* parent = it->parent.empty() ? root.get() : parents.at(it->parent);
      void* args[] = {category};
      Call(parent, "RemoveChild", 1, args);
    }
  } catch (...) {
    if (addedRoot) {
      void* args[] = {addedRoot};
      Call(root.get(), "RemoveChild", 1, args);
    }
    throw;
  }
}

void RefreshSavePresentations()
{
  RefreshViews();
  RefreshActions();
}

void InstallPages()
{
  RegisterSettingsSearch(ModPages());
  RegisterModPages();
  RegisterMaintenancePages(ModPages());
  pagePlan = ModPages().Build();
  // Add after empty-page pruning so a notice never creates an otherwise empty group.
  static ActionSetting saveNotice{saveNoticeId, "Save notice",
                                  [](std::size_t) {
                                    return ActionSetting::Presentation{
                                        "<color=#FFC66D>Some changes weren't saved. See Defaults & Help.</color>", "",
                                        "", false, runtime_config::HasSaveFailures()};
                                  },
                                  [](std::size_t) {}};
  for (auto& page : pagePlan)
    page.items.insert(page.items.begin(), &saveNotice);
  std::size_t rowCount = 2; // FC and FT live on the native confirmation page.
  for (const auto& page : Pages())
    rowCount += page.ControlRows() - std::ranges::distance(page.Controls<ActionSetting>());
  PrepareValueViews(rowCount);
  if (Pages().empty())
    return;
  auto&            m = PageMeta();
  const std::array hooks{m.bind, m.release, m.selected, m.destroyed};
  const std::array hookNames{"CategoryOptionWidget.OnDidBindContext", "CategoryOptionWidget.OnAboutToReleaseContext",
                             "GameSettingsViewController.OnCategorySelected", "GameSettingsViewController.OnDestroy"};
  for (std::size_t i = 0; i < hooks.size(); ++i) {
    if (!Instance(hooks[i], i == 2 ? 1 : 0, IL2CPP_TYPE_VOID))
      throw std::runtime_error(std::string(hookNames[i]) + ": incompatible instance Void hook signature");
    for (std::size_t j = 0; j < i; ++j)
      if (hooks[i]->methodPointer == hooks[j]->methodPointer)
        throw std::runtime_error(std::string(hookNames[i]) + " shares target with " + hookNames[j]);
    const auto& core = ToggleMeta();
    for (auto* owned :
         {core.addGeneral, core.refresh, core.changed, core.release, core.reload, core.session, core.load})
      if (hooks[i]->methodPointer == owned->methodPointer)
        throw std::runtime_error(std::string(hookNames[i]) + " shares target with " + owned->klass->name + "."
                                 + owned->name);
  }
  if (!Instance(m.add, 4, IL2CPP_TYPE_CLASS) || !Reference(m.add->parameters[0])
      || !Type(m.add->parameters[1], IL2CPP_TYPE_STRING) || !Type(m.add->parameters[2], IL2CPP_TYPE_STRING)
      || !Reference(m.add->parameters[3]))
    throw std::runtime_error(
        "SettingsContext.AddCategory: expected instance reference result and (reference, String, String, reference)");
  if (!Reference(m.selected->parameters[0]))
    throw std::runtime_error("GameSettingsViewController.OnCategorySelected: expected reference parameter");
  InstallChoiceAndSliderWidgets();
  if (std::any_of(Pages().begin(), Pages().end(), [](const auto& page) {
        return std::any_of(page.items.begin(), page.items.end(), [](const auto& item) {
          const auto* heading = std::get_if<PageCatalog::Heading>(&item);
          return (heading && !heading->collapsible) || std::holds_alternative<ChoiceSetting*>(item);
        });
      })) {
    auto&       heading = HeadingMeta();
    const auto* get     = ToggleMeta().director.GetMethodInfo("GetClientVersion", 0);
    if (!Instance(get, 0, IL2CPP_TYPE_STRING))
      throw std::runtime_error("SettingsSectionDirector.GetClientVersion: expected instance String()");
    if (!Instance(heading.add, 4, IL2CPP_TYPE_VOID) || !Reference(heading.add->parameters[0])
        || !Type(heading.add->parameters[1], IL2CPP_TYPE_STRING) || !Reference(heading.add->parameters[2])
        || !Reference(heading.add->parameters[3]))
      throw std::runtime_error(
          "SettingsContext.AddText: expected instance Void(reference, String, reference, reference)");
    if (!heading.getContext || !Reference(heading.getContext->return_type)
        || !Instance(heading.getContext, 0, heading.getContext->return_type->type))
      throw std::runtime_error("TextOptionWidget.get_Context: expected instance reference result");
    if (!headingGetter.Initialize(get, EmptyHeadingValue))
      throw std::runtime_error("heading getter: incompatible native callback schema");
    const std::array targets{heading.refresh, heading.clear};
    const std::array names{"TextOptionWidget.SetWidgetData", "TextOptionWidget.ClearWidgetData"};
    for (std::size_t i = 0; i < targets.size(); ++i)
      if (!Instance(targets[i], 0, IL2CPP_TYPE_VOID))
        throw std::runtime_error(std::string(names[i]) + ": expected instance Void()");
    if (targets[0]->methodPointer == targets[1]->methodPointer)
      throw std::runtime_error("TextOptionWidget.SetWidgetData and ClearWidgetData share a native hook target");
    for (auto* target : targets) {
      const auto& core = ToggleMeta();
      for (auto* existing : {core.refresh, core.changed, core.release, core.addGeneral, core.reload, core.session,
                             core.load, m.bind, m.release, m.selected, m.destroyed})
        if (target->methodPointer == existing->methodPointer)
          throw std::runtime_error(std::string("TextOptionWidget.") + target->name + " shares target with "
                                   + existing->klass->name + "." + existing->name);
      if (SelectionActive())
        for (auto* existing : {SelectionMeta().refresh, SelectionMeta().changed, SelectionMeta().release})
          if (target->methodPointer == existing->methodPointer)
            throw std::runtime_error(std::string("TextOptionWidget.") + target->name + " shares target with "
                                     + existing->klass->name + "." + existing->name);
      if (SliderActive())
        for (auto* existing :
             {SliderMeta().refresh, SliderMeta().changed, SliderMeta().release, SliderMeta().valueLabel})
          if (target->methodPointer == existing->methodPointer)
            throw std::runtime_error(std::string("TextOptionWidget.") + target->name + " shares target with "
                                     + existing->klass->name + "." + existing->name);
    }
    ValidateHookFit(heading.refresh);
    if (!SPUD_STATIC_DETOUR(heading.refresh->methodPointer, HeadingRefreshHook))
      throw std::runtime_error("TextOptionWidget.SetWidgetData: hook installation failed");
    ValidateHookFit(heading.clear);
    if (!SPUD_STATIC_DETOUR(heading.clear->methodPointer, HeadingClearHook))
      throw std::runtime_error("TextOptionWidget.ClearWidgetData: hook installation failed");
    headingsActive = true;
  }
  InstallActionWidgets();
  InstallSettingsSearch();
  if (ActionsActive() && !runtime_config::SetSaveStatusObserver(RefreshSavePresentations))
    Warn("settings save notice refresh unavailable");
  for (const auto& page : Pages())
    for (auto* setting : page.Controls<BooleanSetting>()) {
      if (setting->id() == FleetCommanderConfirmationSetting().id() && setting != &FleetCommanderConfirmationSetting())
        throw std::runtime_error("settings owner collision");
      if (!setting->SetChangeObserver(RefreshViews))
        throw std::runtime_error("settings observer ownership");
    }
  ValidateHookFit(m.bind);
  if (!SPUD_STATIC_DETOUR(m.bind->methodPointer, CategoryBindHook))
    throw std::runtime_error("CategoryOptionWidget.OnDidBindContext: hook installation failed");
  ValidateHookFit(m.release);
  if (!SPUD_STATIC_DETOUR(m.release->methodPointer, CategoryReleaseHook))
    throw std::runtime_error("CategoryOptionWidget.OnAboutToReleaseContext: hook installation failed");
  ValidateHookFit(m.selected);
  if (!SPUD_STATIC_DETOUR(m.selected->methodPointer, PageSelectedHook))
    throw std::runtime_error("GameSettingsViewController.OnCategorySelected: hook installation failed");
  ValidateHookFit(m.destroyed);
  if (!SPUD_STATIC_DETOUR(m.destroyed->methodPointer, PageDestroyedHook))
    throw std::runtime_error("GameSettingsViewController.OnDestroy: hook installation failed");
  pagesActive = true;
  spdlog::info("[ModSettings] Native navigation installed: {} registered pages", Pages().size());
}

bool HeadingsActive()
{ return headingsActive; }

} // namespace mod_settings::native
#endif

#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "action_widgets.h"
#include "page_navigation.h"
#include "row_style.h"
#include "settings/native_boolean_callback.h"
#include "settings/shortcut_popup.h"
#include "settings/shortcut_settings.h"
#include <deque>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace mod_settings::native
{
namespace
{
  NativeCallback<void>          actionCallback;
  NativeCallback<Il2CppString*> actionGetter;
  bool                          actionsActive  = false;
  Il2CppObject*                 invokingWidget = nullptr;
} // namespace
Il2CppObject* InvokingActionWidget()
{ return invokingWidget; }
bool ActionsActive()
{ return actionsActive; }
struct ActionMetadata {
  IL2CppClassHelper widget =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "ButtonAndTextOptionWidget");
  IL2CppClassHelper row =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.GameSettings", "ButtonAndTextOptionContext");
  const MethodInfo* add         = ToggleMeta().context.GetMethodInfo("AddButtonAndText", 6);
  const MethodInfo* refresh     = widget.GetMethodInfo("SetWidgetData", 0);
  const MethodInfo* release     = widget.GetMethodInfo("OnAboutToReleaseContext", 0);
  const MethodInfo* getContext  = widget.GetMethodInfo("get_Context", 0);
  FieldInfo*        label       = Field(widget.get_cls(), "_label");
  FieldInfo*        buttonLabel = Field(widget.get_cls(), "_buttonLabel");
  FieldInfo*        valueLabel  = Field(widget.get_cls(), "_valueLabel");
  FieldInfo*        button      = Field(widget.get_cls(), "_button");
};
ActionMetadata& ActionMeta()
{
  static ActionMetadata metadata;
  return metadata;
}

Il2CppObject* ActionToken(Il2CppObject* context)
{
  if (!context || context->klass != ActionMeta().row.get_cls())
    return nullptr;
  Root safe(Call(context, "get_Callback"));
  if (!safe.get())
    return nullptr;
  Root callbacks(ReadField(safe.get(), Field(safe.get()->klass, "_callbacks")));
  if (Count(callbacks.get()) != 1)
    return nullptr;
  auto* callback = reinterpret_cast<Il2CppDelegate*>(Item(callbacks.get(), 0));
  return callback && callback->method == actionCallback.method()
                 && callback->method_ptr == actionCallback.method()->methodPointer
             ? callback->target
             : nullptr;
}
ActionRow ActionFor(Il2CppObject* context)
{
  if (!ActionToken(context))
    return {};
  Root        parent(Call(context, "get_Parent"));
  const auto* page = PageFor(parent.get());
  if (!page)
    return {};
  Root label(Call(context, "get_LabelContext"));
  Root identifier(Call(label.get(), "get_Identifier"));
  if (!identifier.get() || !Type(il2cpp_class_get_type(identifier.get()->klass), IL2CPP_TYPE_STRING))
    return {};
  auto*       text = reinterpret_cast<Il2CppString*>(identifier.get());
  std::string id;
  for (int i = 0; i < il2cpp_string_length(text); ++i) {
    const auto character = il2cpp_string_chars(text)[i];
    if (character > 127)
      return {};
    id += static_cast<char>(character);
  }
  for (auto* action : page->Controls<ActionSetting>())
    if (const auto index = action->item_index(id))
      return {action, *index};
  return {};
}
struct ActionView {
  Il2CppGCHandle                widget = nullptr, context = nullptr, token = nullptr;
  std::array<Il2CppGCHandle, 3> labels{};
  Il2CppGCHandle                button       = nullptr;
  Il2CppGCHandle                buttonObject = nullptr;
  ActionSetting*                action       = nullptr;
  std::size_t                   index        = 0;
  bool                          buttonHidden = false, buttonBefore = false;
  bool                          rendering = false, invoking = false;
};
std::deque<ActionView> actionViews;
void                   ClearAction(ActionView& view)
{
  view.action = nullptr;
  for (auto& handle : view.labels) {
    if (auto* label = Target(handle))
      ClearRowText(label);
    Free(handle);
  }
  try {
    if (auto* button = Target(view.button))
      Call(button, "ClearInteractable");
    if (view.buttonHidden)
      SetActive(Target(view.buttonObject), view.buttonBefore);
  } catch (...) {
    Warn("command view restoration unavailable");
  }
  Free(view.widget);
  Free(view.context);
  Free(view.token);
  Free(view.button);
  Free(view.buttonObject);
  view.buttonHidden = false;
}
void RenderAction(ActionView& view)
{
  if (!view.action || view.rendering)
    return;
  struct Scope {
    bool& value;
    Scope(bool& v)
        : value(v)
    { value = true; }
    ~Scope()
    { value = false; }
  } scope(view.rendering);
  Root widget(Target(view.widget)), context(Target(view.context));
  if (!widget.get() || !context.get() || Invoke(ActionMeta().getContext, widget.get()) != context.get()) {
    ClearAction(view);
    return;
  }
  auto*      action       = view.action;
  const auto index        = view.index;
  const auto presentation = action->Read(index);
  if (view.action != action || view.index != index || Target(view.context) != context.get()
      || Invoke(ActionMeta().getContext, widget.get()) != context.get())
    return;
  const std::array text{presentation.label, presentation.button, presentation.value};
  for (std::size_t i = 0; i < text.size(); ++i) {
    auto* label = Target(view.labels[i]);
    SetRowText(label, label, text[i]);
  }
  if (view.buttonHidden) {
    SetActive(Target(view.buttonObject), view.buttonBefore);
    view.buttonHidden = false;
  }
  if (presentation.button.empty()) {
    view.buttonBefore = Il2CppChecked::Boolean(Call(Target(view.buttonObject), "get_activeSelf"));
    view.buttonHidden = true;
    SetActive(Target(view.buttonObject), false);
  }
  bool  enabled = presentation.actionable();
  void* args[]  = {&enabled};
  Call(Target(view.button), "OverrideInteractable", 1, args);
}
void InvokeSliderEditor(Il2CppObject* widget, Il2CppObject* context, SliderSetting& slider)
{
  if (!OnUIThread() || !PagesActive() || PageRefreshInProgress() || !slider.exact_editor || !OwnsValueContext(context)
      || SliderFor(context) != &slider)
    return;
  Root owner(widget), bound(context);
  if (!slider.exact_editor->Read(0).actionable() || Invoke(SliderMeta().getContext, owner.get()) != bound.get())
    return;
  struct Invocation {
    Il2CppObject* previous;
    explicit Invocation(Il2CppObject* widget)
        : previous(invokingWidget)
    { invokingWidget = widget; }
    ~Invocation()
    { invokingWidget = previous; }
  } invocation(owner.get());
  slider.exact_editor->invoke(0);
}
void RefreshActions()
{
  if (!OnUIThread() || !actionsActive || !PagesActive() || PageRefreshInProgress())
    return;

  RefreshPageRows();
  RefreshPageSummaries();
  RefreshActionPresentations();
}
void RefreshActionPresentations()
{
  if (!OnUIThread() || !actionsActive || !PagesActive() || PageRefreshInProgress())
    return;
  // Binding during a callback may append a deque slot. References stay valid;
  // iterators do not, so visit only the slots that existed at entry.
  for (std::size_t i = 0, count = actionViews.size(); i < count; ++i) {
    auto& view = actionViews[i];
    try {
      RenderAction(view);
    } catch (...) {
      ClearAction(view);
      Warn("settings command presentation unavailable");
    }
  }
}
void InvokeAction(Il2CppObject* token, const MethodInfo*)
{
  if (!OnUIThread() || !actionsActive || !PagesActive() || PageRefreshInProgress())
    return;
  try {
    for (auto& view : actionViews) {
      if (!view.action || view.rendering || view.invoking || Target(view.token) != token)
        continue;
      struct Scope {
        bool& flag;
        Scope(bool& value)
            : flag(value)
        { flag = true; }
        ~Scope()
        { flag = false; }
      } scope(view.invoking);
      auto*      action = view.action;
      const auto index  = view.index;
      Root       widget(Target(view.widget)), context(Target(view.context));
      if (!widget.get() || !context.get() || Invoke(ActionMeta().getContext, widget.get()) != context.get()
          || ActionToken(context.get()) != token)
        return;
      // EventSystem can submit a focused button with Enter/Space while capture
      // is active. The command's current availability is authoritative too.
      const bool enabled = action->Read(index).actionable();
      // A feature-owned reader may release/rebind its row. Keep that callback
      // from authorizing a different command or recursively invoking itself.
      if (enabled && view.action == action && view.index == index && Target(view.context) == context.get()
          && Target(view.token) == token && Invoke(ActionMeta().getContext, widget.get()) == context.get()) {
        struct Invocation {
          Il2CppObject* previous;
          explicit Invocation(Il2CppObject* widget)
              : previous(invokingWidget)
          { invokingWidget = widget; }
          ~Invocation()
          { invokingWidget = previous; }
        } invocation(widget.get());
        action->invoke(index);
      }
      return;
    }
  } catch (...) {
    Warn("settings command unavailable");
  }
}
void ActionRefreshHook(auto original, Il2CppObject* widget)
{
  if (OnUIThread())
    for (std::size_t i = 0, count = actionViews.size(); i < count; ++i) {
      auto& view = actionViews[i];
      if (Target(view.widget) == widget) {
        // Pooling/refresh only releases the widget. The page owns its editor.
        ClearAction(view);
      }
    }
  original(widget);
  if (!OnUIThread() || !actionsActive || !PagesActive())
    return;
  ActionView* tracked = nullptr;
  try {
    Root       context(Invoke(ActionMeta().getContext, widget));
    const auto action = ActionFor(context.get());
    if (!action.first)
      return;
    for (auto& view : actionViews)
      if (!view.action && !view.rendering && !view.invoking) {
        tracked = &view;
        break;
      }
    if (!tracked) {
      actionViews.emplace_back();
      tracked = &actionViews.back();
    }
    auto& view  = *tracked;
    view.action = action.first;
    view.index  = action.second;
    auto weak   = [](Il2CppObject* object) {
      const auto handle = object ? il2cpp_gchandle_new_weakref(object, false) : nullptr;
      if (!handle)
        throw std::runtime_error("settings command weak root");
      return handle;
    };
    view.widget  = weak(widget);
    view.context = weak(context.get());
    view.token   = weak(ActionToken(context.get()));
    const std::array fields{ActionMeta().label, ActionMeta().buttonLabel, ActionMeta().valueLabel};
    for (std::size_t i = 0; i < fields.size(); ++i)
      view.labels[i] = weak(ReadField(widget, fields[i]));
    view.button = weak(ReadField(widget, ActionMeta().button));
    Root buttonObject(Call(Target(view.button), "get_gameObject"));
    view.buttonObject = weak(buttonObject.get());
    RenderAction(view);
  } catch (...) {
    if (tracked)
      ClearAction(*tracked);
    Warn("settings command binding unavailable");
  }
}
void ActionReleaseHook(auto original, Il2CppObject* widget)
{
  if (OnUIThread())
    for (std::size_t i = 0, count = actionViews.size(); i < count; ++i) {
      auto& view = actionViews[i];
      if (Target(view.widget) == widget)
        ClearAction(view);
    }
  original(widget);
}
void AddActionRow(Il2CppObject* director, Il2CppObject* context, Il2CppObject* parent, ActionSetting& action,
                  std::size_t index)
{
  if (!actionsActive)
    return;
  const auto* add = ActionMeta().add;
  Root        children(Call(parent, "get_Children"));
  const int   before = Count(children.get());
  if (before == PageCatalog::NativeChildLimit)
    throw std::runtime_error("settings command capacity");
  Root label(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(action.item_id(index).c_str())));
  Root empty(reinterpret_cast<Il2CppObject*>(il2cpp_string_new("")));
  // A fresh plain Object is the closed callback's identity, owned by this native
  // context. Old pooled-row events cannot invoke a later context's command.
  Root  token(il2cpp_object_new(il2cpp_class_from_name(il2cpp_get_corlib(), "System", "Object")));
  Root  callback(MakeDelegate(il2cpp_class_from_type(add->parameters[3]), token.get(), actionCallback.method()));
  Root  get(MakeDelegate(il2cpp_class_from_type(add->parameters[4]), director, actionGetter.method()));
  void* args[] = {parent, label.get(), empty.get(), callback.get(), get.get(), empty.get()};
  Invoke(add, context, args);
  if (Count(children.get()) != before + 1)
    throw std::runtime_error("settings command insertion");
  Root row(Item(children.get(), before));
  if (ActionFor(row.get()) != ActionRow{&action, index} || ActionToken(row.get()) != token.get())
    throw std::runtime_error("settings command identity");
}
void SyncActionRows(Il2CppObject* controller, Il2CppObject* context, const PageCatalog::Page& page)
{
  if (!actionsActive)
    return;
  auto actions = page.Controls<ActionSetting>();
  if (actions.empty())
    return;
  Root                   children(Call(context, "get_Children"));
  const auto             size = Count(children.get());
  std::vector<ActionRow> existing, missing;
  for (int i = 0; i < size; ++i)
    if (const auto row = ActionFor(Item(children.get(), i)); row.first)
      existing.push_back(row);
  for (auto* action : actions)
    for (std::size_t index = 0, count = action->Count(); index < count; ++index) {
      const ActionRow row{action, index};
      if (std::find(existing.begin(), existing.end(), row) != existing.end())
        continue;
      // The adapter already bounds every native child list at 128. Check the
      // full addition before mutating it, including rows retained after removal.
      if (size + missing.size() == PageCatalog::NativeChildLimit)
        throw std::runtime_error("settings command capacity");
      missing.push_back(row);
    }
  if (missing.empty())
    return;
  Root  callbackObject(ReadField(context, ToggleMeta().queryField));
  auto* callback = reinterpret_cast<Il2CppDelegate*>(callbackObject.get());
  if (!callback || callback->method != QueryMethod() || callback->method_ptr != QueryMethod()->methodPointer
      || !callback->target || callback->target->klass != ToggleMeta().director.get_cls())
    throw std::runtime_error("settings command page owner");
  Root director(callback->target);
  Root canvas(Call(controller, "get_CanvasContext"));
  Root selected(Call(canvas.get(), "get_SelectedOption"));
  if (!canvas.get() || canvas.get()->klass != ToggleMeta().context.get_cls() || selected.get() != context)
    throw std::runtime_error("settings command page changed");
  // Contexts remain owned by the native page up to its largest binding count.
  // Shrinking hides surplus indices; later additions reuse those same contexts.
  for (const auto& [action, index] : missing)
    AddActionRow(director.get(), canvas.get(), context, *action, index);
}
void InstallActionWidgets()
{
  auto& m = PageMeta();
  if (std::any_of(Pages().begin(), Pages().end(),
                  [](const auto& page) { return !page.template Controls<ActionSetting>().empty(); })) {
    try {
      auto&       action      = ActionMeta();
      auto*       objects     = il2cpp_class_from_name(il2cpp_get_corlib(), "System", "Object");
      const auto* clickSchema = objects ? il2cpp_class_get_method_from_name(objects, ".ctor", 0) : nullptr;
      const auto* getSchema   = ToggleMeta().director.GetMethodInfo("GetClientVersion", 0);
      if (!Instance(action.add, 6, IL2CPP_TYPE_VOID) || !Reference(action.add->parameters[0])
          || !Type(action.add->parameters[1], IL2CPP_TYPE_STRING)
          || !Type(action.add->parameters[2], IL2CPP_TYPE_STRING) || !Reference(action.add->parameters[3])
          || !Reference(action.add->parameters[4]) || !Type(action.add->parameters[5], IL2CPP_TYPE_STRING))
        throw std::runtime_error("SettingsContext.AddButtonAndText: expected instance Void(reference, String, String, "
                                 "reference, reference, String)");
      if (!action.getContext || !Reference(action.getContext->return_type)
          || !Instance(action.getContext, 0, action.getContext->return_type->type))
        throw std::runtime_error("ButtonAndTextOptionWidget.get_Context: expected instance reference result");
      if (!Instance(clickSchema, 0, IL2CPP_TYPE_VOID))
        throw std::runtime_error("System.Object..ctor: expected instance Void() callback donor");
      if (!Instance(getSchema, 0, IL2CPP_TYPE_STRING))
        throw std::runtime_error("SettingsSectionDirector.GetClientVersion: expected instance String()");
      if (!actionCallback.Initialize(clickSchema, InvokeAction))
        throw std::runtime_error("command callback: incompatible native callback schema");
      if (!actionGetter.Initialize(getSchema, EmptyHeadingValue))
        throw std::runtime_error("command text getter: incompatible native callback schema");
      // build261 x64 GameAssembly 487af4bb: SetWidgetData CFD5F0..CFD8A5 (693)
      // and OnAboutToReleaseContext CFD430..CFD53F (271), vs SPUD's 24 bytes.
      // Resolve current addresses through managed metadata.
      const std::array targets{action.refresh, action.release};
      const std::array names{"ButtonAndTextOptionWidget.SetWidgetData",
                             "ButtonAndTextOptionWidget.OnAboutToReleaseContext"};
      for (std::size_t i = 0; i < targets.size(); ++i)
        if (!Instance(targets[i], 0, IL2CPP_TYPE_VOID))
          throw std::runtime_error(std::string(names[i]) + ": expected instance Void()");
      if (targets[0]->methodPointer == targets[1]->methodPointer)
        throw std::runtime_error(
            "ButtonAndTextOptionWidget.SetWidgetData and OnAboutToReleaseContext share a native hook target");
      for (auto* target : targets) {
        for (auto* existing :
             {ToggleMeta().refresh, ToggleMeta().changed, ToggleMeta().release, ToggleMeta().addGeneral,
              ToggleMeta().reload, ToggleMeta().session, ToggleMeta().load, m.bind, m.release, m.selected, m.destroyed})
          if (target->methodPointer == existing->methodPointer)
            throw std::runtime_error(std::string("ButtonAndTextOptionWidget.") + target->name + " shares target with "
                                     + existing->klass->name + "." + existing->name);
        if (SelectionActive())
          for (auto* existing : {SelectionMeta().refresh, SelectionMeta().changed, SelectionMeta().release})
            if (target->methodPointer == existing->methodPointer)
              throw std::runtime_error(std::string("ButtonAndTextOptionWidget.") + target->name + " shares target with "
                                       + existing->klass->name + "." + existing->name);
        if (SliderActive())
          for (auto* existing :
               {SliderMeta().refresh, SliderMeta().changed, SliderMeta().release, SliderMeta().valueLabel})
            if (target->methodPointer == existing->methodPointer)
              throw std::runtime_error(std::string("ButtonAndTextOptionWidget.") + target->name + " shares target with "
                                       + existing->klass->name + "." + existing->name);
        if (HeadingsActive())
          for (auto* existing : {HeadingMeta().refresh, HeadingMeta().clear})
            if (target->methodPointer == existing->methodPointer)
              throw std::runtime_error(std::string("ButtonAndTextOptionWidget.") + target->name + " shares target with "
                                       + existing->klass->name + "." + existing->name);
      }
      ValidateHookFit(action.refresh);
      if (!SPUD_STATIC_DETOUR(action.refresh->methodPointer, ActionRefreshHook))
        throw std::runtime_error("ButtonAndTextOptionWidget.SetWidgetData: hook installation failed");
      ValidateHookFit(action.release);
      if (!SPUD_STATIC_DETOUR(action.release->methodPointer, ActionReleaseHook))
        throw std::runtime_error("ButtonAndTextOptionWidget.OnAboutToReleaseContext: hook installation failed");
      actionsActive = true;
      InstallShortcutPopup();
      SetShortcutPresentationObserver(RefreshActions);
    } catch (const std::exception&) {
      Warn("settings command installation unavailable");
    }
  }
}

} // namespace mod_settings::native
#endif

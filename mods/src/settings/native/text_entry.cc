#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "settings/text_entry.h"
#include "action_widgets.h"
#include "patches/key.h"
#include "patches/screen_update_hook.h"
#include "prime/Vector3.h"
#include "row_style.h"
#include "settings/native_boolean_callback.h"
#include "settings_search.h"
#include "str_utils.h"
#include "ui_helpers.h"
#include "value_widgets.h"
#include <algorithm>
#include <array>
#include <optional>
#include <spdlog/spdlog.h>
#include <vector>

namespace mod_settings
{
namespace
{
  using namespace native;
  using namespace native::ui;
  struct Vec2 {
    float x, y;
  };
  struct Color {
    float r, g, b, a;
  };
  struct Rect {
    float x, y, width, height;
  };
  struct Editor {
    std::optional<TextEntryRequest> request;
    Il2CppGCHandle                  object{}, owner{}, input{}, status{}, scroll{}, apply{}, back{}, navigation{};
    std::array<Il2CppGCHandle, 2>   tokens{};
    bool                            previousBack = false, previousNavigation = false;
    bool        backHeld = false, navigationHeld = false, invoking = false, releaseFrame = false, opening = false;
    std::string value, error;
    float       statusHeight = 130;
  } editor;
  NativeCallback<void> clickCallback;
  bool                 installed = false;
  bool (*focused)()              = nullptr;

  void DestroySurface()
  {
    if (Alive(Target(editor.object))) {
      SetActive(Target(editor.object), false);
      void* args[]{Target(editor.object)};
      Static(IL2CppClassHelper(UnityClass("Object")).GetMethodInfo("Destroy", 1), args);
    }
  }
  void ReleaseInput()
  {
    if (editor.backHeld && Alive(Target(editor.back)))
      Value(Target(editor.back), "set_enabled", editor.previousBack);
    if (editor.navigationHeld && Alive(Target(editor.navigation)))
      Value(Target(editor.navigation), "set_sendNavigationEvents", editor.previousNavigation);
    editor.backHeld = editor.navigationHeld = false;
    Free(editor.back);
    Free(editor.navigation);
    editor.releaseFrame  = false;
    Key::textEntryActive = false;
  }
  void HoldInput(Il2CppObject* back)
  {
    auto* cls = Class("UnityEngine.UI", "UnityEngine.EventSystems", "EventSystem");
    Root  current(Static(IL2CppClassHelper(cls).GetMethodInfo("get_current", 0), nullptr));
    if (!Alive(back) || !current.get())
      throw std::runtime_error("text editor navigation owner unavailable");
    Retain(editor.back, back);
    Retain(editor.navigation, current.get());
    editor.previousBack       = Boolean(UiCall(back, "get_enabled"));
    editor.previousNavigation = Boolean(UiCall(current.get(), "get_sendNavigationEvents"));
    Key::textEntryActive      = true; // Cleanup also drains a partially acquired hold.
    editor.backHeld           = true;
    Value(back, "set_enabled", false);
    editor.navigationHeld = true;
    Value(current.get(), "set_sendNavigationEvents", false);
  }
  Il2CppObject* NewObject(const char* name, Il2CppObject* parent)
  {
    Root        object(il2cpp_object_new(UnityClass("GameObject")));
    Root        label(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(name)));
    const auto* ctor = IL2CppClassHelper(UnityClass("GameObject")).GetMethodInfoSpecial(".ctor", [](auto n, auto p) {
      return n == 1 && Type(p[0], IL2CPP_TYPE_STRING);
    });
    void*       args[]{label.get()};
    Invoke(ctor, object.get(), args);
    if (!parent)
      Retain(editor.object, object.get());
    Root transform(UiCall(object.get(), "get_transform"));
    if (parent) {
      bool  world = false;
      void* parentArgs[]{parent, &world};
      UiCall(transform.get(), "SetParent", 2, parentArgs);
    }
    WithType(object.get(), "AddComponent", UnityClass("RectTransform"));
    Value(object.get(), "set_layer", 5);
    return object.get();
  }
  void Layout(Il2CppObject* transform, Vec2 size, Vec2 position)
  {
    Value(transform, "set_anchorMin", Vec2{.5f, .5f});
    Value(transform, "set_anchorMax", Vec2{.5f, .5f});
    Value(transform, "set_pivot", Vec2{.5f, .5f});
    Value(transform, "set_sizeDelta", size);
    Value(transform, "set_anchoredPosition", position);
  }
  Il2CppObject* Image(Il2CppObject* object, Color color)
  {
    Root image(WithType(object, "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "Image")));
    Value(image.get(), "set_color", color);
    return image.get();
  }
  Il2CppObject* Label(Il2CppObject* parent, Il2CppObject* font, const char* name, Vec2 size, Vec2 position,
                      float fontSize, const std::string& text)
  {
    Root child(NewObject(name, parent));
    Root transform(UiCall(child.get(), "get_transform"));
    Layout(transform.get(), size, position);
    Root label(WithType(child.get(), "AddComponent", Class("Unity.TextMeshPro", "TMPro", "TextMeshProUGUI")));
    Set(label.get(), "set_font", font);
    Value(label.get(), "set_fontSize", fontSize);
    Value(label.get(), "set_alignment", 0x202);
    Value(label.get(), "set_color", Color{.88f, .95f, .97f, 1});
    Value(label.get(), "set_raycastTarget", false);
    Value(label.get(), "set_richText", false);
    Value(label.get(), "set_enableWordWrapping", true);
    Text(label.get(), text);
    return label.get();
  }
  std::string ReadText()
  {
    Root value(UiCall(Target(editor.input), "get_text"));
    if (!value.get() || !Type(il2cpp_class_get_type(value.get()->klass), IL2CPP_TYPE_STRING))
      throw std::runtime_error("text editor value unavailable");
    return to_string(reinterpret_cast<Il2CppString*>(value.get()));
  }
  void ShowStatus(const std::string& text)
  {
    Text(Target(editor.status), text);
    Root preferred(UiCall(Target(editor.status), "get_preferredHeight"));
    if (!preferred.get() || !Type(il2cpp_class_get_type(preferred.get()->klass), IL2CPP_TYPE_R4))
      throw std::runtime_error("text editor help layout unavailable");
    Root transform(UiCall(Target(editor.status), "get_transform"));
    Value(transform.get(), "set_sizeDelta",
          Vec2{590, std::max(editor.statusHeight, *static_cast<float*>(il2cpp_object_unbox(preferred.get())))});
    Value(Target(editor.scroll), "set_verticalNormalizedPosition", 1.f);
  }
  void Validate()
  {
    editor.value = ReadText();
    editor.error = editor.request->validate ? editor.request->validate(editor.value) : std::string{};
    ShowStatus(editor.error.empty() ? editor.request->help : editor.error);
    Value(Target(editor.apply), "set_interactable", editor.error.empty());
  }
  void Cancel()
  {
    auto cancel = editor.request ? editor.request->cancel : std::function<void()>{};
    CloseTextEntry();
    if (cancel) {
      try {
        cancel();
      } catch (...) {
        Warn("text editor cancel callback unavailable");
      }
    }
  }
  void Apply()
  {
    Validate();
    if (!editor.error.empty())
      return;
    // Keep the request alive if commit synchronously closes the settings page.
    const auto request = *editor.request;
    const auto value   = editor.value;
    if (!request.commit(value)) {
      if (editor.request)
        ShowStatus("Setting changed or could not be applied. Cancel and reopen to try again.");
      return;
    }
    CloseTextEntry();
    RefreshActions();
  }
  void Click(Il2CppObject* token, const MethodInfo*)
  {
    if (editor.invoking || !OnUIThread() || !editor.request)
      return;
    struct Scope {
      Scope()
      { editor.invoking = true; }
      ~Scope()
      { editor.invoking = false; }
    } scope;
    try {
      if (editor.opening)
        return;
      if (!focused() || !Alive(Target(editor.object)) || !Alive(Target(editor.owner))) {
        Cancel();
        return;
      }
      if (token == Target(editor.tokens[0]))
        Apply();
      else if (token == Target(editor.tokens[1]))
        Cancel();
    } catch (const std::exception& error) {
      spdlog::warn("[ModSettings] Text edit failed: {}", error.what());
      Cancel();
    }
  }
  void Button(Il2CppObject* parent, Il2CppObject* font, int index, const char* title, float x)
  {
    Root object(NewObject(title, parent));
    Root transform(UiCall(object.get(), "get_transform"));
    Layout(transform.get(), {190, 52}, {x, -166});
    Root image(Image(object.get(), {.18f, .34f, .43f, 1}));
    Root button(WithType(object.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "Button")));
    Set(button.get(), "set_targetGraphic", image.get());
    Label(transform.get(), font, "Label", {178, 46}, {0, 0}, 24, title);
    if (index == 0)
      Retain(editor.apply, button.get());
    Root token(il2cpp_object_new(il2cpp_class_from_name(il2cpp_get_corlib(), "System", "Object")));
    Retain(editor.tokens[index], token.get());
    Root        event(UiCall(button.get(), "get_onClick"));
    const auto* add = IL2CppClassHelper(event.get()->klass).GetMethodInfo("AddListener", 1);
    if (!Instance(add, 1, IL2CPP_TYPE_VOID) || !Reference(add->parameters[0]))
      throw std::runtime_error("text editor button event");
    Root  callback(MakeDelegate(il2cpp_class_from_type(add->parameters[0]), token.get(), clickCallback.method()));
    void* args[]{callback.get()};
    Invoke(add, event.get(), args);
  }
  void Tick()
  {
    if (!editor.request && !Key::textEntryActive)
      return;
    try {
      if (!editor.request) {
        // Consume the dismissal frame and all held keys, including a mouse click.
        if (!focused() || Key::AnyRawPressed())
          editor.releaseFrame = false;
        else if (editor.releaseFrame)
          ReleaseInput();
        else
          editor.releaseFrame = true;
        return;
      }
      if (!focused() || !Alive(Target(editor.object)) || !Alive(Target(editor.owner))
          || !Boolean(UiCall(Target(editor.owner), "get_activeInHierarchy"))) {
        Cancel();
        return;
      }
      // A key or pointer click that opened the editor cannot also apply/cancel it.
      if (editor.opening) {
        if (Key::AnyRawPressed())
          return;
        editor.opening = false;
      }
      if (Key::RawDown(KeyCode::Escape)) {
        Cancel();
        return;
      }
      if (ReadText() != editor.value)
        Validate();
      // Enter explicitly applies; multiline TOML arrays are entered as a single
      // TOML expression, so keyboard Submit never adds an invisible newline.
      if (Key::RawDown(KeyCode::Return) || Key::RawDown(KeyCode::KeypadEnter))
        Apply();
    } catch (const std::exception& error) {
      spdlog::warn("[ModSettings] Text editor closed: {}", error.what());
      Cancel();
    }
  }
} // namespace

bool TextEntryActive()
{ return editor.request.has_value(); }
void CloseTextEntry()
{
  editor.request.reset();
  editor.releaseFrame = false;
  editor.opening      = false;
  try {
    DestroySurface();
  } catch (...) {
    Warn("text editor cleanup unavailable");
  }
  for (auto* handle : {&editor.object, &editor.owner, &editor.input, &editor.status, &editor.scroll, &editor.apply})
    Free(*handle);
  for (auto& token : editor.tokens)
    Free(token);
}
bool OpenTextEntry(TextEntryRequest request)
{
  if (!OnUIThread() || !request.commit || editor.request || Key::textEntryActive || Key::shortcutPopupActive
      || Key::shortcutCaptureActive || !InvokingActionWidget())
    return false;
  try {
    if (!installed) {
      auto*       object = il2cpp_class_from_name(il2cpp_get_corlib(), "System", "Object");
      const auto* ctor   = object ? il2cpp_class_get_method_from_name(object, ".ctor", 0) : nullptr;
      focused            = il2cpp_resolve_icall_typed<bool()>("UnityEngine.Application::get_isFocused()");
      installed = focused && clickCallback.Initialize(ctor, Click) && register_screen_manager_update_callback(Tick);
      if (!installed)
        return false;
    }
    CloseSettingsSearch(true);
    Root host(InvokingActionWidget());
    Root hostObject(UiCall(host.get(), "get_gameObject"));
    Root settings(WithType(hostObject.get(), "GetComponentInParent",
                           Class("Assembly-CSharp", "Digit.Prime.GameSettings", "GameSettingsViewController"), 2));
    if (!settings.get())
      throw std::runtime_error("text editor settings controller unavailable");
    Root back(ReadField(settings.get(), Field(settings.get()->klass, "_backButtonViewController")));
    Root canvas(
        WithType(hostObject.get(), "GetComponentInParent", Class("UnityEngine.UIModule", "UnityEngine", "Canvas"), 2));
    Root parent(UiCall(canvas.get(), "get_transform"));
    Retain(editor.owner, UiCall(settings.get(), "get_gameObject"));
    Root localizer(ReadField(host.get(), Field(host.get()->klass, "_label")));
    Root labelObject(UiCall(localizer.get(), "get_gameObject"));
    Root source(WithType(labelObject.get(), "GetComponent", Class("Unity.TextMeshPro", "TMPro", "TMP_Text")));
    Root font(UiCall(source.get(), "get_font"));
    if (!font.get())
      throw std::runtime_error("text editor font unavailable");
    Root root(NewObject("CommunityMod_TextEditor", nullptr));
    SetActive(root.get(), false);
    Root  transform(UiCall(root.get(), "get_transform"));
    bool  world = false;
    void* args[]{parent.get(), &world};
    UiCall(transform.get(), "SetParent", 2, args);
    Value(transform.get(), "set_anchorMin", Vec2{0, 0});
    Value(transform.get(), "set_anchorMax", Vec2{1, 1});
    Value(transform.get(), "set_sizeDelta", Vec2{0, 0});
    Value(transform.get(), "set_anchoredPosition", Vec2{0, 0});
    UiCall(transform.get(), "SetAsLastSibling");
    Image(root.get(), {0, 0, 0, .5f});
    Root panel(NewObject("Panel", transform.get()));
    Root panelTransform(UiCall(panel.get(), "get_transform"));
    Layout(panelTransform.get(), {680, 430}, {0, 0});
    Image(panel.get(), {.10f, .18f, .24f, 1});
    Root rect(UiCall(parent.get(), "get_rect"));
    if (rect.get() && rect.get()->klass == UnityClass("Rect")) {
      const auto bounds = *static_cast<Rect*>(il2cpp_object_unbox(rect.get()));
      const auto scale  = std::min({1.f, bounds.width / 710.f, bounds.height / 460.f});
      if (scale <= 0)
        throw std::runtime_error("text editor canvas size unavailable");
      Value(panelTransform.get(), "set_localScale", Vector3{scale, scale, 1});
    }
    Label(panelTransform.get(), font.get(), "Title", {620, 60}, {0, 162}, 28, request.title);
    Root inputObject(NewObject("Input", panelTransform.get()));
    Root inputRect(UiCall(inputObject.get(), "get_transform"));
    Layout(inputRect.get(), {610, 80}, {0, 72});
    Root background(Image(inputObject.get(), {.06f, .12f, .16f, 1}));
    Root viewport(NewObject("Text Area", inputRect.get()));
    Root viewportRect(UiCall(viewport.get(), "get_transform"));
    Layout(viewportRect.get(), {580, 64}, {0, 0});
    WithType(viewport.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "RectMask2D"));
    Root text(Label(viewportRect.get(), font.get(), "Text", {580, 64}, {0, 0}, 25, ""));
    Value(text.get(), "set_alignment", 0x201);
    Value(text.get(), "set_enableWordWrapping", false);
    Root field(WithType(inputObject.get(), "AddComponent", Class("Unity.TextMeshPro", "TMPro", "TMP_InputField")));
    Retain(editor.input, field.get());
    Set(field.get(), "set_targetGraphic", background.get());
    Set(field.get(), "set_textViewport", viewportRect.get());
    Set(field.get(), "set_textComponent", text.get());
    Value(field.get(), "set_characterLimit", std::max(32768, static_cast<int>(request.value.size())));
    Value(field.get(), "set_lineType", 0);
    Value(field.get(), "set_richText", false);
    Value(field.get(), "set_restoreOriginalTextOnEscape", false);
    Value(field.get(), "set_readOnly", request.read_only);
    Text(field.get(), request.value);
    const bool details = request.read_only && request.value.empty();
    if (details)
      SetActive(inputObject.get(), false);
    editor.statusHeight = details ? 250.f : 130.f;
    Root statusArea(NewObject("Help", panelTransform.get()));
    Root statusRect(UiCall(statusArea.get(), "get_transform"));
    Layout(statusRect.get(), {610, editor.statusHeight}, {0, details ? 2.f : -58.f});
    WithType(statusArea.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "RectMask2D"));
    Image(statusArea.get(), {0, 0, 0, .01f});
    Root status(Label(statusRect.get(), font.get(), "Status", {590, editor.statusHeight}, {0, 0}, 21, request.help));
    Root statusTransform(UiCall(status.get(), "get_transform"));
    Value(statusTransform.get(), "set_anchorMin", Vec2{.5f, 1});
    Value(statusTransform.get(), "set_anchorMax", Vec2{.5f, 1});
    Value(statusTransform.get(), "set_pivot", Vec2{.5f, 1});
    Value(status.get(), "set_alignment", 0x102);
    Retain(editor.status, status.get());
    Root scroll(WithType(statusArea.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "ScrollRect")));
    Set(scroll.get(), "set_viewport", statusRect.get());
    Set(scroll.get(), "set_content", statusTransform.get());
    Value(scroll.get(), "set_horizontal", false);
    Value(scroll.get(), "set_vertical", true);
    Value(scroll.get(), "set_movementType", 2);
    Value(scroll.get(), "set_scrollSensitivity", 24.f);
    Retain(editor.scroll, scroll.get());
    Button(panelTransform.get(), font.get(), 1, "Cancel", -124);
    Button(panelTransform.get(), font.get(), 0, request.apply_label.c_str(), 124);
    HoldInput(back.get());
    editor.request = std::move(request);
    editor.opening = true;
    Validate();
    SetActive(root.get(), true);
    if (!editor.request->read_only) {
      UiCall(field.get(), "Select");
      UiCall(field.get(), "ActivateInputField");
    }
    return true;
  } catch (const std::exception& error) {
    spdlog::warn("[ModSettings] Text editor unavailable: {}", error.what());
    CloseTextEntry();
    return false;
  }
}
} // namespace mod_settings
#endif

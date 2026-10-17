#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "settings/shortcut_popup.h"
#include "action_widgets.h"
#include "patches/key.h"
#include "patches/screen_update_hook.h"
#include "prime/Vector3.h"
#include "row_style.h"
#include "settings_search.h"
#include "settings/native_boolean_callback.h"
#include "settings/shortcut_popup_keys.h"
#include "ui_helpers.h"
#include "value_widgets.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>

namespace mod_settings::native
{
namespace
{
  using namespace ui;
  struct Vec2 {
    float x, y;
  };
  struct Color {
    float r, g, b, a;
  };
  struct Rect {
    float x, y, width, height;
  };
  struct Popup {
    Il2CppGCHandle                object{}, owner{}, status{}, proposed{}, current{}, title{}, confirmText{}, scroll{};
    std::array<Il2CppGCHandle, 3> buttons{}, tokens{};
    ShortcutPopupCommands         commands;
    std::optional<ShortcutPopupPresentation> rendered;
    ShortcutPopupKeys                        keys;
  } popup;
  Il2CppGCHandle       navigationSystem   = nullptr;
  Il2CppGCHandle       backController     = nullptr;
  bool                 previousNavigation = false, releaseNavigationNextTick = false;
  bool                 previousBackEnabled = false;
  NativeCallback<void> clickCallback;
  bool                 installed = false, invoking = false;
  bool (*focused)() = nullptr;

  void ReleaseNavigation()
  {
    if (Alive(Target(backController))) {
      Value(Target(backController), "set_enabled", previousBackEnabled);
      if (Boolean(UiCall(Target(backController), "get_enabled")) != previousBackEnabled)
        throw std::runtime_error("shortcut popup Back controller restore failed");
    }
    Free(backController);
    if (Alive(Target(navigationSystem))) {
      Value(Target(navigationSystem), "set_sendNavigationEvents", previousNavigation);
      if (Boolean(UiCall(Target(navigationSystem), "get_sendNavigationEvents")) != previousNavigation)
        throw std::runtime_error("shortcut popup navigation restore failed");
    }
    Free(navigationSystem);
    releaseNavigationNextTick = false;
  }
  void HoldNavigation(Il2CppObject* settingsBack)
  {
    auto* cls = Class("UnityEngine.UI", "UnityEngine.EventSystems", "EventSystem");
    Root  current(Static(IL2CppClassHelper(cls).GetMethodInfo("get_current", 0), nullptr));
    if (!current.get() || !Alive(settingsBack))
      throw std::runtime_error("shortcut popup navigation owner unavailable");
    if (current.get() != Target(navigationSystem) || settingsBack != Target(backController)) {
      ReleaseNavigation();
      previousNavigation = Boolean(UiCall(current.get(), "get_sendNavigationEvents"));
      Retain(navigationSystem, current.get());
      previousBackEnabled = Boolean(UiCall(settingsBack, "get_enabled"));
      Retain(backController, settingsBack);
    }
    releaseNavigationNextTick = false;
    // Suppress Unity Submit/Cancel/Move only; pointer clicks still work. Keep
    // this held through teardown and key release so Escape cannot also go Back.
    Value(current.get(), "set_sendNavigationEvents", false);
    // Client263 BackButtonViewController.Update polls Input.GetKeyDown(Escape)
    // directly, outside EventSystem and ScreenManager.Update. Suspend only the
    // owning settings controller's poll until the dismissal key is released.
    Value(settingsBack, "set_enabled", false);
  }
  void Layout(Il2CppObject* transform, Vec2 size, Vec2 position)
  {
    Value(transform, "set_anchorMin", Vec2{.5f, .5f});
    Value(transform, "set_anchorMax", Vec2{.5f, .5f});
    Value(transform, "set_pivot", Vec2{.5f, .5f});
    Value(transform, "set_sizeDelta", size);
    Value(transform, "set_anchoredPosition", position);
  }
  Il2CppObject* NewObject(const char* name, Il2CppObject* parent)
  {
    auto* cls = UnityClass("GameObject");
    Root  object(il2cpp_object_new(cls));
    Root  label(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(name)));
    auto* ctor = IL2CppClassHelper(cls).GetMethodInfoSpecial(
        ".ctor", [](auto n, auto p) { return n == 1 && Type(p[0], IL2CPP_TYPE_STRING); });
    void* args[] = {label.get()};
    Invoke(ctor, object.get(), args);
    // The root is retained before any operation that can throw. Children are
    // parented immediately; closing the root then cleans up partial construction.
    if (!parent)
      Retain(popup.object, object.get());
    Root transform(UiCall(object.get(), "get_transform"));
    if (parent) {
      bool  world        = false;
      void* parentArgs[] = {parent, &world};
      UiCall(transform.get(), "SetParent", 2, parentArgs);
    }
    WithType(object.get(), "AddComponent", UnityClass("RectTransform"));
    Value(object.get(), "set_layer", 5); // Unity UI layer.
    return object.get();
  }
  Il2CppObject* Image(Il2CppObject* object, Color color, Il2CppObject* sprite = nullptr)
  {
    Root image(WithType(object, "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "Image")));
    Value(image.get(), "set_color", color);
    if (sprite) {
      Set(image.get(), "set_sprite", sprite);
      Value(image.get(), "set_type", 1); // Sliced.
    }
    return image.get();
  }
  Il2CppObject* Label(Il2CppObject* parent, Il2CppObject* font, const char* name, Vec2 size, Vec2 position,
                      float fontSize, const std::string& content)
  {
    Root object(NewObject(name, parent));
    Root transform(UiCall(object.get(), "get_transform"));
    Layout(transform.get(), size, position);
    Root label(WithType(object.get(), "AddComponent", Class("Unity.TextMeshPro", "TMPro", "TextMeshProUGUI")));
    Set(label.get(), "set_font", font);
    Value(label.get(), "set_fontSize", fontSize);
    Value(label.get(), "set_alignment", 0x202); // Center.
    Value(label.get(), "set_color", Color{.88f, .95f, .97f, 1});
    Value(label.get(), "set_raycastTarget", false);
    Value(label.get(), "set_enableWordWrapping", true);
    Text(label.get(), content);
    return label.get();
  }
  void Render()
  {
    if (!popup.commands.read)
      return;
    const auto value = popup.commands.read();
    if (popup.rendered == value)
      return;
    Text(Target(popup.title), value.title);
    Text(Target(popup.current), "Current: " + value.current);
    Text(Target(popup.proposed), value.proposed.empty() ? "Press a shortcut" : value.proposed);
    Text(Target(popup.status), value.status);
    Root statusTransform(UiCall(Target(popup.status), "get_transform"));
    Root preferred(UiCall(Target(popup.status), "get_preferredHeight"));
    if (!preferred.get() || !Type(il2cpp_class_get_type(preferred.get()->klass), IL2CPP_TYPE_R4))
      throw std::runtime_error("shortcut popup text height");
    Value(statusTransform.get(), "set_sizeDelta",
          Vec2{548, std::max(180.f, *static_cast<float*>(il2cpp_object_unbox(preferred.get())))});
    Value(Target(popup.scroll), "set_verticalNormalizedPosition", 1.f);
    Text(Target(popup.confirmText), value.confirmLabel);
    Value(Target(popup.buttons[0]), "set_interactable", value.canConfirm);
    Value(Target(popup.buttons[1]), "set_interactable", value.canRecord);
    popup.rendered = value;
  }
  void Cancel()
  {
    auto cancel = popup.commands.cancel;
    CloseShortcutPopup();
    if (cancel)
      cancel();
  }
  void Click(Il2CppObject* token, const MethodInfo*)
  {
    if (invoking || !OnUIThread() || !popup.commands.read)
      return;
    struct Scope {
      Scope()
      { invoking = true; }
      ~Scope()
      { invoking = false; }
    } scope;
    try {
      if (!focused() || !Alive(Target(popup.object)) || !Alive(Target(popup.owner))
          || !Boolean(UiCall(Target(popup.owner), "get_activeInHierarchy"))) {
        Cancel();
        return;
      }
      const auto presentation = popup.commands.read();
      // Copy before invocation: confirming/cancelling destroys the surface.
      std::function<void()> command;
      if (token == Target(popup.tokens[0]) && presentation.canConfirm)
        command = popup.commands.confirm;
      else if (token == Target(popup.tokens[1]) && presentation.canRecord)
        command = popup.commands.record;
      else if (token == Target(popup.tokens[2])) {
        Cancel();
        return;
      }
      if (command)
        command();
      Render();
    } catch (const std::exception& error) {
      spdlog::warn("[Shortcuts] Popup command failed: {}", error.what());
      Cancel();
    }
  }
  void Button(Il2CppObject* parent, Il2CppObject* font, Il2CppObject* sprite, int index, const char* caption,
              Vec2 position)
  {
    Root object(NewObject(caption, parent));
    Root transform(UiCall(object.get(), "get_transform"));
    Layout(transform.get(), {174, 52}, position);
    Root image(Image(object.get(), {.60f, .80f, .95f, 1}, sprite));
    Root button(WithType(object.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "Button")));
    Set(button.get(), "set_targetGraphic", image.get());
    Root text(Label(transform.get(), font, "Label", {166, 46}, {0, 0}, 21, caption));
    Retain(popup.buttons[index], button.get());
    if (index == 0)
      Retain(popup.confirmText, text.get());
    Root token(il2cpp_object_new(il2cpp_class_from_name(il2cpp_get_corlib(), "System", "Object")));
    Retain(popup.tokens[index], token.get());
    Root  event(UiCall(button.get(), "get_onClick"));
    auto* add = IL2CppClassHelper(event.get()->klass).GetMethodInfo("AddListener", 1);
    if (!Instance(add, 1, IL2CPP_TYPE_VOID) || !Reference(add->parameters[0]))
      throw std::runtime_error("shortcut popup button event");
    Root  callback(MakeDelegate(il2cpp_class_from_type(add->parameters[0]), token.get(), clickCallback.method()));
    void* args[] = {callback.get()};
    Invoke(add, event.get(), args);
  }
  void Tick()
  {
    try {
      if (!popup.commands.read) {
        if ((navigationSystem || backController) && !Key::shortcutCaptureActive) {
          if (releaseNavigationNextTick)
            ReleaseNavigation();
          else
            releaseNavigationNextTick = true; // Also consume the release frame.
        } else {
          releaseNavigationNextTick = false;
        }
        return;
      }
      if (!focused() || !Alive(Target(popup.object)) || !Alive(Target(popup.owner))
          || !Boolean(UiCall(Target(popup.owner), "get_activeInHierarchy"))) {
        Cancel();
        return;
      }
      const auto command = popup.keys.Tick(
          popup.commands.read().canConfirm, Key::RawPressed(KeyCode::Return) || Key::RawPressed(KeyCode::KeypadEnter),
          Key::RawDown(KeyCode::Return) || Key::RawDown(KeyCode::KeypadEnter), Key::RawDown(KeyCode::Escape));
      if (command == ShortcutPopupKey::Cancel) {
        Cancel();
        return;
      }
      if (command == ShortcutPopupKey::Confirm) {
        Root token(Target(popup.tokens[0]));
        Click(token.get(), nullptr); // Same availability/conflict checks as the button.
      }
      Render();
    } catch (const std::exception& error) {
      spdlog::warn("[Shortcuts] Popup closed: {}", error.what());
      Cancel();
    }
  }

} // namespace

void CloseShortcutPopup()
{
  popup.commands = {};
  popup.rendered.reset();
  popup.keys               = {};
  Key::shortcutPopupActive = false;
  try {
    if (Alive(Target(popup.object))) {
      SetActive(Target(popup.object), false);
      static auto* destroy = IL2CppClassHelper(UnityClass("Object")).GetMethodInfo("Destroy", 1);
      void*        args[]  = {Target(popup.object)};
      Static(destroy, args);
    }
  } catch (...) {
    spdlog::warn("[Shortcuts] Popup teardown unavailable");
  }
  for (auto* handle : {&popup.object, &popup.owner, &popup.status, &popup.proposed, &popup.current, &popup.title,
                       &popup.confirmText, &popup.scroll})
    Free(*handle);
  for (auto& handle : popup.buttons)
    Free(handle);
  for (auto& handle : popup.tokens)
    Free(handle);
}

bool OpenShortcutPopup(ShortcutPopupCommands commands)
{
  if (!installed || !OnUIThread() || popup.commands.read || !InvokingActionWidget())
    return false;
  try {
    CloseSettingsSearch(true);
    Root host(InvokingActionWidget());
    Root hostObject(UiCall(host.get(), "get_gameObject"));
    Root settings(WithType(hostObject.get(), "GetComponentInParent",
                           Class("Assembly-CSharp", "Digit.Prime.GameSettings", "GameSettingsViewController"), 2));
    if (!settings.get())
      throw std::runtime_error("shortcut popup settings controller unavailable");
    Root settingsBack(ReadField(settings.get(), Field(settings.get()->klass, "_backButtonViewController")));
    Root canvas(
        WithType(hostObject.get(), "GetComponentInParent", Class("UnityEngine.UIModule", "UnityEngine", "Canvas"), 2));
    if (!canvas.get())
      throw std::runtime_error("shortcut popup canvas");
    Root parent(UiCall(canvas.get(), "get_transform"));
    Root owner(UiCall(canvas.get(), "get_gameObject"));
    Retain(popup.owner, owner.get());
    Root localizer(ReadField(host.get(), Field(host.get()->klass, "_label")));
    Root labelObject(UiCall(localizer.get(), "get_gameObject"));
    Root source(WithType(labelObject.get(), "GetComponent", Class("Unity.TextMeshPro", "TMPro", "TMP_Text")));
    Root font(UiCall(source.get(), "get_font"));
    if (!font.get())
      throw std::runtime_error("shortcut popup font");
    Root background(RowImage(host.get()));
    Root sprite(background.get() ? UiCall(background.get(), "get_sprite") : nullptr);

    Root object(NewObject("CommunityMod_ShortcutPopup", nullptr));
    SetActive(object.get(), false);
    Root  transform(UiCall(object.get(), "get_transform"));
    bool  world        = false;
    void* parentArgs[] = {parent.get(), &world};
    UiCall(transform.get(), "SetParent", 2, parentArgs);
    Value(transform.get(), "set_anchorMin", Vec2{0, 0});
    Value(transform.get(), "set_anchorMax", Vec2{1, 1});
    Value(transform.get(), "set_sizeDelta", Vec2{0, 0});
    Value(transform.get(), "set_anchoredPosition", Vec2{0, 0});
    UiCall(transform.get(), "SetAsLastSibling");
    Image(object.get(), {0, 0, 0, .45f}); // Blocks pointer input to the settings behind it.
    Root panel(NewObject("Panel", transform.get()));
    Root panelTransform(UiCall(panel.get(), "get_transform"));
    Layout(panelTransform.get(), {620, 540}, {0, 0});
    // The row sprite is already dark; tinting it again obscures the popup surface.
    Image(panel.get(), {.10f, .18f, .24f, 1});
    Root rect(UiCall(parent.get(), "get_rect"));
    if (rect.get() && rect.get()->klass == UnityClass("Rect")) {
      const auto value = *static_cast<Rect*>(il2cpp_object_unbox(rect.get()));
      const auto scale = std::min({1.f, value.width / 650.f, value.height / 570.f});
      if (scale <= 0)
        throw std::runtime_error("shortcut popup canvas size");
      Value(panelTransform.get(), "set_localScale", Vector3{scale, scale, 1});
    }
    Retain(popup.title, Label(panelTransform.get(), font.get(), "Title", {560, 58}, {0, 214}, 29, ""));
    Retain(popup.current, Label(panelTransform.get(), font.get(), "Current", {560, 45}, {0, 157}, 21, ""));
    Retain(popup.proposed, Label(panelTransform.get(), font.get(), "Proposed", {560, 75}, {0, 82}, 32, ""));
    Root viewport(NewObject("Warnings", panelTransform.get()));
    Root viewportTransform(UiCall(viewport.get(), "get_transform"));
    Layout(viewportTransform.get(), {560, 180}, {0, -59});
    WithType(viewport.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "RectMask2D"));
    Image(viewport.get(), {0, 0, 0, .01f}); // Wheel/drag target even over empty text.
    Root status(Label(viewportTransform.get(), font.get(), "Status", {548, 180}, {0, 0}, 20, ""));
    Root statusTransform(UiCall(status.get(), "get_transform"));
    Value(statusTransform.get(), "set_anchorMin", Vec2{.5f, 1});
    Value(statusTransform.get(), "set_anchorMax", Vec2{.5f, 1});
    Value(statusTransform.get(), "set_pivot", Vec2{.5f, 1});
    Value(status.get(), "set_alignment", 0x102); // Top center.
    Retain(popup.status, status.get());
    Root scroll(WithType(viewport.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "ScrollRect")));
    Set(scroll.get(), "set_viewport", viewportTransform.get());
    Set(scroll.get(), "set_content", statusTransform.get());
    Value(scroll.get(), "set_horizontal", false);
    Value(scroll.get(), "set_vertical", true);
    Value(scroll.get(), "set_movementType", 2); // Clamped.
    Value(scroll.get(), "set_scrollSensitivity", 24.f);
    Retain(popup.scroll, scroll.get());
    Button(panelTransform.get(), font.get(), sprite.get(), 2, "Cancel", {-190, -220});
    Button(panelTransform.get(), font.get(), sprite.get(), 1, "Record again", {0, -220});
    Button(panelTransform.get(), font.get(), sprite.get(), 0, "Confirm", {190, -220});
    HoldNavigation(settingsBack.get());
    popup.commands           = std::move(commands);
    Key::shortcutPopupActive = true;
    Render();
    SetActive(object.get(), true);
    spdlog::info("[Shortcuts] Popup opened");
    return true;
  } catch (const std::exception& error) {
    spdlog::warn("[Shortcuts] Popup unavailable: {}", error.what());
    CloseShortcutPopup();
    return false;
  }
}

void InstallShortcutPopup()
{
  if (installed)
    return;
  auto*       object = il2cpp_class_from_name(il2cpp_get_corlib(), "System", "Object");
  const auto* ctor   = object ? il2cpp_class_get_method_from_name(object, ".ctor", 0) : nullptr;
  focused            = il2cpp_resolve_icall_typed<bool()>("UnityEngine.Application::get_isFocused()");
  installed = focused && clickCallback.Initialize(ctor, Click) && register_screen_manager_update_callback(Tick);
}
} // namespace mod_settings::native
#endif

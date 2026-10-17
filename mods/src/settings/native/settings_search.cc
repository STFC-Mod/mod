#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include "settings_search.h"
#include "action_widgets.h"
#include "page_navigation.h"
#include "patches/key.h"
#include "patches/screen_update_hook.h"
#include "settings/input_release.h"
#include "settings/settings_search.h"
#include "str_utils.h"
#include "ui_helpers.h"
#include <spdlog/spdlog.h>

namespace mod_settings::native
{
namespace
{
  using namespace ui;
  constexpr auto rootId    = "community_mod.settings";
  constexpr auto resultsId = "community_mod.search.results";
  struct Vec2 {
    float x, y;
  };
  struct Color {
    float r, g, b, a;
  };
  SettingsSearch                          index;
  std::vector<const SettingsSearchEntry*> matches;
  std::string                             query;
  std::optional<SettingsSearchEntry>      pending;
  Il2CppGCHandle                          object{}, input{}, owner{}, panel{}, back{};
  Vec2                                    panelOffset{};
  bool                                    installed = false, movedPanel = false, backHeld = false, backEnabled = false;
  InputRelease                            release;
  bool (*focused)()                  = nullptr;
  Il2CppGCHandle drainingBack        = nullptr;
  bool           drainingBackEnabled = false;

  template <class T> T ReadValue(Il2CppObject* component, const char* name, const char* type)
  {
    Root boxed(UiCall(component, name));
    if (!boxed.get() || boxed.get()->klass != UnityClass(type))
      throw std::runtime_error("settings search layout value");
    return *static_cast<T*>(il2cpp_object_unbox(boxed.get()));
  }
  void ReleaseBack()
  {
    if (backHeld && Alive(Target(back)))
      Value(Target(back), "set_enabled", backEnabled);
    backHeld                  = false;
    if (Alive(Target(drainingBack)))
      Value(Target(drainingBack), "set_enabled", drainingBackEnabled);
    Free(drainingBack);
  }
  void SetQuery(std::string text)
  {
    query   = std::move(text);
    matches = index.Find(query);
  }
  Il2CppObject* NewObject(const char* name, Il2CppObject* parent)
  {
    Root        result(il2cpp_object_new(UnityClass("GameObject")));
    Root        label(reinterpret_cast<Il2CppObject*>(il2cpp_string_new(name)));
    const auto* ctor   = IL2CppClassHelper(UnityClass("GameObject")).GetMethodInfoSpecial(".ctor", [](auto n, auto p) {
      return n == 1 && Type(p[0], IL2CPP_TYPE_STRING);
    });
    void*       args[] = {label.get()};
    Invoke(ctor, result.get(), args);
    if (!parent)
      Retain(object, result.get());
    Root transform(UiCall(result.get(), "get_transform"));
    if (parent) {
      bool  world = false;
      void* parentArgs[]{parent, &world};
      UiCall(transform.get(), "SetParent", 2, parentArgs);
    }
    WithType(result.get(), "AddComponent", UnityClass("RectTransform"));
    Value(result.get(), "set_layer", 5);
    return result.get();
  }
  void Stretch(Il2CppObject* transform, Vec2 min, Vec2 max)
  {
    Value(transform, "set_anchorMin", Vec2{0, 0});
    Value(transform, "set_anchorMax", Vec2{1, 1});
    Value(transform, "set_offsetMin", min);
    Value(transform, "set_offsetMax", max);
  }
  Il2CppObject* Label(Il2CppObject* parent, Il2CppObject* font, const char* name, const char* text, Color color)
  {
    Root child(NewObject(name, parent));
    Root transform(UiCall(child.get(), "get_transform"));
    Stretch(transform.get(), {0, 0}, {0, 0});
    Root label(WithType(child.get(), "AddComponent", Class("Unity.TextMeshPro", "TMPro", "TextMeshProUGUI")));
    Set(label.get(), "set_font", font);
    Value(label.get(), "set_fontSize", 26.f);
    Value(label.get(), "set_alignment", 0x201); // Middle left.
    Value(label.get(), "set_color", color);
    Value(label.get(), "set_raycastTarget", false);
    Value(label.get(), "set_richText", false);
    Value(label.get(), "set_enableWordWrapping", false);
    Text(label.get(), text);
    return label.get();
  }
  void Tick()
  {
    if (!object && !release.active())
      return;
    try {
      if (!object) {
        if (release.Tick(focused && focused(), Key::AnyRawPressed())) {
          ReleaseBack();
          Key::settingsSearchActive = false;
        }
        return;
      }
      if (!Alive(Target(object)) || !Alive(Target(owner))
          || !Boolean(UiCall(Target(object), "get_activeInHierarchy"))) {
        CloseSettingsSearch();
        return;
      }
      if (!focused || !focused()) {
        // Alt-Tab suspends input ownership; it does not end the page visit.
        // Retain the field/query so returning focus can edit or clear them.
        release.Tick(false, false);
        return;
      }
      const bool editing = Boolean(UiCall(Target(input), "get_isFocused"));
      if (editing) {
        if (!backHeld) {
          backEnabled = Boolean(UiCall(Target(back), "get_enabled"));
          backHeld    = true;
          Value(Target(back), "set_enabled", false);
        }
        Key::settingsSearchActive = true;
        release.Hold();
      } else if (release.Tick(true, Key::AnyRawPressed())) {
        ReleaseBack();
        Key::settingsSearchActive = false;
      }
      Root text(UiCall(Target(input), "get_text"));
      if (!text.get() || !Type(il2cpp_class_get_type(text.get()->klass), IL2CPP_TYPE_STRING))
        throw std::runtime_error("settings search text");
      auto next = to_string(reinterpret_cast<Il2CppString*>(text.get()));
      if (next != query) {
        SetQuery(std::move(next));
        RefreshActions();
      }
      // A result click must finish before native navigation releases that row.
      if (pending) {
        const auto target = std::exchange(pending, {});
        NavigateToSearchResult(target->page, target->item);
      }
    } catch (const std::exception& error) {
      spdlog::warn("[ModSettings] Search closed: {}", error.what());
      CloseSettingsSearch();
      SetQuery({});
      RefreshActions();
    }
  }
} // namespace

void RegisterSettingsSearch(PageCatalog& catalog)
{
  static ActionSetting results{
      resultsId, "Search results",
      [](std::size_t row) {
        if (row == 0) {
          const auto label = matches.empty() ? std::string("No matching settings")
                                             : std::to_string(matches.size()) + " results"
                                                   + (matches.size() > SettingsSearch::ResultLimit
                                                          ? " (showing first 32; keep typing to narrow)"
                                                          : "");
          return ActionSetting::Presentation{label, "Clear", "", true};
        }
        const auto& hit    = *matches.at(row - 1);
        const auto  page   = std::ranges::find(Pages(), hit.page, &PageCatalog::Page::id);
        const bool  hidden = page != Pages().end() && !hit.item.empty() && !page->IsVisible(hit.item);
        return ActionSetting::Presentation{hit.label + "\n<size=65%>" + hit.location
                                               + (hidden ? " (hidden by another setting)" : "") + "</size>",
                                           "Open", hit.key, true};
      },
      [](std::size_t row) {
        if (row == 0) {
          // Tick owns refresh; do not release a clicked pooled row in its callback.
          if (Alive(Target(input)))
            Text(Target(input), "");
        } else if (row <= matches.size()) {
          pending = *matches[row - 1];
        }
      },
      [] { return query.empty() ? 0u : 1 + std::min(matches.size(), SettingsSearch::ResultLimit); }};
  catalog.AddAction(rootId, results);
}
bool FilterSettingsSearch(const PageCatalog::Page& page, std::string_view id)
{ return page.id != rootId || query.empty() || id == resultsId || id == "community_mod.save_notice"; }
void CloseSettingsSearch(bool transfer_navigation)
{
  pending.reset();
  try {
    if (transfer_navigation || !release.active())
      ReleaseBack();
    else if (backHeld && Target(back)) {
      Free(drainingBack);
      drainingBack        = il2cpp_gchandle_new_weakref(Target(back), false);
      drainingBackEnabled = backEnabled;
      if (!drainingBack)
        ReleaseBack();
      else
        backHeld = false;
    }
    if (movedPanel && Alive(Target(panel)))
      Value(Target(panel), "set_offsetMax", panelOffset);
    if (Alive(Target(object))) {
      SetActive(Target(object), false);
      void* args[]{Target(object)};
      Static(IL2CppClassHelper(UnityClass("Object")).GetMethodInfo("Destroy", 1), args);
    }
  } catch (...) {
    Warn("search cleanup unavailable");
  }
  movedPanel                = false;
  backHeld                  = false;
  // Destruction/navigation only releases the UI. Input stays owned through
  // focus recovery and release of every supported key/button.
  Key::settingsSearchActive = release.active();
  for (auto* handle : {&object, &input, &owner, &panel, &back})
    Free(*handle);
}
void OpenSettingsSearch(Il2CppObject* controller)
{
  if (!installed || !ActionsActive())
    return;
  try {
    ReleaseBack();
    Retain(owner, controller);
    Retain(back, ReadField(controller, Field(controller->klass, "_backButtonViewController")));
    Root nativePanel(ReadField(controller, PageMeta().panel));
    Root rect(UiCall(nativePanel.get(), "get_transform"));
    Retain(panel, rect.get());
    Root       parent(UiCall(rect.get(), "get_parent"));
    const auto min       = ReadValue<Vec2>(rect.get(), "get_anchorMin", "Vector2");
    const auto max       = ReadValue<Vec2>(rect.get(), "get_anchorMax", "Vector2");
    const auto offsetMin = ReadValue<Vec2>(rect.get(), "get_offsetMin", "Vector2");
    panelOffset          = ReadValue<Vec2>(rect.get(), "get_offsetMax", "Vector2");
    Root title(ReadField(controller, PageMeta().title));
    Root titleObject(UiCall(title.get(), "get_gameObject"));
    Root source(WithType(titleObject.get(), "GetComponent", Class("Unity.TextMeshPro", "TMPro", "TMP_Text")));
    Root font(UiCall(source.get(), "get_font"));
    if (!font.get())
      throw std::runtime_error("settings search font");
    Root root(NewObject("CommunityMod_SettingsSearch", nullptr));
    SetActive(root.get(), false);
    Root  transform(UiCall(root.get(), "get_transform"));
    bool  world = false;
    void* parentArgs[]{parent.get(), &world};
    UiCall(transform.get(), "SetParent", 2, parentArgs);
    Value(transform.get(), "set_anchorMin", Vec2{min.x, max.y});
    Value(transform.get(), "set_anchorMax", Vec2{max.x, max.y});
    Value(transform.get(), "set_offsetMin", Vec2{offsetMin.x, panelOffset.y - 76});
    Value(transform.get(), "set_offsetMax", Vec2{panelOffset.x, panelOffset.y - 4});
    Root image(WithType(root.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "Image")));
    Value(image.get(), "set_color", Color{.10f, .20f, .25f, 1});
    Root viewport(NewObject("Text Area", transform.get()));
    Root viewportRect(UiCall(viewport.get(), "get_transform"));
    Stretch(viewportRect.get(), {20, 6}, {-20, -6});
    WithType(viewport.get(), "AddComponent", Class("UnityEngine.UI", "UnityEngine.UI", "RectMask2D"));
    Root text(Label(viewportRect.get(), font.get(), "Text", "", {.88f, .95f, .97f, 1}));
    Root placeholder(Label(viewportRect.get(), font.get(), "Placeholder", "Search settings, shortcuts or TOML name...",
                           {.60f, .72f, .76f, 1}));
    Root field(WithType(root.get(), "AddComponent", Class("Unity.TextMeshPro", "TMPro", "TMP_InputField")));
    Retain(input, field.get());
    Set(field.get(), "set_targetGraphic", image.get());
    Set(field.get(), "set_textViewport", viewportRect.get());
    Set(field.get(), "set_textComponent", text.get());
    Set(field.get(), "set_placeholder", placeholder.get());
    Value(field.get(), "set_characterLimit", SettingsSearch::QueryLimit);
    Value(field.get(), "set_lineType", 0);
    Value(field.get(), "set_richText", false);
    Value(field.get(), "set_restoreOriginalTextOnEscape", false);
    Text(field.get(), query);
    movedPanel = true;
    Value(rect.get(), "set_offsetMax", Vec2{panelOffset.x, panelOffset.y - 90});
    SetActive(root.get(), true);
    spdlog::info("[ModSettings] Search field opened");
  } catch (const std::exception& error) {
    spdlog::warn("[ModSettings] Search unavailable: {}", error.what());
    CloseSettingsSearch();
    SetQuery({});
    RefreshActions();
  }
}
void InstallSettingsSearch()
{
  index.Build(Pages());
  focused   = il2cpp_resolve_icall_typed<bool()>("UnityEngine.Application::get_isFocused()");
  installed = focused && register_screen_manager_update_callback(Tick);
}
} // namespace mod_settings::native
#endif

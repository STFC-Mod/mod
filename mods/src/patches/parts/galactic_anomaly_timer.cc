#include "config.h"
#include "patches/patches.h"

#include <prime/ui.h>
#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace
{
// All Unity work runs on the ScreenManager.Update tick. No detours of its own.
bool installed = false;

using UnityObject = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;

struct TimeSpan {
  int64_t ticks;
};

struct TextMeshProUGUI : Component {
  using Class = il2cpp::Class<"Unity.TextMeshPro", "TMPro", "TextMeshProUGUI">;

  // TMP_FontAsset
  il2cpp::Object* font()
  { return Get<"font", il2cpp::Object*>(); }
  void SetFont(il2cpp::Object* value)
  { Set<"font">(value); }
  void SetFontSize(float value)
  { Set<"fontSize">(value); }
  // TextAlignmentOptions
  void SetAlignment(int32_t value)
  { Set<"alignment">(value); }
  void SetColor(color value)
  { Set<"color">(value); }
  void SetRaycastTarget(bool value)
  { Set<"raycastTarget">(value); }
  void SetEnableWordWrapping(bool value)
  { Set<"enableWordWrapping">(value); }
  void SetText(Il2CppString* value)
  { Set<"text">(value); }
};

struct FactionInformation : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "FactionInformation">;

  // TextLocalizer components, one per system name label.
  il2cpp::List<Component*>* systemName()
  { return Field<"_systemName", il2cpp::List<Component*>*>(); }
};

struct HudNavigationViewController : Component {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation.UI", "HudNavigationViewController">;

  Component* galacticAnomaliesButtonWidget()
  { return Field<"_galacticAnomaliesButtonWidget", Component*>(); }
  FactionInformation* factionInformationHelper()
  { return Field<"_factionInformationHelper", FactionInformation*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  bool CanShowGalacticAnomaly()
  { return Call<"CanShowGalacticAnomaly", bool>(); }
  // NavigationViewData. It and the server models below are read on their runtime classes.
  il2cpp::Object* CanvasContext()
  { return Get<"CanvasContext", il2cpp::Object*>(); }
};

struct HudAllianceAndNewsViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "HudAllianceAndNewsViewController">;

  Component* chestPromotion()
  { return Field<"_chestPromotion", Component*>(); }
};

// The data contexts the two HUD canvases are registered with.
struct NavigationViewData : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationViewData">;
};
struct HudAllianceAndNewsViewControllerContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "HudAllianceAndNewsViewControllerContext">;
};

struct GalacticAnomaliesManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GalacticAnomalies", "GalacticAnomaliesManager">;

  // Digit.PrimeServer.Models.GalacticAnomaly, or null.
  il2cpp::Object* GetSystemGalacticAnomalies(int64_t system)
  { return Call<"GetSystemGalacticAnomalies", il2cpp::Object*>(system); }
};

bool Alive(Il2CppObject* object)
{ return object && UnityObject::Call<"op_Implicit", bool>(object); }

void Destroy(Il2CppObject* object)
{ UnityObject::Call<"Destroy">(object); }

template <typename T> T* Find()
{
  auto* type = T::Class::TypeObject();
  return type ? static_cast<T*>(UnityObject::Call<"FindObjectOfType", Il2CppObject*>(type)) : nullptr;
}

// The first active T on `root`'s GameObject or below it. Scoped to one hierarchy, unlike FindObjectOfType, which
// scans every loaded object.
template <typename T> T* FindInChildren(il2cpp::Object* root)
{
  auto* type = T::Class::TypeObject();
  return root && type ? root->Call<"GetComponentInChildren", T*>(type, false) : nullptr;
}

// The first live T on a visible registered canvas whose context is a `Context`. Only the canvas that owns that
// context is searched: the top interaction canvas and its root can belong to unrelated overlays.
template <typename T, typename Context> T* RegisteredController()
{
  auto*   screenManager = ScreenManager::Existing();
  auto*   screens       = Alive(screenManager) ? screenManager->screens() : nullptr;
  int32_t count         = screens ? screens->Count() : 0;
  if (count < 0 || count > 256)
    return nullptr;
  for (int32_t index = 0; index < count; ++index) {
    auto* canvas  = (*screens)[index];
    auto* context = Alive(canvas) ? canvas->Context() : nullptr;
    if (!context || !context->Is<Context>() || !canvas->Visible())
      continue;
    if (auto* controller = FindInChildren<T>(canvas); Alive(controller))
      return controller;
  }
  return nullptr;
}

HudNavigationViewController* NavigationHud()
{ return RegisteredController<HudNavigationViewController, NavigationViewData>(); }

il2cpp::GcHandle navigation, panel, label, anomalyManager;
int64_t          renderedSeconds = -1;

void Clear()
{
  if (auto* go = panel.Get<GameObject>(); Alive(go)) {
    go->SetActive(false);
    Destroy(go);
  }
  label.Reset();
  panel.Reset();
  navigation.Reset();
  anomalyManager.Reset();
  renderedSeconds = -1;
}

void Rect(RectTransform* transform, Vector2 anchor, Vector2 pivot, Vector2 size, Vector2 position)
{
  transform->SetAnchorMin(anchor);
  transform->SetAnchorMax(anchor);
  transform->SetPivot(pivot);
  transform->SetSizeDelta(size);
  transform->SetAnchoredPosition(position);
}

// Creates an inactive GameObject under `parent` and roots it in `root`.
RectTransform* NewObject(const char* name, Transform* parent, il2cpp::GcHandle& root)
{
  auto* object = il2cpp::New<GameObject>(il2cpp_string_new(name));
  root         = il2cpp::GcHandle::Strong(object);
  if (!object)
    return nullptr;
  object->SetActive(false);
  auto* transform = object->AddComponent<RectTransform>();
  if (!transform)
    return nullptr;
  transform->SetParent(parent, false);
  return transform;
}

Sprite* GraphicSprite(Image* image)
{
  if (!image)
    return nullptr;
  // ImageSelector applies localized artwork through overrideSprite.
  auto* sprite = image->overrideSprite();
  return sprite ? sprite : image->sprite();
}

Sprite* NativeBackgroundSprite(HudNavigationViewController* nav)
{
  auto* button     = nav->galacticAnomaliesButtonWidget();
  auto* transform  = button ? button->transform() : nullptr;
  auto* parent     = transform ? transform->parent() : nullptr;
  auto* background = parent ? parent->Find(il2cpp_string_new("ShortcutKeybindHint/Background")) : nullptr;
  auto* object     = background ? background->gameObject() : nullptr;
  return object ? GraphicSprite(object->GetComponent<Image>()) : nullptr;
}

void Slice(Image* image, Sprite* sprite)
{
  if (!image || !sprite)
    return;
  float spritePixels = sprite->pixelsPerUnit();
  if (spritePixels <= 0)
    return;
  image->SetType(Image::Type::Sliced);
  image->SetPixelsPerUnitMultiplier(100 / spritePixels);
}

Image* Decoration(const char* name, Transform* parent, Vector2 size, Vector2 position, color tint,
                  Sprite* sprite = nullptr)
{
  il2cpp::GcHandle object;
  auto*            transform = NewObject(name, parent, object);
  auto*            go        = object.Get<GameObject>();
  auto*            image     = go ? go->AddComponent<Image>() : nullptr;
  const bool       ok        = transform && image;
  if (ok) {
    Rect(transform, {0.5f, 0.5f}, {0.5f, 0.5f}, size, position);
    image->SetColor(tint);
    image->SetRaycastTarget(false);
    if (sprite)
      image->SetSprite(sprite);
    go->SetActive(true);
  } else if (Alive(go)) {
    Destroy(go);
  }
  return ok ? image : nullptr;
}

bool Create(HudNavigationViewController* nav)
{
  // Keep the native system-name font, but place the timer under the upper-right
  // Help All / Engage / Claim drawer. Parenting to the drawer follows its scale
  // and visibility without changing any native button positions.
  auto* helper = nav->factionInformationHelper();
  auto* names  = helper ? helper->systemName() : nullptr;
  if (!names || names->Count() < 1)
    return false;
  auto* name = (*names)[0];
  if (!Alive(name))
    return false;
  auto* drawer     = RegisteredController<HudAllianceAndNewsViewController, HudAllianceAndNewsViewControllerContext>();
  auto* promotion  = drawer ? drawer->chestPromotion() : nullptr;
  auto* chest      = promotion ? promotion->transform() : nullptr;
  auto* parent     = chest ? chest->parent() : nullptr;
  auto* nameObject = name->gameObject();
  auto* source     = nameObject ? nameObject->GetComponent<TextMeshProUGUI>() : nullptr;
  auto* font       = source ? source->font() : nullptr;
  if (!parent || !font)
    return false;

  auto* transform = NewObject("CommunityMod_AnomalyTimer", parent, panel);
  auto* panelGo   = panel.Get<GameObject>();
  if (!transform || !panelGo)
    return false;
  Rect(transform, {0.5f, 0}, {0.5f, 1}, {198, 38}, {0, -40});
  auto* background = panelGo->AddComponent<Image>();
  if (!background)
    return false;
  background->SetColor({0.64f, 0.34f, 0.46f, 0.85f});
  background->SetRaycastTarget(false);
  // Use the existing native key-hint silhouette for both layers, with a narrow
  // inset creating the border. A missing sprite still yields a visible frame.
  auto* rounded = NativeBackgroundSprite(nav);
  if (rounded) {
    background->SetSprite(rounded);
    Slice(background, rounded);
  }
  auto* fill = Decoration("AnomalyFill", transform, {194, 34}, {0, 0}, {0.06f, 0.10f, 0.14f, 0.96f}, rounded);
  if (!fill)
    return false;
  Slice(fill, rounded);

  il2cpp::GcHandle textObject;
  auto*            textTransform = NewObject("Countdown", transform, textObject);
  auto*            textGo        = textObject.Get<GameObject>();
  auto*            text          = textGo ? textGo->AddComponent<TextMeshProUGUI>() : nullptr;
  label                          = il2cpp::GcHandle::Strong(text);
  const bool ok                  = textTransform && text;
  if (ok) {
    Rect(textTransform, {0.5f, 0.5f}, {0.5f, 0.5f}, {190, 28}, {0, 0});
    text->SetFont(font);
    text->SetFontSize(22);
    text->SetAlignment(0x202); // TMP: Center (514).
    text->SetColor({0.95f, 0.83f, 0.89f, 1});
    text->SetRaycastTarget(false);
    text->SetEnableWordWrapping(false);
    textGo->SetActive(true);
  } else if (Alive(textGo)) {
    Destroy(textGo);
  }
  textObject.Reset(); // The parent hierarchy now owns this child.
  return ok;
}

// Remaining ticks of the active anomaly in the viewed system, or 0.
int64_t RemainingTicks(HudNavigationViewController* nav)
{
  auto*   context = nav->CanvasContext();
  auto*   address = context ? context->Get<"ViewingAddress", il2cpp::Object*>() : nullptr;
  int64_t system  = address ? address->Get<"System", int64_t>() : -1;
  if (system <= 0)
    return 0;
  // Cache the Unity object, not its timer data. Unity's liveness check detects
  // destruction even while the managed wrapper is retained by our GC handle.
  if (!Alive(anomalyManager.Get()))
    anomalyManager = il2cpp::GcHandle::Strong(Find<GalacticAnomaliesManager>());
  auto* manager = anomalyManager.Get<GalacticAnomaliesManager>();
  auto* anomaly = manager ? manager->GetSystemGalacticAnomalies(system) : nullptr;
  if (!anomaly || !anomaly->Get<"IsActive", bool>())
    return 0;
  auto* timer = anomaly->Get<"EndTimeTimerDataContext", il2cpp::Object*>();
  return timer ? timer->Get<"RemainingTime", TimeSpan>().ticks : 0;
}

void Update()
{
  static auto next = std::chrono::steady_clock::time_point{};
  auto        now  = std::chrono::steady_clock::now();
  if (now < next)
    return;
  next = now + std::chrono::milliseconds(250);

  if (!Config::Get().graphics.galactic_anomaly_timer) {
    Clear();
    return;
  }
  // Rediscovery walks every registered canvas, so retry at most once a second. A hidden HUD is kept until a
  // different live one turns up.
  static auto nextDiscovery = std::chrono::steady_clock::time_point{};
  auto*       cached        = navigation.Get<HudNavigationViewController>();
  if ((!Alive(cached) || !cached->isActiveAndEnabled()) && now >= nextDiscovery) {
    nextDiscovery     = now + std::chrono::seconds(1);
    auto* replacement = NavigationHud();
    if ((replacement && replacement != cached) || !Alive(cached)) {
      Clear();
      navigation = il2cpp::GcHandle::Strong(replacement);
    }
  }
  auto*   nav   = navigation.Get<HudNavigationViewController>();
  int64_t ticks = 0;
  if (Alive(nav) && nav->isActiveAndEnabled() && nav->CanShowGalacticAnomaly())
    ticks = RemainingTicks(nav);
  bool show = ticks > 0;
  if (show && (!Alive(panel.Get()) || !Alive(label.Get()))) {
    // Release old UI without releasing the current navigation reference.
    auto* current = nav;
    Clear();
    navigation = il2cpp::GcHandle::Strong(current);
    if (!Create(current)) {
      static bool warned = false;
      if (!warned) {
        spdlog::warn("Galactic anomaly countdown: HUD creation failed; leaving the native HUD unchanged");
        warned = true;
      }
      Clear();
      next = now + std::chrono::seconds(5);
      return;
    }
  }
  auto* panelGo = panel.Get<GameObject>();
  if (!Alive(panelGo))
    return;
  if (show) {
    // Only touch the label when the displayed whole second changes.
    auto seconds = ticks / 10000000 + (ticks % 10000000 != 0);
    if (seconds != renderedSeconds) {
      auto text = seconds >= 3600 ? fmt::format("{}h {:02}m {:02}s", seconds / 3600, seconds / 60 % 60, seconds % 60)
                                  : fmt::format("{:02}m {:02}s", seconds / 60, seconds % 60);
      label.Get<TextMeshProUGUI>()->SetText(il2cpp_string_new(text.c_str()));
      renderedSeconds = seconds;
    }
  }
  panelGo->SetActive(show);
}
} // namespace

void GalacticAnomalyTimerUpdate()
{
  if (installed)
    Update();
}

void InstallGalacticAnomalyTimer()
{
  installed = InstallScreenManagerUpdateHook();
  if (installed)
    spdlog::info("Galactic anomaly countdown enabled (below Help All / Engage / Claim)");
}

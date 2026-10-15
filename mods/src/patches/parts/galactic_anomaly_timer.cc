// galactic_anomaly_timer: a countdown for the active galactic anomaly in the viewed system, under the HUD's
// Help All / Engage / Claim drawer. All Unity work runs on the frame tick; there are no detours of its own.

#include "config.h"
#include "patches/patches.h"

#include <prime/Hub.h>
#include <prime/ui.h>
#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <utility>

namespace
{
struct TimeSpan {
  int64_t ticks;
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

// The first active T on `root`'s GameObject or below it. Scoped to one hierarchy, unlike FindObjectOfType, which
// scans every object.
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
  auto*   screens       = UnityObject::Alive(screenManager) ? screenManager->screens() : nullptr;
  int32_t count         = screens ? screens->Count() : 0;
  if (count < 0 || count > 256)
    return nullptr;
  for (int32_t index = 0; index < count; ++index) {
    auto* canvas  = (*screens)[index];
    auto* context = UnityObject::Alive(canvas) ? canvas->Context() : nullptr;
    if (!context || !context->Is<Context>() || !canvas->Visible())
      continue;
    if (auto* controller = FindInChildren<T>(canvas); UnityObject::Alive(controller))
      return controller;
  }
  return nullptr;
}

struct GalacticAnomaliesManager : MonoSingleton<GalacticAnomaliesManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GalacticAnomalies", "GalacticAnomaliesManager">;

  // Instance is only read once the singleton is up, so a missing manager never triggers the game's own search.
  static GalacticAnomaliesManager* Current()
  { return Class::Get<"Initialized", bool>() ? Instance() : nullptr; }

  // Digit.PrimeServer.Models.GalacticAnomaly, or null.
  il2cpp::Object* GetSystemGalacticAnomalies(int64_t system)
  { return Call<"GetSystemGalacticAnomalies", il2cpp::Object*>(system); }
};

using Clock = std::chrono::steady_clock;

// Creates an inactive GameObject with a RectTransform under `parent`. `root` keeps it alive until the caller is
// done building it.
RectTransform* NewObject(const char* name, Transform* parent, il2cpp::GcHandle& root)
{
  auto* object = il2cpp::New<GameObject>(il2cpp_string_new(name));
  root         = il2cpp::GcHandle::Strong(object);
  if (!object)
    return nullptr;
  object->SetActive(false);
  auto* transform = object->AddComponent<RectTransform>();
  if (transform)
    transform->SetParent(parent, false);
  return transform;
}

// The rounded key-hint background next to the native anomaly button, reused for the panel.
Sprite* NativeBackgroundSprite(HudNavigationViewController* nav)
{
  auto* button     = nav->galacticAnomaliesButtonWidget();
  auto* transform  = button ? button->transform() : nullptr;
  auto* parent     = transform ? transform->parent() : nullptr;
  auto* background = parent ? parent->Find(il2cpp_string_new("ShortcutKeybindHint/Background")) : nullptr;
  auto* object     = background ? background->gameObject() : nullptr;
  auto* image      = object ? object->GetComponent<Image>() : nullptr;
  if (!image)
    return nullptr;
  // ImageSelector applies localized artwork through overrideSprite.
  auto* sprite = image->overrideSprite();
  return sprite ? sprite : image->sprite();
}

Image* AddImage(GameObject* object, color tint, Sprite* sprite)
{
  auto* image = object->AddComponent<Image>();
  if (!image)
    return nullptr;
  image->SetColor(tint);
  image->SetRaycastTarget(false);
  if (sprite && sprite->pixelsPerUnit() > 0) {
    image->SetSprite(sprite);
    image->SetType(Image::Type::Sliced);
    image->SetPixelsPerUnitMultiplier(100 / sprite->pixelsPerUnit());
  }
  return image;
}

// The countdown panel under the upper-right Help All / Engage / Claim drawer. Parenting to the drawer follows its
// scale and visibility without moving any native button.
class AnomalyTimer
{
public:
  void Update()
  {
    const auto now = Clock::now();
    if (now < next_update_)
      return;
    next_update_ = now + std::chrono::milliseconds(250);

    auto* nav = Navigation();
    // Remaining ticks of the active anomaly in the viewed system.
    const int64_t ticks = nav && nav->isActiveAndEnabled() && nav->CanShowGalacticAnomaly() ? RemainingTicks(nav) : 0;
    if (ticks <= 0) {
      if (auto* label = Label())
        label->transform()->parent()->gameObject()->SetActive(false);
      return;
    }
    if (!Label() && !Create(nav)) {
      static bool warned = false;
      if (!std::exchange(warned, true))
        spdlog::warn("Galactic anomaly countdown: HUD creation failed; leaving the native HUD unchanged");
      DestroyPanel();
      next_update_ = now + std::chrono::seconds(5);
      return;
    }

    const auto seconds = ticks / 10'000'000 + (ticks % 10'000'000 != 0);
    if (seconds != shown_seconds_) {
      shown_seconds_  = seconds;
      const auto text = seconds >= 3600
                            ? fmt::format("{}h {:02}m {:02}s", seconds / 3600, seconds / 60 % 60, seconds % 60)
                            : fmt::format("{:02}m {:02}s", seconds / 60, seconds % 60);
      Label()->SetText(il2cpp_string_new(text.c_str()));
    }
    Label()->transform()->parent()->gameObject()->SetActive(true);
  }

private:
  // The navigation HUD while the system view is open. It has no singleton; look for it on its registered canvas
  // whenever the one held is gone or inactive, rather than with FindObjectOfType, which scans every object. The
  // search walks every registered canvas, so it runs at most once a second, and a held HUD is kept until a
  // different live one turns up.
  HudNavigationViewController* Navigation()
  {
    if (Hub::CurrentSection() != SectionID::Navigation_System) {
      DestroyPanel();
      navigation_.Reset();
      return nullptr;
    }
    auto* held = navigation_.Get<HudNavigationViewController>();
    if (UnityObject::Alive(held) && held->isActiveAndEnabled())
      return held;
    if (const auto now = Clock::now(); now >= next_discovery_) {
      next_discovery_   = now + std::chrono::seconds(1);
      auto* replacement = RegisteredController<HudNavigationViewController, NavigationViewData>();
      if ((replacement && replacement != held) || !UnityObject::Alive(held)) {
        DestroyPanel();
        navigation_ = il2cpp::GcHandle::Strong(replacement);
      }
    }
    auto* nav = navigation_.Get<HudNavigationViewController>();
    return UnityObject::Alive(nav) ? nav : nullptr;
  }

  TMP_Text* Label()
  {
    auto* label = label_.Get<TMP_Text>();
    return UnityObject::Alive(label) ? label : nullptr;
  }

  void DestroyPanel()
  {
    if (auto* label = Label()) {
      auto* panel = label->transform()->parent()->gameObject();
      panel->SetActive(false);
      UnityObject::Destroy(panel);
    }
    label_.Reset();
    shown_seconds_ = -1;
  }

  static int64_t RemainingTicks(HudNavigationViewController* nav)
  {
    auto*   context = nav->CanvasContext();
    auto*   address = context ? context->Get<"ViewingAddress", il2cpp::Object*>() : nullptr;
    int64_t system  = address ? address->Get<"System", int64_t>() : -1;
    auto*   manager = system > 0 ? GalacticAnomaliesManager::Current() : nullptr;
    auto*   anomaly = manager ? manager->GetSystemGalacticAnomalies(system) : nullptr;
    if (!anomaly || !anomaly->Get<"IsActive", bool>())
      return 0;
    auto* timer = anomaly->Get<"EndTimeTimerDataContext", il2cpp::Object*>();
    return timer ? timer->Get<"RemainingTime", TimeSpan>().ticks : 0;
  }

  // Builds the panel: the native rounded sprite twice, with a narrow inset making the border, and a countdown in
  // the system-name font. A missing sprite still gives a visible frame.
  bool Create(HudNavigationViewController* nav)
  {
    auto* helper = nav->factionInformationHelper();
    auto* names  = helper ? helper->systemName() : nullptr;
    auto* name   = names && names->Count() > 0 ? (*names)[0] : nullptr;
    if (!UnityObject::Alive(name))
      return false;
    auto* drawer    = RegisteredController<HudAllianceAndNewsViewController, HudAllianceAndNewsViewControllerContext>();
    auto* promotion = drawer ? drawer->chestPromotion() : nullptr;
    auto* chest     = promotion ? promotion->transform() : nullptr;
    auto* parent    = chest ? chest->parent() : nullptr;
    auto* source    = name->gameObject() ? name->gameObject()->GetComponent<TextMeshProUGUI>() : nullptr;
    auto* font      = source ? source->font() : nullptr;
    if (!parent || !font)
      return false;

    il2cpp::GcHandle panel, fill, text;
    auto*            panel_rect = NewObject("CommunityMod_AnomalyTimer", parent, panel);
    if (!panel_rect)
      return false;
    panel_rect->Place({0.5f, 0}, {0.5f, 0}, {0.5f, 1}, {198, 38}, {0, -40});
    auto* rounded = NativeBackgroundSprite(nav);
    if (!AddImage(panel.Get<GameObject>(), {0.64f, 0.34f, 0.46f, 0.85f}, rounded))
      return Abandon(panel);

    auto* fill_rect = NewObject("AnomalyFill", panel_rect, fill);
    if (!fill_rect || !AddImage(fill.Get<GameObject>(), {0.06f, 0.10f, 0.14f, 0.96f}, rounded))
      return Abandon(panel);
    fill_rect->Place({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {194, 34}, {0, 0});
    fill.Get<GameObject>()->SetActive(true);

    auto* text_rect = NewObject("Countdown", panel_rect, text);
    auto* label     = text_rect ? text.Get<GameObject>()->AddComponent<TextMeshProUGUI>() : nullptr;
    if (!label)
      return Abandon(panel);
    text_rect->Place({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {190, 28}, {0, 0});
    label->SetFont(font);
    label->SetFontSize(22);
    label->SetAlignment(0x202); // TMP: Center (514).
    label->SetColor({0.95f, 0.83f, 0.89f, 1});
    label->SetRaycastTarget(false);
    label->SetEnableWordWrapping(false);
    text.Get<GameObject>()->SetActive(true);
    // The children are owned by the hierarchy once parented; only the label is kept.
    label_ = il2cpp::GcHandle::Strong(label);
    return true;
  }

  static bool Abandon(il2cpp::GcHandle& panel)
  {
    if (auto* object = panel.Get<GameObject>(); UnityObject::Alive(object))
      UnityObject::Destroy(object);
    return false;
  }

  il2cpp::GcHandle  navigation_, label_;
  Clock::time_point next_update_{}, next_discovery_{};
  int64_t           shown_seconds_ = -1;
};

AnomalyTimer* timer = nullptr;
} // namespace

void GalacticAnomalyTimerUpdate()
{
  if (timer)
    timer->Update();
}

void InstallGalacticAnomalyTimer()
{
  // Never destroyed: GC handles must not be released by static destructors after the runtime is gone.
  if (InstallScreenManagerUpdateHook())
    timer = new AnomalyTimer;
  if (timer)
    spdlog::info("Galactic anomaly countdown enabled (below Help All / Engage / Claim)");
}

// [graphics] galactic_anomaly_timer: a countdown for the active galactic anomaly in the viewed system, under the HUD's
// Help All / Engage / Claim drawer. All Unity work runs on the frame tick; there are no detours of its own.

#include "config.h"
#include "features/features.h"

#include "game/app.h"
#include "game/hud.h"
#include "game/ui.h"

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/ui.h>

#include <chrono>
#include <cstdint>
#include <utility>

namespace
{
using Clock = std::chrono::steady_clock;

// The rounded key-hint background next to the native anomaly button, reused for the panel.
unity::Sprite* NativeBackgroundSprite(HudNavigationViewController* nav)
{
  auto* button     = nav->galacticAnomaliesButtonWidget();
  auto* transform  = button ? button->transform() : nullptr;
  auto* parent     = transform ? transform->parent() : nullptr;
  auto* background = parent ? parent->Find("ShortcutKeybindHint/Background") : nullptr;
  auto* object     = background ? background->gameObject() : nullptr;
  auto* image      = object ? object->GetComponent<unity::Image>() : nullptr;
  if (!image)
    return nullptr;
  // ImageSelector applies localized artwork through overrideSprite.
  auto* sprite = image->overrideSprite();
  return sprite ? sprite : image->sprite();
}

unity::Image* AddImage(unity::GameObject* object, unity::Color tint, unity::Sprite* sprite)
{
  auto* image = object->AddComponent<unity::Image>();
  if (!image)
    return nullptr;
  image->SetColor(tint);
  image->SetRaycastTarget(false);
  if (sprite && sprite->pixelsPerUnit() > 0) {
    image->SetSprite(sprite);
    image->SetType(unity::Image::Type::Sliced);
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
    if (!Config::Get().graphics.galactic_anomaly_timer) {
      DestroyPanel();
      navigation_.Reset();
      return;
    }

    auto* nav = Navigation();
    // Remaining ticks of the active anomaly in the viewed system.
    const int64_t ticks = nav && nav->isActiveAndEnabled() && nav->CanShowGalacticAnomaly() ? RemainingTicks(nav) : 0;
    if (ticks <= 0) {
      if (auto* panel = Label() ? Label()->ParentObject() : nullptr)
        panel->SetActive(false);
      return;
    }
    if (!Label() && !Create(nav)) {
      static bool warned = false;
      if (!std::exchange(warned, true))
        spdlog::warn("[AnomalyTimer] could not build the countdown panel; leaving the HUD unchanged");
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
      Label()->SetText(text);
    }
    if (auto* panel = Label()->ParentObject())
      panel->SetActive(true);
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
    if (unity::Alive(held) && held->isActiveAndEnabled())
      return held;
    if (const auto now = Clock::now(); now >= next_discovery_) {
      next_discovery_   = now + std::chrono::seconds(1);
      auto* replacement = ScreenManager::View<HudNavigationViewController>();
      if ((replacement && replacement != held) || !unity::Alive(held)) {
        DestroyPanel();
        navigation_ = il2cpp::GcHandle::Strong(replacement);
      }
    }
    auto* nav = navigation_.Get<HudNavigationViewController>();
    return unity::Alive(nav) ? nav : nullptr;
  }

  unity::TMP_Text* Label()
  {
    auto* label = label_.Get<unity::TMP_Text>();
    return unity::Alive(label) ? label : nullptr;
  }

  void DestroyPanel()
  {
    if (auto* panel = Label() ? Label()->ParentObject() : nullptr) {
      panel->SetActive(false);
      unity::Destroy(panel);
    }
    label_.Reset();
    shown_seconds_ = -1;
  }

  static int64_t RemainingTicks(HudNavigationViewController* nav)
  {
    auto*   context = nav->CanvasContext();
    auto*   address = context ? context->ViewingAddress() : nullptr;
    int64_t system  = address ? address->System() : -1;
    auto*   manager = system > 0 ? GalacticAnomaliesManager::Current() : nullptr;
    auto*   anomaly = manager ? manager->GetSystemGalacticAnomalies(system) : nullptr;
    if (!anomaly || !anomaly->IsActive())
      return 0;
    auto* timer = anomaly->EndTimeTimerDataContext();
    return timer ? timer->RemainingTime().ticks : 0;
  }

  // Builds the panel: the native rounded sprite twice, with a narrow inset making the border, and a countdown in
  // the system-name font. A missing sprite still gives a visible frame.
  bool Create(HudNavigationViewController* nav)
  {
    auto* helper = nav->factionInformationHelper();
    auto* names  = helper ? helper->systemName() : nullptr;
    auto* name   = names && names->Count() > 0 ? (*names)[0] : nullptr;
    if (!unity::Alive(name))
      return false;
    auto* drawer    = ScreenManager::View<HudAllianceAndNewsViewController>();
    auto* promotion = drawer ? drawer->chestPromotion() : nullptr;
    auto* chest     = promotion ? promotion->transform() : nullptr;
    auto* parent    = chest ? chest->parent() : nullptr;
    auto* source    = name->gameObject() ? name->gameObject()->GetComponent<unity::TextMeshProUGUI>() : nullptr;
    auto* font      = source ? source->font() : nullptr;
    if (!parent || !font)
      return false;

    il2cpp::GcHandle panel, fill, text;
    auto*            panel_rect = unity::CreateUiObject("CommunityMod_AnomalyTimer", parent, panel);
    if (!panel_rect)
      return false;
    panel_rect->Place({0.5f, 0}, {0.5f, 0}, {0.5f, 1}, {198, 38}, {0, -40});
    auto* rounded = NativeBackgroundSprite(nav);
    if (!AddImage(panel.Get<unity::GameObject>(), {0.64f, 0.34f, 0.46f, 0.85f}, rounded))
      return Abandon(panel);

    auto* fill_rect = unity::CreateUiObject("AnomalyFill", panel_rect, fill);
    if (!fill_rect || !AddImage(fill.Get<unity::GameObject>(), {0.06f, 0.10f, 0.14f, 0.96f}, rounded))
      return Abandon(panel);
    fill_rect->Place({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {194, 34}, {0, 0});
    fill.Get<unity::GameObject>()->SetActive(true);

    auto* text_rect = unity::CreateUiObject("Countdown", panel_rect, text);
    auto* label     = text_rect ? text.Get<unity::GameObject>()->AddComponent<unity::TextMeshProUGUI>() : nullptr;
    if (!label)
      return Abandon(panel);
    text_rect->Place({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {190, 28}, {0, 0});
    label->SetFont(font);
    label->SetFontSize(22);
    label->SetAlignment(unity::TMP_Text::Alignment::Center);
    label->SetColor({0.95f, 0.83f, 0.89f, 1});
    label->SetRaycastTarget(false);
    label->SetEnableWordWrapping(false);
    text.Get<unity::GameObject>()->SetActive(true);
    // The children are owned by the hierarchy once parented; only the label is kept.
    label_ = il2cpp::GcHandle::Strong(label);
    return true;
  }

  static bool Abandon(il2cpp::GcHandle& panel)
  {
    if (auto* object = panel.Get<unity::GameObject>(); unity::Alive(object))
      unity::Destroy(object);
    return false;
  }

  il2cpp::GcHandle  navigation_, label_;
  Clock::time_point next_update_{}, next_discovery_{};
  int64_t           shown_seconds_ = -1;
};

// Leaked on purpose: its GC handles must not be released by static destructors after the runtime is gone.
AnomalyTimer* anomaly_timer = nullptr;

void AnomalyTimerFrame()
{ anomaly_timer->Update(); }

// No hooks: the countdown is all frame work.
bool InstallAnomalyTimer()
{
  anomaly_timer = new AnomalyTimer;
  return true;
}
} // namespace

const Feature kAnomalyTimer{"AnomalyTimer", &Config::Patches::galacticanomalytimerhooks, InstallAnomalyTimer,
                            AnomalyTimerFrame};

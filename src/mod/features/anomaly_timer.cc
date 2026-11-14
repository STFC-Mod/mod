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

namespace
{
// The countdown label, under a panel parented to the upper-right drawer, so it follows the drawer's scale and
// visibility without moving any native button. Unity destroys it with the drawer; it is then built again. Never
// destroyed: freeing a GC handle in a static destructor can run after the runtime has shut down.
auto&   anomaly_label         = *new il2cpp::GcHandle;
int64_t anomaly_shown_seconds = -1;

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

void AddPanelImage(unity::GameObject* object, unity::Color tint, unity::Sprite* sprite)
{
  auto* image = object->AddComponent<unity::Image>();
  if (!image)
    return;
  image->SetColor(tint);
  image->SetRaycastTarget(false);
  if (sprite && sprite->pixelsPerUnit() > 0) {
    image->SetSprite(sprite);
    image->SetType(unity::Image::Type::Sliced);
    // Sliced borders are drawn at the sprite's pixel density; normalizing it to 100 keeps the corners the same size
    // whatever the localized artwork's density is.
    image->SetPixelsPerUnitMultiplier(100 / sprite->pixelsPerUnit());
  }
}

int64_t AnomalyRemainingTicks(HudNavigationViewController* nav)
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

// Builds the panel: the native rounded sprite twice, with a narrow inset making the border, and a countdown in the
// system-name font. A missing sprite still gives a visible frame. Null while the drawer or the HUD's font is not up
// yet; the next tick tries again.
unity::TMP_Text* CreateAnomalyPanel(HudNavigationViewController* nav)
{
  auto* helper = nav->factionInformationHelper();
  auto* name   = il2cpp::At(helper ? helper->systemName() : nullptr, 0);
  if (!unity::Alive(name))
    return nullptr;
  auto* drawer    = ScreenManager::View<HudAllianceAndNewsViewController>();
  auto* promotion = drawer ? drawer->chestPromotion() : nullptr;
  auto* chest     = promotion ? promotion->transform() : nullptr;
  auto* parent    = chest ? chest->parent() : nullptr;
  auto* object    = name->gameObject();
  auto* source    = object ? object->GetComponent<unity::TextMeshProUGUI>() : nullptr;
  auto* font      = source ? source->font() : nullptr;
  if (!parent || !font)
    return nullptr;

  // The children are owned by the hierarchy once parented; only the label is kept. The objects below are null only
  // when a Unity class is missing, which the library reports; a half-built panel then stays inactive.
  il2cpp::GcHandle panel, fill, text;
  auto*            panel_rect = unity::CreateUiObject("CommunityMod_AnomalyTimer", parent, panel);
  if (!panel_rect)
    return nullptr;
  panel_rect->Place({0.5f, 0}, {0.5f, 0}, {0.5f, 1}, {198, 38}, {0, -40});
  auto* rounded = NativeBackgroundSprite(nav);
  AddPanelImage(panel.Get<unity::GameObject>(), {0.64f, 0.34f, 0.46f, 0.85f}, rounded);

  auto* fill_rect = unity::CreateUiObject("AnomalyFill", panel_rect, fill);
  if (!fill_rect)
    return nullptr;
  AddPanelImage(fill.Get<unity::GameObject>(), {0.06f, 0.10f, 0.14f, 0.96f}, rounded);
  fill_rect->Place({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {194, 34}, {0, 0});
  fill.Get<unity::GameObject>()->SetActive(true);

  auto* text_rect = unity::CreateUiObject("Countdown", panel_rect, text);
  auto* label     = text_rect ? text.Get<unity::GameObject>()->AddComponent<unity::TextMeshProUGUI>() : nullptr;
  if (!label)
    return nullptr;
  text_rect->Place({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {190, 28}, {0, 0});
  label->SetFont(font);
  label->SetFontSize(22);
  label->SetAlignment(unity::TMP_Text::Alignment::Center);
  label->SetColor({0.95f, 0.83f, 0.89f, 1});
  label->SetRaycastTarget(false);
  label->SetEnableWordWrapping(false);
  text.Get<unity::GameObject>()->SetActive(true);
  anomaly_label         = il2cpp::GcHandle::Strong(label);
  anomaly_shown_seconds = -1;
  return label;
}

void AnomalyTimerFrame()
{
  static Throttle throttle(std::chrono::milliseconds(250));
  if (!throttle.Due())
    return;

  // View only returns a HUD that is up on a visible canvas, so nothing about it needs holding between ticks.
  auto* nav = Config::Get().graphics.galactic_anomaly_timer && Hub::CurrentSection() == SectionID::Navigation_System
                  ? ScreenManager::View<HudNavigationViewController>()
                  : nullptr;
  const int64_t ticks = nav && nav->CanShowGalacticAnomaly() ? AnomalyRemainingTicks(nav) : 0;
  auto*         label = anomaly_label.Get<unity::TMP_Text>();
  if (!unity::Alive(label))
    label = nullptr;
  if (ticks <= 0) {
    if (auto* panel = label ? label->ParentObject() : nullptr)
      panel->SetActive(false);
    return;
  }
  if (!label && !(label = CreateAnomalyPanel(nav)))
    return;

  // TimeSpan ticks are 100 ns. Round up, so the last second shows as 1s rather than 0s.
  const auto seconds = ticks / 10'000'000 + (ticks % 10'000'000 != 0);
  if (seconds != anomaly_shown_seconds) {
    anomaly_shown_seconds = seconds;
    label->SetText(seconds >= 3600 ? fmt::format("{}h {:02}m {:02}s", seconds / 3600, seconds / 60 % 60, seconds % 60)
                                   : fmt::format("{:02}m {:02}s", seconds / 60, seconds % 60));
  }
  if (auto* panel = label->ParentObject())
    panel->SetActive(true);
}
} // namespace

const Feature kAnomalyTimer{"AnomalyTimer", &Config::Patches::galacticanomalytimerhooks, nullptr, AnomalyTimerFrame};

// [ui] forbidden_tech_ship_name: shows the ship a forbidden tech is equipped on below the header of the large
// selected-item panel, including ships without a dock. Inventory tiles keep their native dock badges.

#include "config.h"
#include "features/features.h"

#include "game/fleet.h"
#include "game/inventory.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/core.h>
#include <unity/ui.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace
{
// --- Equipped ship ---

// The ship the widget's tech is fitted to, by name in the player's language; empty when it is not fitted.
std::string EquippedShipName(ForbiddenTechInventoryItemWidget* widget)
{
  auto* context = widget->Context();
  auto* tech    = context ? context->ForbiddenTech() : nullptr;
  auto* manager = tech ? ForbiddenTechManager::Instance() : nullptr;
  if (!manager)
    return {};
  // The fleet-index API returns -1 for a ship in the hangar. Resolve the slot's OwnerId instead; it identifies the
  // actual ship regardless of dock assignment.
  EntitySlot* slot = nullptr;
  if (!manager->TryGetEntitySlotFromForbiddenTech(tech->Id(), slot) || !slot)
    return {};
  auto* params  = slot->ForbiddenTechSlotParams();
  auto* service = params ? manager->FleetService() : nullptr;
  auto* ship    = service ? service->GetShip(params->OwnerId()) : nullptr;
  auto* hull    = ship ? ship->Hull() : nullptr;
  if (!hull)
    return {};
  // Hull IDs and localisation IDs differ on many ships. Mirror FleetPlayerData.GetLocaId.
  auto* refs = hull->idRefs();
  return LocalizedShipName(refs ? refs->locaId() : hull->id());
}

// --- Ship name rows ---

// The row takes this much from the top of the panel's scrollable buff list.
constexpr float kShipNameRowHeight = 30.0f;

// The panel's own labels and lists, as the components they are.
unity::GameObject* ShipNameObjectOf(il2cpp::Object* component)
{
  auto* as_component = component->As<unity::Component>();
  return as_component ? as_component->gameObject() : nullptr;
}
unity::RectTransform* ShipNameRectOf(il2cpp::Object* component)
{
  auto* as_component = component->As<unity::Component>();
  auto* transform    = as_component ? as_component->transform() : nullptr;
  return transform ? transform->As<unity::RectTransform>() : nullptr;
}

// The ship name row added to one large panel, found again by the panel's buff list. The handles are weak: the panel
// owns both objects and may destroy them.
struct ShipNameRow {
  il2cpp::GcHandle scroller;
  il2cpp::GcHandle label;
  // The buff list's top edge without the row.
  unity::Vector2 original_top{};
  bool           visible = false;

  unity::RectTransform* Scroller() const
  { return scroller.Get<unity::RectTransform>(); }
  TextLocalizer* Label() const
  { return label.Get<TextLocalizer>(); }

  void Hide()
  {
    if (auto* label_object = unity::Alive(Label()) ? ShipNameObjectOf(Label()) : nullptr)
      label_object->SetActive(false);
    if (visible && unity::Alive(Scroller()))
      Scroller()->SetOffsetMax(original_top);
    visible = false;
  }

  // Shows `name` in the row, cloning the panel's name label on first use. Null when shown, otherwise what failed.
  const char* Show(ForbiddenTechInventoryItemWidget* widget, unity::RectTransform* list, const std::string& name)
  {
    auto* text_label = Label();
    if (!unity::Alive(text_label)) {
      auto* source = widget->name();
      if (!unity::Alive(source))
        return nullptr;
      text_label = unity::Instantiate(source, list->parent());
      if (!unity::Alive(text_label))
        return "Forbidden Tech ship label could not be created";
      label = il2cpp::GcHandle::Weak(text_label);
    }
    auto* label_object = ShipNameObjectOf(text_label);
    if (!label_object)
      return "Forbidden Tech label object unavailable";
    label_object->SetActive(false);
    auto* label_rect = ShipNameRectOf(text_label);
    if (!label_rect)
      return "Forbidden Tech ship label layout unavailable";
    const auto min     = list->offsetMin();
    const auto max     = original_top;
    const auto anchors = list->anchorMax();
    label_rect->SetAnchorMin({list->anchorMin().x, anchors.y});
    label_rect->SetAnchorMax(anchors);
    label_rect->SetPivot({0.5f, 1.0f});
    label_rect->SetOffsetMin({min.x, max.y - kShipNameRowHeight});
    label_rect->SetOffsetMax(max);

    // Target resolves the graphic lazily, even when the source label was inactive at clone time.
    auto* target = text_label->Target();
    auto* text   = target ? target->As<unity::TMP_Text>() : nullptr;
    if (!text)
      return "Forbidden Tech ship label text unavailable";
    text->SetAlignment(unity::TMP_Text::Alignment::Left); // Left, vertically centred.
    text->SetRaycastTarget(false);
    text->SetRichText(false); // Ship names are plain text, never markup.
    text->SetEnableWordWrapping(false);
    text->SetOverflowMode(unity::TMP_Text::Overflow::Ellipsis); // Long names stay within this row.
    text->SetEnableAutoSizing(false);
    text->SetFontSize(20.0f);
    text->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    text_label->OverrideLocalizedText(il2cpp::NewString("Equipped on: " + name));
    // Keep the panel's outer size, reserving one row from the scrollable buff area.
    list->SetOffsetMax({max.x, max.y - kShipNameRowHeight});
    visible = true;
    label_object->SetActive(true);
    static bool reported = false;
    if (!std::exchange(reported, true))
      spdlog::info("[ForbiddenTechShipName] equipped-ship row created and populated");
    return nullptr;
  }
};
std::vector<ShipNameRow> ship_name_rows;

// Hides the row of the widget's panel, if it has one.
void HideShipNameRow(ForbiddenTechInventoryItemWidget* widget)
{
  auto* scroller = widget->buffListScroller();
  if (!unity::Alive(scroller))
    return;
  auto* list = ShipNameRectOf(scroller);
  for (auto& row : ship_name_rows)
    if (row.Scroller() == list)
      row.Hide();
}

// Shows the equipped ship in the large selected-item panel after the game refreshed it. Null unless something
// failed, which then says what.
const char* UpdateShipNameRow(ForbiddenTechInventoryItemWidget* widget)
{
  // Only the large selected-item panel: inventory tiles retain their native dock badges.
  if (widget->widgetType() != ForbiddenTechInventoryItemWidget::Type::LargeShowcase)
    return nullptr;
  auto* scroller = widget->buffListScroller();
  if (!unity::Alive(scroller))
    return nullptr;
  auto* list = ShipNameRectOf(scroller);
  if (!list)
    return "Forbidden Tech buff layout unavailable";

  std::erase_if(ship_name_rows, [](const ShipNameRow& row) { return !unity::Alive(row.Scroller()); });
  auto row = std::ranges::find(ship_name_rows, list, &ShipNameRow::Scroller);
  if (row == ship_name_rows.end()) {
    ship_name_rows.push_back({.scroller = il2cpp::GcHandle::Weak(list), .original_top = list->offsetMax()});
    row = std::prev(ship_name_rows.end());
  }
  // Clear before resolving a new assignment so errors/unassigned tech cannot retain an old ship name.
  row->Hide();
  // Native refresh may have resized the panel since the previous selection.
  row->original_top = list->offsetMax();
  if (!Config::Get().ui.forbidden_tech_ship_name)
    return nullptr;
  const auto name = EquippedShipName(widget);
  if (name.empty())
    return nullptr;
  const char* failure = row->Show(widget, list, name);
  if (failure)
    row->Hide();
  return failure;
}

// --- Hooks ---

void ForbiddenTechInventoryItemWidget_SetWidgetData_Hook(auto original, ForbiddenTechInventoryItemWidget* _this)
{
  HideShipNameRow(_this);
  original(_this);
  if (const char* failure = UpdateShipNameRow(_this)) {
    static bool warned = false;
    if (!std::exchange(warned, true))
      spdlog::warn("[ForbiddenTechShipName] native display retained: {}", failure);
  }
}

void ForbiddenTechInventoryItemWidget_ClearWidgetData_Hook(auto original, ForbiddenTechInventoryItemWidget* _this)
{
  HideShipNameRow(_this);
  original(_this);
}

bool InstallForbiddenTechShipName()
{
  auto refresh = ForbiddenTechInventoryItemWidget::Class::Method("SetWidgetData", 0);
  auto clear   = ForbiddenTechInventoryItemWidget::Class::Method("ClearWidgetData", 0);
  // Both methods perform substantive native UI updates, rather than forwarding through tiny wrappers. The lookups
  // walk parents; only detour the widget's own overrides, never the Widget`1 base versions that run for every widget.
  const auto inherited = [](const il2cpp::Method& method) {
    return method && method.Info()->klass != ForbiddenTechInventoryItemWidget::Class::Raw();
  };
  if (inherited(refresh) || inherited(clear)) {
    spdlog::warn("[ForbiddenTechShipName] the widget no longer overrides SetWidgetData and ClearWidgetData; not "
                 "hooking the shared ones");
    return false;
  }
  // One set: the refresh never runs without the cleanup, so a failure leaves the native panel untouched.
  il2cpp::HookSet hooks("ForbiddenTechShipName");
  IL2CPP_STAGE(hooks, clear, ForbiddenTechInventoryItemWidget_ClearWidgetData_Hook);
  IL2CPP_STAGE(hooks, refresh, ForbiddenTechInventoryItemWidget_SetWidgetData_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kForbiddenTechShipName{"ForbiddenTechShipName", &Config::Patches::forbiddentechshipnamehooks,
                                     InstallForbiddenTechShipName};

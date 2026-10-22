// forbidden_tech_ship_name: shows the ship a forbidden tech is equipped on below the header of the large selected-item
// panel, including ships without a dock. Inventory tiles keep their native dock badges.

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
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr float RowHeight = 30.0f;

unity::RectTransform* RectOf(il2cpp::Object* component)
{
  auto* as_component = component ? component->As<unity::Component>() : nullptr;
  auto* transform    = as_component ? as_component->transform() : nullptr;
  return transform ? transform->As<unity::RectTransform>() : nullptr;
}

void SetActive(il2cpp::Object* component, bool active)
{
  auto* as_component = component->As<unity::Component>();
  auto* object       = as_component ? as_component->gameObject() : nullptr;
  if (!object)
    throw std::runtime_error("Forbidden Tech label object unavailable");
  object->SetActive(active);
}

std::string EquippedShipName(ForbiddenTechInventoryItemWidget* widget)
{
  auto* context = widget->Context();
  auto* tech    = context ? context->ForbiddenTech() : nullptr;
  if (!tech)
    return {};
  const auto id      = tech->Id();
  auto*      manager = ForbiddenTechManager::Instance();
  if (!manager)
    return {};

  // The fleet-index API returns -1 for a ship in the hangar. Resolve the slot's
  // OwnerId instead; it identifies the actual ship regardless of dock assignment.
  EntitySlot* slot = nullptr;
  if (!manager->TryGetEntitySlotFromForbiddenTech(id, slot) || !slot)
    return {};
  auto* params = slot->ForbiddenTechSlotParams();
  if (!params)
    return {};
  const auto owner = params->OwnerId();

  auto* service = manager->FleetService();
  if (!service)
    return {};
  auto* ship = service->GetShip(owner);
  auto* hull = ship ? ship->Hull() : nullptr;
  if (!hull)
    return {};
  // Hull IDs and localisation IDs differ on many ships. Mirror FleetPlayerData.GetLocaId.
  auto*      refs    = hull->idRefs();
  const auto loca_id = refs ? refs->locaId() : hull->id();
  return LocalizedShipName(loca_id);
}

struct Row {
  il2cpp::GcHandle scroller;
  il2cpp::GcHandle label;
  unity::Vector2   original_top{};
  bool             visible = false;

  unity::RectTransform* Scroller() const
  { return scroller.Get<unity::RectTransform>(); }
  TextLocalizer* Label() const
  { return label.Get<TextLocalizer>(); }

  void Hide()
  {
    if (unity::Alive(Label()))
      SetActive(Label(), false);
    if (visible && unity::Alive(Scroller()))
      Scroller()->SetOffsetMax(original_top);
    visible = false;
  }
};
std::vector<Row> rows;

void HideWidgetRow(ForbiddenTechInventoryItemWidget* widget)
{
  auto* scroller = widget->buffListScroller();
  if (!unity::Alive(scroller))
    return;
  auto* rect = RectOf(scroller);
  for (auto& row : rows) {
    if (row.Scroller() == rect)
      row.Hide();
  }
}

void UpdateRow(ForbiddenTechInventoryItemWidget* widget)
{
  // Only the large selected-item panel: inventory tiles retain their native dock badges.
  if (widget->widgetType() != ForbiddenTechInventoryItemWidget::Type::LargeShowcase)
    return;

  auto* scroller = widget->buffListScroller();
  if (!unity::Alive(scroller))
    return;
  auto* rect = RectOf(scroller);
  if (!rect)
    throw std::runtime_error("Forbidden Tech buff layout unavailable");

  std::erase_if(rows, [](const Row& row) { return !unity::Alive(row.Scroller()); });
  auto found = std::find_if(rows.begin(), rows.end(), [rect](const Row& row) { return row.Scroller() == rect; });
  if (found == rows.end()) {
    Row row;
    row.original_top = rect->offsetMax();
    row.scroller     = il2cpp::GcHandle::Weak(rect);
    if (!row.scroller)
      return;
    rows.push_back(std::move(row));
    found = std::prev(rows.end());
  }
  auto& row = *found;
  // Clear before resolving a new assignment so errors/unassigned tech cannot retain an old ship name.
  row.Hide();
  // Native refresh may have resized the panel since the previous selection.
  row.original_top = rect->offsetMax();
  if (!Config::Get().ui.forbidden_tech_ship_name)
    return;
  const auto name = EquippedShipName(widget);
  if (name.empty())
    return;

  try {
    auto* label = row.Label();
    if (!unity::Alive(label)) {
      auto* source = widget->name();
      if (!unity::Alive(source))
        return;
      label = unity::Instantiate(source, rect->parent());
      if (!unity::Alive(label))
        throw std::runtime_error("Forbidden Tech ship label could not be created");
      SetActive(label, false);
      row.label = il2cpp::GcHandle::Weak(label);
      if (!row.label)
        throw std::runtime_error("Forbidden Tech label handle unavailable");
    }
    auto* label_rect = RectOf(label);
    if (!label_rect)
      throw std::runtime_error("Forbidden Tech ship label layout unavailable");
    const auto min     = rect->offsetMin();
    const auto max     = row.original_top;
    const auto anchors = rect->anchorMax();
    auto anchor_min    = rect->anchorMin();
    anchor_min.y       = anchors.y;
    label_rect->SetAnchorMin(anchor_min);
    label_rect->SetAnchorMax(anchors);
    label_rect->SetPivot({0.5f, 1.0f});
    label_rect->SetOffsetMin({min.x, max.y - RowHeight});
    label_rect->SetOffsetMax(max);

    // Target resolves the graphic lazily, even when the source label was inactive at clone time.
    auto* target = label->Target();
    auto* text   = target ? target->As<unity::TMP_Text>() : nullptr;
    if (!text)
      throw std::runtime_error("Forbidden Tech ship label text unavailable");
    text->SetAlignment(unity::TMP_Text::Alignment::Left); // Left, vertically centred.
    text->SetRaycastTarget(false);
    text->SetRichText(false); // Ship names are plain text, never markup.
    text->SetEnableWordWrapping(false);
    text->SetOverflowMode(unity::TMP_Text::Overflow::Ellipsis); // Long names stay within this row.
    text->SetEnableAutoSizing(false);
    text->SetFontSize(20.0f);
    text->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    label->OverrideLocalizedText(il2cpp::NewString("Equipped on: " + name));
    // Keep the panel's outer size, reserving one row from the scrollable buff area.
    rect->SetOffsetMax({max.x, max.y - RowHeight});
    row.visible = true;
    SetActive(label, true);
    static bool reported = false;
    if (!reported) {
      reported = true;
      spdlog::info("[ForbiddenTechShipName] equipped-ship row created and populated");
    }
  } catch (...) {
    row.Hide();
    throw;
  }
}

void ForbiddenTechInventoryItemWidget_SetWidgetData_Hook(auto original, ForbiddenTechInventoryItemWidget* _this)
{
  try {
    HideWidgetRow(_this);
  } catch (const std::exception&) {
  }
  original(_this);
  try {
    UpdateRow(_this);
  } catch (const std::exception& error) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      spdlog::warn("[ForbiddenTechShipName] native display retained: {}", error.what());
    }
  }
}

void ForbiddenTechInventoryItemWidget_ClearWidgetData_Hook(auto original, ForbiddenTechInventoryItemWidget* _this)
{
  try {
    HideWidgetRow(_this);
  } catch (const std::exception&) {
  }
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

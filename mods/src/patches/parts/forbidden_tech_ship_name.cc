#include "config.h"
#include "ship_name_match.h"

#include <prime/Color.h>
#include <prime/Component.h>
#include <prime/HullSpec.h>
#include <prime/MonoSingleton.h>
#include <prime/RectTransform.h>
#include <prime/TextLocalizer.h>
#include <prime/Vector2.h>
#include <prime/Widget.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr float RowHeight = 30.0f;

using UnityObject = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;

struct ForbiddenTech : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ForbiddenTech">;

  int64_t Id()
  { return Get<"Id", int64_t>(); }
};

struct ForbiddenTechInventoryItem : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "InventoryItem">;

  ForbiddenTech* forbiddenTech()
  { return Get<"ForbiddenTech", ForbiddenTech*>(); }
};

struct ForbiddenTechSlotParams : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ForbiddenTechSlotParams">;

  int64_t OwnerId()
  { return Get<"OwnerId", int64_t>(); }
};

struct ForbiddenTechEntitySlot : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "EntitySlot">;

  ForbiddenTechSlotParams* forbiddenTechSlotParams()
  { return Get<"ForbiddenTechSlotParams", ForbiddenTechSlotParams*>(); }
};

// GetShip returns an IShipModel, read on its runtime class.
struct ForbiddenTechFleetService : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "FleetService">;

  il2cpp::Object* GetShip(int64_t ship_id)
  { return Call<"GetShip", il2cpp::Object*>(ship_id); }
};

struct ForbiddenTechManager : MonoSingleton<ForbiddenTechManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechManager">;

  bool TryGetEntitySlotFromForbiddenTech(int64_t tech_id, ForbiddenTechEntitySlot*& slot)
  { return Call<"TryGetEntitySlotFromForbiddenTech", bool>(tech_id, std::ref(slot)); }

  // _fleetServiceCache is a CachedService<FleetService> struct whose Service getter resolves the
  // service lazily. The getter runs on a copy, so the manager's own cache is left as it was; the
  // runtime takes the struct receiver unboxed.
  ForbiddenTechFleetService* fleetService()
  {
    struct CachedService {
      ForbiddenTechFleetService* service;
    };
    auto cache = Field<"_fleetServiceCache", CachedService>();
    if (cache.service)
      return cache.service;
    static FieldInfo* const field =
        Class::Raw() ? il2cpp_class_get_field_from_name(Class::Raw(), "_fleetServiceCache") : nullptr;
    static const MethodInfo* const getter =
        field ? il2cpp_class_get_method_from_name(il2cpp_class_from_type(il2cpp_field_get_type(field)), "get_Service",
                                                  0)
              : nullptr;
    if (!getter)
      return nullptr;
    return reinterpret_cast<ForbiddenTechFleetService*>(il2cpp::detail::InvokeBoxed(getter, &cache, nullptr));
  }
};

struct ForbiddenTechInventoryItemWidget : Widget<ForbiddenTechInventoryItem> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechInventoryItemWidget">;

  // ForbiddenTechInventoryItemWidget.WidgetType; 1 is the large selected-item panel.
  int32_t widgetType()
  { return Field<"_widgetType", int32_t>(); }
  // SmartScroller
  Component* buffListScroller()
  { return Field<"_buffListScroller", Component*>(); }
  TextLocalizer* name()
  { return Field<"_name", TextLocalizer*>(); }
};

struct TMP_Text : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.TextMeshPro", "TMPro", "TMP_Text">;

  // TextAlignmentOptions
  void SetAlignment(int32_t value)
  { Set<"alignment">(value); }
  void SetRaycastTarget(bool value)
  { Set<"raycastTarget">(value); }
  void SetRichText(bool value)
  { Set<"richText">(value); }
  void SetEnableWordWrapping(bool value)
  { Set<"enableWordWrapping">(value); }
  // TextOverflowModes
  void SetOverflowMode(int32_t value)
  { Set<"overflowMode">(value); }
  void SetEnableAutoSizing(bool value)
  { Set<"enableAutoSizing">(value); }
  void SetFontSize(float value)
  { Set<"fontSize">(value); }
  void SetColor(color value)
  { Set<"color">(value); }
};

// Unity's `bool(UnityEngine.Object)`: false for null and for destroyed native objects.
bool Alive(il2cpp::Object* object)
{ return object && UnityObject::Call<"op_Implicit", bool>(object); }

RectTransform* RectOf(il2cpp::Object* component)
{
  auto* as_component = component ? component->As<Component>() : nullptr;
  auto* transform    = as_component ? as_component->transform() : nullptr;
  return transform ? transform->As<RectTransform>() : nullptr;
}

void SetActive(il2cpp::Object* component, bool active)
{
  auto* as_component = component->As<Component>();
  auto* object       = as_component ? as_component->gameObject() : nullptr;
  if (!object)
    throw std::runtime_error("Forbidden Tech label object unavailable");
  object->SetActive(active);
}

std::string EquippedShipName(ForbiddenTechInventoryItemWidget* widget)
{
  auto* context = widget->Context();
  auto* tech    = context ? context->forbiddenTech() : nullptr;
  if (!tech)
    return {};
  const auto id      = tech->Id();
  auto*      manager = ForbiddenTechManager::Instance();
  if (!manager)
    return {};

  // The fleet-index API returns -1 for a ship in the hangar. Resolve the slot's
  // OwnerId instead; it identifies the actual ship regardless of dock assignment.
  ForbiddenTechEntitySlot* slot = nullptr;
  if (!manager->TryGetEntitySlotFromForbiddenTech(id, slot) || !slot)
    return {};
  auto* params = slot->forbiddenTechSlotParams();
  if (!params)
    return {};
  const auto owner = params->OwnerId();

  auto* service = manager->fleetService();
  if (!service)
    return {};
  auto* ship = service->GetShip(owner);
  auto* hull = ship ? ship->Get<"Hull", HullSpec*>() : nullptr;
  if (!hull)
    return {};
  // Hull IDs and localisation IDs differ on many ships. Mirror FleetPlayerData.GetLocaId.
  auto*      refs    = hull->idRefs();
  const auto loca_id = refs ? refs->locaId() : hull->Id();
  return ShipNameMatch::LocalizedShipName(loca_id);
}

struct Row {
  il2cpp::GcHandle scroller;
  il2cpp::GcHandle label;
  Vector2          original_top{};
  bool             visible = false;

  RectTransform* Scroller() const
  { return scroller.Get<RectTransform>(); }
  TextLocalizer* Label() const
  { return label.Get<TextLocalizer>(); }

  void Hide()
  {
    if (Alive(Label()))
      SetActive(Label(), false);
    if (visible && Alive(Scroller()))
      Scroller()->SetOffsetMax(original_top);
    visible = false;
  }
};
std::vector<Row> rows;

void HideWidgetRow(ForbiddenTechInventoryItemWidget* widget)
{
  auto* scroller = widget->buffListScroller();
  if (!Alive(scroller))
    return;
  auto* rect = RectOf(scroller);
  for (auto& row : rows) {
    if (row.Scroller() == rect)
      row.Hide();
  }
}

TextLocalizer* CloneLabel(TextLocalizer* source, Transform* parent)
{
  // This client exposes the two-argument overload only as Instantiate<T>.
  // Use the non-generic three-argument overload so no generic inflation is required.
  auto* clone =
      UnityObject::Call<"Instantiate", il2cpp::Object*(il2cpp::Object*, Transform*, bool)>(source, parent, false);
  if (!Alive(clone))
    throw std::runtime_error("Forbidden Tech ship label could not be created");
  return static_cast<TextLocalizer*>(clone);
}

void UpdateRow(ForbiddenTechInventoryItemWidget* widget)
{
  // Only the large selected-item panel: inventory tiles retain their native dock badges.
  if (widget->widgetType() != 1)
    return;

  auto* scroller = widget->buffListScroller();
  if (!Alive(scroller))
    return;
  auto* rect = RectOf(scroller);
  if (!rect)
    throw std::runtime_error("Forbidden Tech buff layout unavailable");

  std::erase_if(rows, [](const Row& row) { return !Alive(row.Scroller()); });
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
  if (!Config::Get().forbidden_tech_ship_name)
    return;
  const auto name = EquippedShipName(widget);
  if (name.empty())
    return;

  try {
    auto* label = row.Label();
    if (!Alive(label)) {
      auto* source = widget->name();
      if (!Alive(source))
        return;
      label = CloneLabel(source, rect->parent());
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
    auto* text   = target ? target->As<TMP_Text>() : nullptr;
    if (!text)
      throw std::runtime_error("Forbidden Tech ship label text unavailable");
    text->SetAlignment(513); // TMP TextAlignmentOptions.Left (left, vertically centred).
    text->SetRaycastTarget(false);
    text->SetRichText(false); // Ship names are plain text, never markup.
    text->SetEnableWordWrapping(false);
    text->SetOverflowMode(1); // TMP TextOverflowModes.Ellipsis; long names stay within this row.
    text->SetEnableAutoSizing(false);
    text->SetFontSize(20.0f);
    text->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    label->OverrideLocalizedText(il2cpp_string_new(("Equipped on: " + name).c_str()));
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
} // namespace

void InstallForbiddenTechShipNameHooks()
{
  auto method = ForbiddenTechInventoryItemWidget::Class::Method("SetWidgetData", 0);
  auto clear  = ForbiddenTechInventoryItemWidget::Class::Method("ClearWidgetData", 0);
  // Both methods perform substantive native UI updates, rather than forwarding through tiny wrappers.
  // Only the widget's own overrides qualify; the Widget`1 base versions run for every widget.
  const auto own = [](il2cpp::Method info) {
    return info && info.Info()->klass == ForbiddenTechInventoryItemWidget::Class::Raw();
  };
  if (!own(method) || !own(clear)) {
    spdlog::warn("[ForbiddenTechShipName] widget refresh unavailable; native display retained");
    return;
  }
  // Install cleanup first; failure leaves the native panel entirely untouched.
  if (!SPUD_STATIC_DETOUR(clear.Pointer(), ForbiddenTechInventoryItemWidget_ClearWidgetData_Hook)) {
    spdlog::warn("[ForbiddenTechShipName] widget cleanup hook unavailable; native display retained");
    return;
  }
  const bool installed =
      SPUD_STATIC_DETOUR(method.Pointer(), ForbiddenTechInventoryItemWidget_SetWidgetData_Hook) != nullptr;
  spdlog::info("[ForbiddenTechShipName] widget refresh hook {}", installed ? "installed" : "unavailable");
}

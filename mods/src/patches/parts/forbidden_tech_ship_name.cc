#include "config.h"
#include "ship_name_match.h"

#include <il2cpp/il2cpp_checked.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace
{
struct Point {
  float x, y;
};
constexpr float RowHeight = 30.0f;

// All value/byref calls below use runtime invocation: no platform-specific struct-return ABI.
Il2CppObject* Call(Il2CppObject* target, const char* name, int count = 0, void** args = nullptr)
{
  auto* method = target ? IL2CppClassHelper(target->klass).GetMethodInfo(name, count) : nullptr;
  Il2CppObject* result = nullptr;
  if (!method || !Il2CppRuntime::TryInvoke(method, target, args, &result))
    throw std::runtime_error(std::string("Forbidden Tech method unavailable: ") + name);
  return result;
}

template <typename T> T Value(Il2CppObject* boxed, const char* type)
{
  if (!boxed || std::strcmp(boxed->klass->name, type) != 0)
    throw std::runtime_error("Forbidden Tech value contract changed");
  return *static_cast<T*>(il2cpp_object_unbox(boxed));
}

bool Alive(Il2CppObject* object)
{
  if (!object)
    return false;
  static auto helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Object");
  static auto* method = helper.GetMethodInfo("op_Implicit", 1);
  void* args[] = {object};
  Il2CppObject* result = nullptr;
  return Il2CppRuntime::TryInvoke(method, nullptr, args, &result) && Il2CppChecked::Boolean(result);
}

Point GetPoint(Il2CppObject* rect, const char* property)
{ return Value<Point>(Call(rect, property), "Vector2"); }

void SetPoint(Il2CppObject* rect, const char* property, Point value)
{
  void* args[] = {&value};
  Call(rect, property, 1, args);
}

void SetActive(Il2CppObject* component, bool active)
{
  auto* object = Call(component, "get_gameObject");
  void* args[] = {&active};
  Call(object, "SetActive", 1, args);
}

Il2CppObject* Manager()
{
  static auto helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechManager");
  static auto parent = helper.GetParent("MonoSingleton`1");
  static auto* instance = parent.GetMethodInfo("get_Instance", 0);
  Il2CppObject* result = nullptr;
  if (!Il2CppRuntime::TryInvoke(instance, nullptr, nullptr, &result))
    throw std::runtime_error("Forbidden Tech manager unavailable");
  return result;
}

std::string EquippedShipName(Il2CppObject* widget)
{
  auto* context = Call(widget, "get_Context");
  auto* tech = context ? Il2CppChecked::Invoke(context, "get_ForbiddenTech") : nullptr;
  if (!tech)
    return {};
  auto id = Value<int64_t>(Call(tech, "get_Id"), "Int64");
  auto* manager = Manager();
  if (!manager)
    return {};

  // The fleet-index API returns -1 for a ship in the hangar. Resolve the slot's
  // OwnerId instead; it identifies the actual ship regardless of dock assignment.
  Il2CppObject* slot = nullptr;
  void* slot_args[] = {&id, &slot};
  if (!Il2CppChecked::Boolean(Call(manager, "TryGetEntitySlotFromForbiddenTech", 2, slot_args)) || !slot)
    return {};
  auto* params = Il2CppChecked::Invoke(slot, "get_ForbiddenTechSlotParams");
  if (!params)
    return {};
  auto owner = Value<int64_t>(Call(params, "get_OwnerId"), "Int64");

  auto* cache_field = Il2CppChecked::Field(manager, "_fleetServiceCache", "CachedService`1");
  auto* cache = il2cpp_field_get_value_object(cache_field, manager);
  if (!cache || !il2cpp_class_is_valuetype(cache->klass))
    throw std::runtime_error("Forbidden Tech service cache contract changed");
  auto* getter = IL2CppClassHelper(cache->klass).GetMethodInfo("get_Service", 0);
  Il2CppObject* service = nullptr;
  if (!Il2CppRuntime::TryInvoke(getter, il2cpp_object_unbox(cache), nullptr, &service) || !service)
    return {};
  void* ship_args[] = {&owner};
  auto* ship = Call(service, "GetShip", 1, ship_args);
  auto* hull = ship ? Il2CppChecked::Invoke(ship, "get_Hull") : nullptr;
  if (!hull)
    return {};
  // Hull IDs and localisation IDs differ on many ships. Mirror FleetPlayerData.GetLocaId.
  auto* refs = Il2CppChecked::Invoke(hull, "get_IdRefs");
  const auto loca_id = Value<int64_t>(Call(refs ? refs : hull, refs ? "get_LocaId" : "get_Id"), "Int64");
  return ShipNameMatch::LocalizedShipName(loca_id);
}

struct Row {
  Il2CppGCHandle scroller = nullptr;
  Il2CppGCHandle label = nullptr;
  Point original_top{};
  bool visible = false;

  Il2CppObject* Scroller() const { return il2cpp_gchandle_get_target(scroller); }
  Il2CppObject* Label() const { return label ? il2cpp_gchandle_get_target(label) : nullptr; }

  void Hide()
  {
    if (Alive(Label()))
      SetActive(Label(), false);
    if (visible && Alive(Scroller()))
      SetPoint(Scroller(), "set_offsetMax", original_top);
    visible = false;
  }

  void Free()
  {
    if (scroller)
      il2cpp_gchandle_free(scroller);
    if (label)
      il2cpp_gchandle_free(label);
  }
};
std::vector<Row> rows;

void HideWidgetRow(Il2CppObject* widget)
{
  auto* scroller = Il2CppChecked::ReferenceField(widget, "_buffListScroller", "SmartScroller");
  if (!Alive(scroller))
    return;
  auto* rect = Call(scroller, "get_transform");
  for (auto& row : rows) {
    if (row.Scroller() == rect)
      row.Hide();
  }
}

Il2CppObject* CloneLabel(Il2CppObject* source, Il2CppObject* parent)
{
  static auto helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Object");
  // This client exposes the two-argument overload only as Instantiate<T>.
  // Use the non-generic three-argument overload so no generic inflation is required.
  static auto* instantiate = helper.GetMethodInfoSpecial("Instantiate", [](int count, const Il2CppType** types) {
    return count == 3 && types && types[0] && types[1] && types[2]
           && !types[0]->byref && !types[1]->byref && !types[2]->byref
           && il2cpp_class_from_type(types[0])
           && std::strcmp(il2cpp_class_from_type(types[0])->name, "Object") == 0
           && il2cpp_class_from_type(types[1])
           && std::strcmp(il2cpp_class_from_type(types[1])->name, "Transform") == 0
           && types[2]->type == IL2CPP_TYPE_BOOLEAN;
  });
  if (!instantiate || instantiate->is_generic || !(instantiate->flags & METHOD_ATTRIBUTE_STATIC))
    throw std::runtime_error("Forbidden Tech label Instantiate(Object, Transform, Boolean) unavailable");
  bool world_position_stays = false;
  void* args[] = {source, parent, &world_position_stays};
  Il2CppObject* result = nullptr;
  if (!Il2CppRuntime::TryInvoke(instantiate, nullptr, args, &result) || !Alive(result))
    throw std::runtime_error("Forbidden Tech ship label could not be created");
  return result;
}

void UpdateRow(Il2CppObject* widget)
{
  // Only the large selected-item panel: inventory tiles retain their native dock badges.
  auto* type_field = Il2CppChecked::Field(widget, "_widgetType", "WidgetType");
  int32_t type = 0;
  il2cpp_field_get_value(widget, type_field, &type);
  if (type != 1)
    return;

  auto* scroller = Il2CppChecked::ReferenceField(widget, "_buffListScroller", "SmartScroller");
  if (!Alive(scroller))
    return;
  auto* rect = Call(scroller, "get_transform");
  if (!rect || std::strcmp(rect->klass->name, "RectTransform") != 0)
    throw std::runtime_error("Forbidden Tech buff layout unavailable");

  std::erase_if(rows, [](Row& row) {
    if (Alive(row.Scroller()))
      return false;
    row.Free();
    return true;
  });
  auto found = std::find_if(rows.begin(), rows.end(), [rect](const Row& row) { return row.Scroller() == rect; });
  if (found == rows.end()) {
    Row row;
    row.original_top = GetPoint(rect, "get_offsetMax");
    row.scroller = il2cpp_gchandle_new_weakref(rect, false);
    if (!row.scroller)
      return;
    rows.push_back(row);
    found = std::prev(rows.end());
  }
  auto& row = *found;
  // Clear before resolving a new assignment so errors/unassigned tech cannot retain an old ship name.
  row.Hide();
  // Native refresh may have resized the panel since the previous selection.
  row.original_top = GetPoint(rect, "get_offsetMax");
  if (!Config::Get().forbidden_tech_ship_name)
    return;
  const auto name = EquippedShipName(widget);
  if (name.empty())
    return;

  try {
    auto* label = row.Label();
    if (!Alive(label)) {
      auto* source = Il2CppChecked::ReferenceField(widget, "_name", "TextLocalizer");
      if (!Alive(source))
        return;
      label = CloneLabel(source, Call(rect, "get_parent"));
      SetActive(label, false);
      if (row.label)
        il2cpp_gchandle_free(row.label);
      row.label = il2cpp_gchandle_new_weakref(label, false);
      if (!row.label)
        throw std::runtime_error("Forbidden Tech label handle unavailable");
    }
    auto* label_rect = Call(label, "get_transform");
    const auto min = GetPoint(rect, "get_offsetMin");
    const auto max = row.original_top;
    const auto anchors = GetPoint(rect, "get_anchorMax");
    auto anchor_min = GetPoint(rect, "get_anchorMin");
    anchor_min.y = anchors.y;
    SetPoint(label_rect, "set_anchorMin", anchor_min);
    SetPoint(label_rect, "set_anchorMax", anchors);
    SetPoint(label_rect, "set_pivot", {0.5f, 1.0f});
    SetPoint(label_rect, "set_offsetMin", {min.x, max.y - RowHeight});
    SetPoint(label_rect, "set_offsetMax", max);

    // Target resolves the graphic lazily, even when the source label was inactive at clone time.
    auto* text = Il2CppChecked::Invoke(label, "get_Target");
    static auto tmp = il2cpp_get_class_helper("Unity.TextMeshPro", "TMPro", "TMP_Text");
    if (!text || !tmp.get_cls() || !il2cpp_class_is_assignable_from(tmp.get_cls(), text->klass))
      throw std::runtime_error("Forbidden Tech ship label text unavailable");
    int32_t alignment = 513; // TMP TextAlignmentOptions.Left (left, vertically centred).
    bool raycast = false;
    bool rich_text = false; // Ship names are plain text, never markup.
    bool wrap = false;
    bool auto_size = false;
    float font_size = 20.0f;
    struct { float r, g, b, a; } color{1.0f, 1.0f, 1.0f, 1.0f};
    int32_t overflow = 1; // TMP TextOverflowModes.Ellipsis; long names stay within this row.
    void* align_args[] = {&alignment};
    void* raycast_args[] = {&raycast};
    void* rich_args[] = {&rich_text};
    void* wrap_args[] = {&wrap};
    void* overflow_args[] = {&overflow};
    void* auto_size_args[] = {&auto_size};
    void* font_args[] = {&font_size};
    void* color_args[] = {&color};
    Call(text, "set_alignment", 1, align_args);
    Call(text, "set_raycastTarget", 1, raycast_args);
    Call(text, "set_richText", 1, rich_args);
    Call(text, "set_enableWordWrapping", 1, wrap_args);
    Call(text, "set_overflowMode", 1, overflow_args);
    Call(text, "set_enableAutoSizing", 1, auto_size_args);
    Call(text, "set_fontSize", 1, font_args);
    Call(text, "set_color", 1, color_args);
    auto* value = il2cpp_string_new(("Equipped on: " + name).c_str());
    void* text_args[] = {value};
    Call(label, "OverrideLocalizedText", 1, text_args);
    // Keep the panel's outer size, reserving one row from the scrollable buff area.
    SetPoint(rect, "set_offsetMax", {max.x, max.y - RowHeight});
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

void SetWidgetData_Hook(auto original, Il2CppObject* widget)
{
  try {
    HideWidgetRow(widget);
  } catch (const std::exception&) {
  }
  original(widget);
  try {
    UpdateRow(widget);
  } catch (const std::exception& error) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      spdlog::warn("[ForbiddenTechShipName] native display retained: {}", error.what());
    }
  }
}

void ClearWidgetData_Hook(auto original, Il2CppObject* widget)
{
  try {
    HideWidgetRow(widget);
  } catch (const std::exception&) {
  }
  original(widget);
}
} // namespace

void InstallForbiddenTechShipNameHooks()
{
  auto helper = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.ForbiddenTechs", "ForbiddenTechInventoryItemWidget");
  auto* method = helper.GetMethodInfo("SetWidgetData", 0);
  auto* clear = helper.GetMethodInfo("ClearWidgetData", 0);
  // Both methods perform substantive native UI updates, rather than forwarding through tiny wrappers.
  const auto valid = [&helper](const MethodInfo* info) {
    return info && info->klass == helper.get_cls() && info->methodPointer && !info->is_generic
           && !info->is_inflated && !(info->flags & METHOD_ATTRIBUTE_STATIC) && info->parameters_count == 0
           && info->return_type && info->return_type->type == IL2CPP_TYPE_VOID;
  };
  if (!valid(method) || !valid(clear)) {
    spdlog::warn("[ForbiddenTechShipName] widget refresh unavailable; native display retained");
    return;
  }
  // Install cleanup first; failure leaves the native panel entirely untouched.
  if (!SPUD_STATIC_DETOUR(clear->methodPointer, ClearWidgetData_Hook)) {
    spdlog::warn("[ForbiddenTechShipName] widget cleanup hook unavailable; native display retained");
    return;
  }
  const bool installed = SPUD_STATIC_DETOUR(method->methodPointer, SetWidgetData_Hook) != nullptr;
  spdlog::info("[ForbiddenTechShipName] widget refresh hook {}", installed ? "installed" : "unavailable");
}

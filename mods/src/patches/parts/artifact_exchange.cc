#include "config.h"

#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>
#include <il2cpp/runtime.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace
{
FieldInfo* convert_all_field = nullptr;
const MethodInfo* get_game_object = nullptr;
const MethodInfo* set_active = nullptr;

void InventoryUsePopup_Bind_Hook(auto original, void* controller)
{
  original(controller);
  if (!Config::Get().disable_exchange_all || !controller)
    return;
  Il2CppObject* button = nullptr;
  il2cpp_field_get_value(static_cast<Il2CppObject*>(controller), convert_all_field, &button);
  Il2CppObject* object = nullptr;
  if (!button || !Il2CppRuntime::TryInvoke(get_game_object, button, nullptr, &object) || !object)
    return;
  bool active = false;
  void* args[]{&active};
  Il2CppRuntime::TryInvoke(set_active, object, args);
}
} // namespace

void InstallArtifactExchangeHooks()
{
  // This dedicated field belongs to artifact bulk conversion, not the inventory
  // entry button or individual exchanges. Preserve native binding first.
  auto controller =
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Inventories", "InventoryUsePopupViewController");
  auto component = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Component");
  auto game_object = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "GameObject");
  auto* cls = controller.get_cls();
  auto* field = cls ? il2cpp_class_get_field_from_name(cls, "_convertAllButton") : nullptr;
  auto* field_class = field && field->type ? il2cpp_class_from_type(field->type) : nullptr;
  const auto* bind = method_contract::Resolve(cls, "OnDidBindCanvasContext", false, "System.Void", {});
  get_game_object = method_contract::Resolve(component.get_cls(), "get_gameObject", false, "UnityEngine.GameObject", {});
  set_active = method_contract::Resolve(game_object.get_cls(), "SetActive", false, "System.Void", {"System.Boolean"});
  if (!cls || !component.get_cls() || !game_object.get_cls() || !field || !field->type || field->type->byref
      || (il2cpp_field_get_flags(field) & FIELD_ATTRIBUTE_STATIC) || !field_class
      || !il2cpp_class_is_assignable_from(component.get_cls(), field_class)
      || !bind || !get_game_object || !set_active
      || bind->has_full_generic_sharing_signature || get_game_object->has_full_generic_sharing_signature
      || set_active->has_full_generic_sharing_signature) {
    spdlog::warn("[ArtifactExchange] required popup API unavailable; leaving game unchanged");
    return;
  }
  convert_all_field = field;
  if (SPUD_STATIC_DETOUR(bind->methodPointer, InventoryUsePopup_Bind_Hook))
    spdlog::info("[ArtifactExchange] installed popup bind hook");
  else
    spdlog::warn("[ArtifactExchange] popup bind hook was not installed");
}

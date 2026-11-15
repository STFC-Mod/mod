#include "config.h"
#include "errormsg.h"

#include "patches/instant_warp_policy.h"
#include "patches/screen_update_hook.h"
#include "prime/DeploymentManager.h"
#include "prime/FleetsManager.h"
#include "prime/StarNodeObjectViewerWidget.h"
#include "str_utils.h"
#include <il2cpp/il2cpp_checked.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>

namespace
{
// This is a client of the existing frame dispatcher, not another UI detour.
// Never mutate the button context, timer, resource cost, or click handler.
bool Alive(Il2CppObject* object)
{
  if (!object)
    return false;
  static auto  helper = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Object");
  static auto* method = helper.GetMethodInfo("op_Implicit", 1);
  if (!method)
    return false;
  void*            args[]    = {object};
  Il2CppException* exception = nullptr;
  auto*            result    = il2cpp_runtime_invoke(method, nullptr, args, &exception);
  return !exception && Il2CppChecked::Boolean(result);
}

std::string CurrentText(Il2CppObject* label)
{
  auto* text = Il2CppChecked::Invoke(label, "CurrentText");
  if (!text || il2cpp_class_get_type(text->klass)->type != IL2CPP_TYPE_STRING)
    throw std::runtime_error("expected label text");
  return to_string(reinterpret_cast<Il2CppString*>(text));
}

struct OwnedLabel {
  Il2CppGCHandle handle = nullptr;
  std::string    text;

  Il2CppObject* Get() const
  { return handle ? il2cpp_gchandle_get_target(handle) : nullptr; }

  void Clear()
  {
    auto* label    = Get();
    auto  previous = std::move(text);
    if (handle)
      il2cpp_gchandle_free(handle);
    handle = nullptr;
    text.clear();
    // Forget ownership even if native cleanup fails.
    if (Alive(label) && Il2CppChecked::BooleanField(label, "_textOverride") && CurrentText(label) == previous)
      Il2CppChecked::Invoke(label, "ClearTextOverride");
  }

  void Apply(Il2CppObject* label, const char* desired)
  {
    if (!label || !desired) {
      Clear();
      return;
    }
    if (Get() != label) {
      Clear();
      // Don't take ownership from an existing text override.
      if (Il2CppChecked::BooleanField(label, "_textOverride"))
        return;
      handle = il2cpp_gchandle_new_weakref(label, false);
      if (!handle)
        return;
    }
    const auto current = CurrentText(label);
    if (!text.empty() && Il2CppChecked::BooleanField(label, "_textOverride") && current != text) {
      Clear();
      return;
    }
    if (text == desired && Il2CppChecked::BooleanField(label, "_textOverride") && current == desired)
      return;
    auto* value = il2cpp_string_new(desired);
    if (!value)
      return;
    void* args[] = {value};
    Il2CppChecked::Invoke(label, "OverrideLocalizedText", 1, args);
    text = desired;
  }
};
OwnedLabel action_label;

void Update()
{
  static auto next = std::chrono::steady_clock::time_point{};
  const auto  now  = std::chrono::steady_clock::now();
  if (now < next)
    return;
  next = now + std::chrono::milliseconds(100);
  try {
    auto*      widget = reinterpret_cast<Il2CppObject*>(ObjectFinder<StarNodeObjectViewerWidget>::Get());
    const bool active =
        Alive(widget) && Il2CppChecked::Boolean(Il2CppChecked::Invoke(widget, "get_isActiveAndEnabled"));
    auto*      button = active ? Il2CppChecked::ReferenceField(widget, "_visitButton", "GenericButtonWidget") : nullptr;
    const bool enabled  = Alive(button)
                          && Il2CppChecked::Boolean(Il2CppChecked::Invoke(button, "get_isActiveAndEnabled"))
                          && Il2CppChecked::Boolean(Il2CppChecked::Invoke(button, "get_Interactable"));
    const char* desired = nullptr;
    // The ordinary visit path uses the currently selected fleet. The card's
    // cached popup CourseData is populated on click, so it is not a preview source.
    // Toll/wormhole buttons have separate confirmation paths and retain native text.
    if (enabled) {
      auto* fleets     = reinterpret_cast<Il2CppObject*>(FleetsManager::Instance());
      auto* deployment = reinterpret_cast<Il2CppObject*>(DeploymentManger::Instance());
      auto* fleet      = Alive(fleets) ? Il2CppChecked::Invoke(fleets, "GetSelectedFleetData") : nullptr;
      void* args[]     = {fleet};
      if (fleet && Alive(deployment)
          && Il2CppChecked::Boolean(Il2CppChecked::Invoke(deployment, "HasInstantWarpAbility", 1, args))) {
        switch (ResolveInstantWarpConfirmation(reinterpret_cast<FleetPlayerData*>(fleet))) {
          case InstantWarpConfirmation::Warp:
            desired = "WARP";
            break;
          case InstantWarpConfirmation::Jump:
            desired = "JUMP";
            break;
          case InstantWarpConfirmation::None:
            break;
        }
      }
    }
    auto* label = desired ? Il2CppChecked::ReferenceField(button, "_mainText", "TextLocalizer") : nullptr;
    action_label.Apply(Alive(label) ? label : nullptr, desired);
  } catch (const std::exception& error) {
    static bool warned = false;
    if (!warned) {
      warned = true;
      spdlog::warn("[WarpActionLabel] native label unavailable: {}", error.what());
    }
    try {
      action_label.Clear();
    } catch (...) {
    }
  }
}
} // namespace

void InstallWarpActionLabel()
{
  if (install_screen_manager_update_hook() && register_screen_manager_update_callback(Update))
    spdlog::info("[WarpActionLabel] system-card action labels enabled");
}

#include "config.h"
#include "patches/object_tracker.h"

#include "patches/instant_warp_policy.h"
#include "patches/screen_update_hook.h"
#include "prime/DeploymentManager.h"
#include "prime/FleetsManager.h"
#include "prime/StarNodeObjectViewerWidget.h"
#include "prime/TextLocalizer.h"
#include "str_utils.h"
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>

namespace
{
// This is a client of the existing frame dispatcher, not another UI detour.
// Never mutate the button context, timer, resource cost, or click handler.
// Unity's `bool(UnityEngine.Object)`: false for null and for destroyed native objects.
bool Alive(il2cpp::Object* object)
{
  using UnityObject = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Object">;
  return object && UnityObject::Call<"op_Implicit", bool>(object);
}

std::string CurrentText(TextLocalizer* label)
{
  auto* text = label->CurrentText();
  if (!text)
    throw std::runtime_error("expected label text");
  return to_string(text);
}

struct OwnedLabel {
  il2cpp::GcHandle handle;
  std::string      text;

  TextLocalizer* Get() const
  { return handle.Get<TextLocalizer>(); }

  void Clear()
  {
    auto* label    = Get();
    auto  previous = std::move(text);
    handle.Reset();
    text.clear();
    // Forget ownership even if native cleanup fails.
    if (Alive(label) && label->textOverride() && CurrentText(label) == previous)
      label->ClearTextOverride();
  }

  void Apply(TextLocalizer* label, const char* desired)
  {
    if (!label || !desired) {
      Clear();
      return;
    }
    if (Get() != label) {
      Clear();
      // Don't take ownership from an existing text override.
      if (label->textOverride())
        return;
      handle = il2cpp::GcHandle::Weak(label);
      if (!handle)
        return;
    }
    const auto current = CurrentText(label);
    if (!text.empty() && label->textOverride() && current != text) {
      Clear();
      return;
    }
    if (text == desired && label->textOverride() && current == desired)
      return;
    label->OverrideLocalizedText(il2cpp_string_new(desired));
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
    auto*       widget  = ObjectFinder<StarNodeObjectViewerWidget>::Get();
    const bool  active  = Alive(widget) && widget->isActiveAndEnabled();
    auto*       button  = active ? widget->visitButton() : nullptr;
    const bool  enabled = Alive(button) && button->isActiveAndEnabled() && button->Interactable();
    const char* desired = nullptr;
    // The ordinary visit path uses the currently selected fleet. The card's
    // cached popup CourseData is populated on click, so it is not a preview source.
    // Toll/wormhole buttons have separate confirmation paths and retain native text.
    if (enabled) {
      auto* fleets     = FleetsManager::Instance();
      auto* deployment = DeploymentManger::Instance();
      auto* fleet      = Alive(fleets) ? fleets->GetSelectedFleetData() : nullptr;
      if (fleet && Alive(deployment) && deployment->HasInstantWarpAbility(fleet)) {
        switch (ResolveInstantWarpConfirmation(fleet)) {
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
    auto* label = desired ? button->mainText() : nullptr;
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

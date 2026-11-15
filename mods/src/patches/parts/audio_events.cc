#include "config.h"
#include "errormsg.h"
#include "str_utils.h"

#include <il2cpp/il2cpp_helper.h>
#include <il2cpp/method_contract.h>

#include <spud/detour.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

struct FabricEventManager {
};

namespace
{
enum class FabricEventAction : int {
  PlaySound       = 0,
  StopSound       = 1,
  UnpauseSound    = 3,
  AdvanceSequence = 16,
  StopAll         = 21,
  UnloadAudio     = 23,
  PlayScheduled   = 33,
};

constexpr bool can_start_audio(int event_action)
{
  switch (static_cast<FabricEventAction>(event_action)) {
    case FabricEventAction::PlaySound:
    case FabricEventAction::UnpauseSound:
    case FabricEventAction::AdvanceSequence:
    case FabricEventAction::PlayScheduled:
      return true;
    default:
      return false;
  }
}

constexpr bool should_suppress_audio_event(bool disable_all, bool event_is_disabled, int event_action)
{ return disable_all ? can_start_audio(event_action) : event_is_disabled; }

static_assert(should_suppress_audio_event(true, false, static_cast<int>(FabricEventAction::PlaySound)));
static_assert(!should_suppress_audio_event(true, false, static_cast<int>(FabricEventAction::StopSound)));
static_assert(should_suppress_audio_event(false, true, static_cast<int>(FabricEventAction::StopAll)));
static_assert(!should_suppress_audio_event(true, true, static_cast<int>(FabricEventAction::UnloadAudio)));
} // namespace

// Fabric.EventManager.PostEvent(string, EventAction, object, GameObject,
// InitialiseParameters, bool, OnEventNotify)
bool FabricEventManager_PostEvent_Hook(auto original, FabricEventManager* _this, Il2CppString* event_name,
                                       int event_action, Il2CppObject* parameter, Il2CppObject* parent_game_object,
                                       Il2CppObject* initialise_parameters, bool add_to_queue,
                                       Il2CppObject* on_event_notify)
{
  auto& config = Config::Get();
  if (!event_name || (!config.trace_audio_events && !config.disable_all_audio_events
                     && config.disabled_audio_events.empty())) {
    return original(_this, event_name, event_action, parameter, parent_game_object, initialise_parameters,
                    add_to_queue, on_event_notify);
  }

  try {
    const auto event = to_string(event_name);
    if (config.trace_audio_events)
      spdlog::info("Audio event: {}", event);
    const bool event_is_disabled =
        std::ranges::find(config.disabled_audio_events, event) != config.disabled_audio_events.end();
    if (should_suppress_audio_event(config.disable_all_audio_events, event_is_disabled, event_action)) {
      spdlog::debug("Suppressed audio event: {}", event);
      return false;
    }
  } catch (...) {
    // Filter processing failure preserves the native event path.
  }
  return original(_this, event_name, event_action, parameter, parent_game_object, initialise_parameters,
                  add_to_queue, on_event_notify);
}

void InstallAudioEventHooks()
{
  auto helper = il2cpp_get_class_helper("Fabric.Core", "Fabric", "EventManager");
  if (!helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Fabric", "EventManager");
    return;
  }

  // String overloads funnel here. Require the complete instance ABI to avoid
  // the integer-ID overload and incompatible future signatures.
  const auto* method = method_contract::Resolve(helper.get_cls(), "PostEvent", false, "System.Boolean",
      {"System.String", "Fabric.EventAction", "System.Object", "UnityEngine.GameObject",
       "Fabric.InitialiseParameters", "System.Boolean", "Fabric.OnEventNotify"});
  if (!method || method->has_full_generic_sharing_signature) {
    ErrorMsg::MissingMethod("EventManager", "PostEvent(string, EventAction, object, GameObject, InitialiseParameters, bool, OnEventNotify)");
    return;
  }
  for (const auto index : {0, 2, 3, 4, 6}) {
    auto* cls = il2cpp_class_from_type(method->parameters[index]);
    if (!cls || il2cpp_class_is_valuetype(cls)) {
      spdlog::error("Fabric PostEvent reference argument is incompatible");
      return;
    }
  }
  auto* action = il2cpp_class_from_type(method->parameters[1]);
  const auto* action_type = action && il2cpp_class_is_enum(action) ? il2cpp_class_enum_basetype(action) : nullptr;
  if (!action_type || action_type->byref || action_type->type != IL2CPP_TYPE_I4) {
    spdlog::error("Fabric PostEvent action enum is incompatible");
    return;
  }
  if (!SPUD_STATIC_DETOUR(method->methodPointer, FabricEventManager_PostEvent_Hook))
    spdlog::error("Fabric named audio event hook was not installed");
}

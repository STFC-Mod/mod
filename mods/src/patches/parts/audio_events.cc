// [audio]: logs the game's sound events by name, and mutes the ones in disabled_events.

#include "config.h"
#include "patches/patches.h"
#include "str_utils.h"

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <algorithm>
#include <string>

namespace
{
struct FabricEventManager;

// Fabric.EventManager.PostEvent(string, EventAction, object, GameObject,
// InitialiseParameters, bool, OnEventNotify)
bool FabricEventManager_PostEvent_Hook(auto original, FabricEventManager* _this, Il2CppString* event_name,
                                       int event_action, Il2CppObject* parameter, Il2CppObject* parent_game_object,
                                       Il2CppObject* initialise_parameters, bool add_to_queue,
                                       Il2CppObject* on_event_notify)
{
  if (event_name) {
    const auto& audio = Config::Get().audio;
    const auto  event = to_string(event_name);
    if (audio.trace_events)
      spdlog::info("Audio event: {}", event);
    if ((audio.disable_all_events ? event_action == 0 || event_action == 3 || event_action == 16 || event_action == 33 : std::ranges::find(audio.disabled_events, event) != audio.disabled_events.end())) {
      spdlog::debug("Suppressed audio event: {}", event);
      return false;
    }
  }
  return original(_this, event_name, event_action, parameter, parent_game_object, initialise_parameters, add_to_queue,
                  on_event_notify);
}
} // namespace

void InstallAudioEventHooks()
{
  using EventManager = il2cpp::Class<"Fabric.Core", "Fabric", "EventManager">;

  // The shorter PostEvent overloads funnel into this seven-argument overload.
  if (auto method = EventManager::Method("PostEvent", 7))
    SPUD_STATIC_DETOUR(method.Pointer(), FabricEventManager_PostEvent_Hook);
}

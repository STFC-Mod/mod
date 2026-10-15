// [audio]: logs the game's sound events by name, and mutes the ones in disabled_events.

#include "config.h"
#include "patches/patches.h"
#include "str_utils.h"

#include "prime/Hub.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

namespace
{
// Fabric.EventManager.PostEvent(string, EventAction, object, GameObject, InitialiseParameters, bool, OnEventNotify)
bool AudioEventManager_PostEvent_Hook(auto original, Il2CppObject* _this, Il2CppString* event_name, int event_action,
                                      Il2CppObject* parameter, Il2CppObject* parent_game_object,
                                      Il2CppObject* initialise_parameters, bool add_to_queue,
                                      Il2CppObject* on_event_notify)
{
  if (const auto& audio = Config::Get().audio; event_name && (audio.trace_events || audio.disable_all_events || !audio.disabled_events.empty())) {
    const auto event = il2cpp::ToUtf8(event_name);
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

void InstallAudio()
{ IL2CPP_HOOK(AudioEventManager::Method("PostEvent", 7), AudioEventManager_PostEvent_Hook); }
} // namespace

const Feature kAudio{"Audio", &Config::Patches::audioeventhooks, InstallAudio};

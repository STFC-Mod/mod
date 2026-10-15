// [audio]: logs the game's sound events by name, and mutes the ones in disabled_events.

#include "config.h"
#include "features/features.h"
#include "text.h"

#include "game/app.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

namespace
{
// PlaySound=0, UnpauseSound=3, AdvanceSequence=16, PlayScheduled=33. Cleanup still runs with All.
constexpr bool SuppressEvent(bool all, bool named, int32_t action)
{ return all ? action == 0 || action == 3 || action == 16 || action == 33 : named; }
static_assert(SuppressEvent(true, false, 0));
static_assert(!SuppressEvent(true, true, 1));
static_assert(!SuppressEvent(true, true, 23));
static_assert(SuppressEvent(false, true, 21));

// Fabric.EventManager.PostEvent(string, EventAction, object, GameObject, InitialiseParameters, bool, OnEventNotify)
bool AudioEventManager_PostEvent_Hook(auto original, Il2CppObject* _this, Il2CppString* event_name, int event_action,
                                      Il2CppObject* parameter, Il2CppObject* parent_game_object,
                                      Il2CppObject* initialise_parameters, bool add_to_queue,
                                      Il2CppObject* on_event_notify)
{
  if (const auto& audio = Config::Get().audio;
      event_name && (audio.trace_events || audio.disable_all_events || !audio.disabled_events.empty())) {
    try {
      const auto event = il2cpp::ToUtf8(event_name);
      if (audio.trace_events)
        spdlog::info("Audio event: {}", event);
      if (SuppressEvent(audio.disable_all_events,
                        std::ranges::find(audio.disabled_events, event) != audio.disabled_events.end(), event_action)) {
        spdlog::debug("Suppressed audio event: {}", event);
        return false;
      }
    } catch (...) {
      // A filter failure must leave the native event path available.
    }
  }
  return original(_this, event_name, event_action, parameter, parent_game_object, initialise_parameters, add_to_queue,
                  on_event_notify);
}

void InstallAudio()
{
  IL2CPP_HOOK(AudioEventManager::Method<bool(Il2CppString*, int32_t, Il2CppObject*, Il2CppObject*, Il2CppObject*, bool,
                                             Il2CppObject*)>("PostEvent"),
              AudioEventManager_PostEvent_Hook);
}
} // namespace

const Feature kAudio{"Audio", &Config::Patches::audioeventhooks, InstallAudio};

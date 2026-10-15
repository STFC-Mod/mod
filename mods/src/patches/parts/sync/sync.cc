// Data sync: watches the game's server responses and realtime updates and hands them to the processing thread,
// which turns them into records for the sync targets (records.cc), uploaded by upload.cc. It also captures the game's
// session for the battle-log requests.

#include "patches/parts/sync/sync.h"

#include "patches/patches.h"
#include "str_utils.h"

#include "prime/Hub.h"
#include "prime/server.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>

#include <thread>

namespace data_sync
{
namespace
{
  // Responses waiting for the processing thread. A login delivers a few dozen at once; 256 leaves room for a
  // slow backlog without holding unbounded copies of large inventory payloads.
  constexpr size_t kJobQueueCapacity = 256;

  struct Job {
    Processor process;
    Input     input;
  };

  auto& pending = *new BoundedQueue<Job>{"processing", kJobQueueCapacity};

  // One thread processes every response in arrival order, so a delta is never applied before the full state it
  // updates.
  void RunJobs()
  {
    for (;;) {
      auto job = pending.Pop();
      try {
        if (job.input.generation == Session().generation)
          job.process(job.input);
      } catch (const std::exception& e) {
        spdlog::error("Exception while processing sync data: {}", e.what());
      } catch (...) {
        spdlog::error("Unknown exception while processing sync data");
      }
    }
  }

  // The game's record of the local player, or nullptr.
  UserProfile* LocalUserProfile()
  {
    auto* manager = UserProfileManager::Instance();
    return manager ? manager->GetLocalUserProfile() : nullptr;
  }

  // Runs on the game thread: copies the bytes out and leaves the work to the processing thread.
  void HandleEntityGroup(EntityGroup* entity_group)
  {
    auto* group = entity_group ? entity_group->Group() : nullptr;
    if (!group)
      return;
    const auto length = group->Length();
    auto*      bytes  = group->bytes();
    if (length < 0 || (length > 0 && (!bytes || static_cast<size_t>(length) > bytes->size())))
      return;
    const auto type    = entity_group->Type();
    auto       process = ProcessorFor(type);
    // UserProfiles also carries the local player's own record, read only while a target syncs the profile.
    const bool own_profile = type == EntityGroup::Kind::UserProfiles && Config::Get().sync.options.profile;
    if (!process && !own_profile)
      return;
    Input input{Session().generation,
                length ? std::string(reinterpret_cast<const char*>(bytes->data()), static_cast<size_t>(length))
                       : std::string{}};
    if (type == EntityGroup::Kind::PlanetaryMapResponse || own_profile) {
      if (auto* profile = LocalUserProfile()) {
        input.user_id = il2cpp::ToUtf8(profile->UserId());
        input.level   = profile->Level();
      }
    }
    if (process)
      pending.Push({process, input});
    // Skipped if the local profile is not ready; a later response retries.
    if (own_profile && !input.user_id.empty())
      pending.Push({ProcessOwnProfile, std::move(input)});
  }

  // Platform responses (store offers among them) pass through the same registry code as game server responses. Only
  // store offers are read from them, and only while a target wants them, so a target that turns them on later
  // waits for the next store refresh.
  void HandlePlatformResponse(PlatformServiceResponse* response)
  {
    auto* groups = response->EntityGroups();
    if (!groups)
      return;
    for (auto* entity_group : *groups) {
      auto*      group  = entity_group && entity_group->Type() == PlatformEntityGroup::Kind::StoreOffers
                              ? entity_group->Group()
                              : nullptr;
      const auto length = group ? group->Length() : 0;
      auto*      bytes  = group ? group->bytes() : nullptr;
      if (length > 0 && bytes && static_cast<size_t>(length) <= bytes->size())
        pending.Push({ProcessStoreOffers,
                      {Session().generation,
                       std::string(reinterpret_cast<const char*>(bytes->data()), static_cast<size_t>(length))}});
    }
  }

  void* RtcParser_ParseFinalPayload_Hook(auto original, void* _this, CentrifugoInfo centrifugoInfo,
                                         RealtimeDataPayload* realtimeDataPayload, void* finalPayload)
  {
    if (realtimeDataPayload && realtimeDataPayload->Target() && realtimeDataPayload->DataType()
        && realtimeDataPayload->Data()) {
      // DataType arrives as a number in a string; JSON is "0". This runs inside the game's parser, so nothing here
      // may throw.
      const auto* type = realtimeDataPayload->DataType();
      if (type->length == 1 && type->chars[0] == u'0')
        pending.Push({ProcessRealtimeSlot, {Session().generation, il2cpp::ToUtf8(realtimeDataPayload->Data())}});
    }
    return original(_this, centrifugoInfo, realtimeDataPayload, finalPayload);
  }

  void GameServerModelRegistry_ProcessResultInternal_Hook(auto original, void* _this, void* parsing_context,
                                                          ServiceResponse* service_response, MethodInfo* method)
  {
    // The hooked method is shared generic code; PlatformModelRegistry passes its own response type.
    if (auto* groups =
            service_response && service_response->Is<ServiceResponse>() ? service_response->EntityGroups() : nullptr)
      for (auto* group : *groups)
        HandleEntityGroup(group);
    else if (auto* platform = service_response && Config::Get().sync.options.store_offers
                                  ? service_response->As<PlatformServiceResponse>()
                                  : nullptr)
      HandlePlatformResponse(platform);
    return original(_this, parsing_context, service_response, method);
  }

  void PrimeApp_InitPrimeServer_Hook(auto original, void* _this, Il2CppString* gameServerUrl,
                                     Il2CppString* gatewayServerUrl, Il2CppString* sessionId,
                                     Il2CppString* serverRegion)
  {
    // Establish the generation before original can deliver responses from the new session.
    SetGameServer(il2cpp::ToUtf8(gameServerUrl), il2cpp::ToUtf8(sessionId));
    original(_this, gameServerUrl, gatewayServerUrl, sessionId, serverRegion);
  }

  void GameServer_Initialise_Hook(auto original, void* _this, Il2CppString* sessionId, Il2CppString* gameVersion,
                                  bool encryptRequests, Il2CppString* serverRegion)
  {
    original(_this, sessionId, gameVersion, encryptRequests, serverRegion);
    SetGameVersion(il2cpp::ToUtf8(gameVersion));
  }

  void GameServer_SetInstanceIdHeader_Hook(auto original, void* _this, int32_t instanceId)
  {
    original(_this, instanceId);
    SetInstanceId(instanceId);
  }

  void Install()
  {
    IL2CPP_HOOK(GameServerModelRegistry::Method("ProcessResultInternal"),
                GameServerModelRegistry_ProcessResultInternal_Hook);
    IL2CPP_HOOK(PrimeApp::Class::Method("InitPrimeServer"), PrimeApp_InitPrimeServer_Hook);

    // This hook only captures the game version for the X-PRIME-VERSION header, which has a fallback. It can be
    // turned off for users whose other mods detour the same method: a native and a managed detour on one prologue
    // corrupt each other and crash at login.
    if (!Config::Get().patches.game_version)
      spdlog::info("Sync: skipping GameServer::Initialise hook (patches.game_version = false)");
    else
      IL2CPP_HOOK(GameServer::Method("Initialise"), GameServer_Initialise_Hook);

    IL2CPP_HOOK(GameServer::Method("SetInstanceIdHeader"), GameServer_SetInstanceIdHeader_Hook);

    // Each parser overrides ParseFinalPayload, so these are two distinct native methods.
    IL2CPP_HOOK(SlotAssignRtcParser::Method("ParseFinalPayload"), RtcParser_ParseFinalPayload_Hook);
    IL2CPP_HOOK(SlotClearRtcParser::Method("ParseFinalPayload"), RtcParser_ParseFinalPayload_Hook);

    FollowSettings();
    InstallFleetSync();
    StartBattleLogs();
    std::thread(RunJobs).detach();
  }

  // Frame work: settings changes reach the targets, then fleet state is polled.
  void Frame()
  {
    FollowSettings();
    UpdateFleetSync();
  }
} // namespace
} // namespace data_sync

const Feature kSync{"Sync", &Config::Patches::syncpatches, data_sync::Install, data_sync::Frame};

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

#include <functional>
#include <thread>

namespace data_sync
{
namespace
{
  // Responses waiting for the processing thread. A login delivers a few dozen at once; 256 leaves room for a
  // slow backlog without holding unbounded copies of large inventory payloads.
  constexpr size_t kJobQueueCapacity = 256;

  struct Job {
    // A plain processor, or one bound to what the game thread knew when the response arrived.
    std::function<void(const std::string& bytes)> process;
    std::string                                   bytes;
  };

  auto& pending = *new BoundedQueue<Job>{"processing", kJobQueueCapacity};

  // One thread processes every response in arrival order, so a delta is never applied before the full state it
  // updates.
  void RunJobs()
  {
    for (;;) {
      auto job = pending.Pop();
      try {
        job.process(job.bytes);
      } catch (const std::exception& e) {
        spdlog::error("Exception while processing sync data: {}", e.what());
      } catch (...) {
        spdlog::error("Unknown exception while processing sync data");
      }
    }
  }

  // Runs on the game thread: copies the bytes out and leaves the work to the processing thread.
  void HandleEntityGroup(EntityGroup* entity_group)
  {
    auto* group = entity_group ? entity_group->Group() : nullptr;
    if (!group || !group->bytes() || group->Length() <= 0)
      return;
    const auto type  = entity_group->Type();
    const auto bytes = [group] {
      return std::string(reinterpret_cast<const char*>(group->bytes()->data()), static_cast<size_t>(group->Length()));
    };
    if (auto process = ProcessorFor(type))
      pending.Push({process, bytes()});

    // The own profile needs the local player's id and level, which only the game thread can read. Skipped if the
    // local profile is not ready; a later response retries.
    if (type == EntityGroup::Kind::UserProfiles && Config::Get().sync.options.profile) {
      auto* local   = LocalUserProfile();
      auto* user_id = local ? local->UserId() : nullptr;
      if (auto self = user_id ? il2cpp::ToUtf8(user_id) : std::string{}; !self.empty())
        pending.Push({[self = std::move(self), level = local->Level()](const std::string& bytes) {
                        ProcessOwnProfile(bytes, self, level);
                      },
                      bytes()});
    }
  }

  // Platform responses (store offers among them) pass through the same registry code as game server responses. Only
  // store offers are read from them, and only while a target wants them.
  void HandlePlatformResponse(PlatformServiceResponse* response)
  {
    if (auto* groups = response->EntityGroups())
      for (auto* entity_group : *groups) {
        auto* group = entity_group && entity_group->Type() == PlatformEntityGroup::Kind::StoreOffers
                          ? entity_group->Group()
                          : nullptr;
        if (group && group->bytes() && group->Length() > 0)
          pending.Push({ProcessStoreOffers, std::string(reinterpret_cast<const char*>(group->bytes()->data()),
                                                        static_cast<size_t>(group->Length()))});
      }
  }

  void* RtcParser_ParseFinalPayload_Hook(auto original, void* _this, CentrifugoInfo centrifugoInfo,
                                         RealtimeDataPayload* realtimeDataPayload, void* finalPayload)
  {
    if (Config::Get().sync.options.slots && realtimeDataPayload && realtimeDataPayload->Target()
        && realtimeDataPayload->DataType() && realtimeDataPayload->Data()) {
      // DataType arrives as a number in a string; JSON is "0". This runs inside the game's parser, so nothing here
      // may throw.
      const auto* type = realtimeDataPayload->DataType();
      if (type->length == 1 && type->chars[0] == u'0')
        pending.Push({ProcessRealtimeSlot, il2cpp::ToUtf8(realtimeDataPayload->Data())});
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
    original(_this, gameServerUrl, gatewayServerUrl, sessionId, serverRegion);
    SetGameServer(il2cpp::ToUtf8(gameServerUrl), il2cpp::ToUtf8(sessionId));
    // A new login (or a switch of account or server) must not be diffed against the last one's data: the new
    // account gets its first full upload and haven data again, and nothing of the old one is reported gone.
    pending.Push({StartNewSession, {}});
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

    InstallFleetSync();

    StartBattleLogs();
    std::thread(RunJobs).detach();
  }
} // namespace
} // namespace data_sync

const Feature kSync{"Sync", &Config::Patches::syncpatches, data_sync::Install, data_sync::UpdateFleetSync};

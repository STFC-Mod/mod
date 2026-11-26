// [sync]: watches the game's server responses and realtime updates, turns them into records on one processing
// thread (records.cc), and keeps the shared state: records, the game session and the targets. Each target uploads
// on its own thread (upload.cc); battle journals are fetched on another (battlelogs.cc).
//
// TODO: sync has no shutdown. Its threads are detached and run until the process exits, which can interrupt an
// HTTP request mid-flight. The shared state is leaked so they never see it destroyed.

#include "sync/sync.h"

#include "features/features.h"
#include "file.h"

#include "game/app.h"
#include "game/server.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>

#include <thread>
#include <utility>

namespace data_sync
{
namespace
{
  // --- Processing thread ---

  // Responses waiting for the processing thread. A login delivers a few dozen at once; 256 leaves room for a
  // slow backlog without holding unbounded copies of large inventory payloads.
  constexpr size_t kJobQueueCapacity = 256;

  struct Job {
    Processor process;
    Input     input;
  };

  // Leaked, like the shared state: the processing thread may still pop from it during exit.
  auto& pending_jobs = *new BoundedQueue<Job>{"processing", kJobQueueCapacity};

  // One thread processes every response in arrival order, so a delta is never applied before the full state it
  // updates. A processor that throws loses its one response, not the thread.
  void RunJobs()
  {
    for (;;) {
      auto job = pending_jobs.Pop();
      try {
        // Queued before a new login: its records would land in the wrong session.
        if (job.input.generation == SessionGeneration())
          job.process(job.input);
      } catch (const std::exception& e) {
        spdlog::error("[Sync] processing a response failed: {}", e.what());
      } catch (...) {
        spdlog::error("[Sync] processing a response failed with an unknown exception");
      }
    }
  }

  // Runs on the game thread: copies the bytes out and leaves the work to the processing thread.
  void HandleEntityGroup(EntityGroup* entity_group)
  {
    auto* group = entity_group ? entity_group->Group() : nullptr;
    if (!group)
      return;
    const auto type    = entity_group->Type();
    const auto process = ProcessorFor(type);
    // UserProfiles also carries the local player's own record, stored like every category for the targets that
    // sync the profile.
    const bool own_profile = type == EntityGroup::Kind::UserProfiles;
    if (!process && !own_profile)
      return;
    const auto bytes = group->Span();
    Input      input{.generation = SessionGeneration(), .bytes = {bytes.begin(), bytes.end()}};
    // A map response can be another player's Haven, and a profiles response lists other players; the processors keep
    // only the local player's.
    if (type == EntityGroup::Kind::PlanetaryMapResponse || own_profile) {
      auto* manager = UserProfileManager::Instance();
      if (auto* profile = manager ? manager->GetLocalUserProfile() : nullptr) {
        input.user_id = il2cpp::ToUtf8(profile->UserId());
        input.level   = profile->Level();
      }
    }
    if (process)
      pending_jobs.Push({process, input});
    // Skipped if the local profile is not ready; a later response retries.
    if (own_profile && !input.user_id.empty())
      pending_jobs.Push({ProcessOwnProfile, std::move(input)});
  }

  // Platform responses (store offers among them) pass through the same registry code as game server responses. Only
  // store offers are read from them, and only while a target wants them: unlike the categories above, a target that
  // turns them on waits for the next store refresh.
  void HandlePlatformResponse(PlatformServiceResponse* response)
  {
    for (auto* entity_group : il2cpp::Items(response->EntityGroups())) {
      auto* group = entity_group && entity_group->Type() == PlatformEntityGroup::Kind::StoreOffers
                        ? entity_group->Group()
                        : nullptr;
      if (!group)
        continue;
      const auto bytes = group->Span();
      pending_jobs.Push(
          {ProcessStoreOffers, {.generation = SessionGeneration(), .bytes = {bytes.begin(), bytes.end()}}});
    }
  }

  // --- Hooks ---

  // The parser and game server classes below are only bound as hook targets, so their receivers stay untyped.

  void* RtcParser_ParseFinalPayload_Hook(auto original, void* _this, CentrifugoInfo centrifugo_info,
                                         RealtimeDataPayload* payload, void* final_payload)
  {
    // DataType arrives as a number in a string; JSON is "0". This runs inside the game's parser, so it only copies
    // the payload out.
    if (payload && payload->Target() && il2cpp::Equals(payload->DataType(), "0"))
      if (auto* data = payload->Data())
        pending_jobs.Push({ProcessRealtimeSlot, {SessionGeneration(), il2cpp::ToUtf8(data)}});
    return original(_this, centrifugo_info, payload, final_payload);
  }

  void GameServerModelRegistry_ProcessResultInternal_Hook(auto original, void* _this, void* parsing_context,
                                                          ServiceResponse* service_response, MethodInfo* method)
  {
    // The hooked method is shared generic code; PlatformModelRegistry passes its own response type.
    if (service_response && service_response->Is<ServiceResponse>()) {
      for (auto* group : il2cpp::Items(service_response->EntityGroups()))
        HandleEntityGroup(group);
    } else if (service_response && Config::Sync::Get()->options.store_offers) {
      if (auto* platform = service_response->As<PlatformServiceResponse>())
        HandlePlatformResponse(platform);
    }
    return original(_this, parsing_context, service_response, method);
  }

  void PrimeApp_InitPrimeServer_Hook(auto original, PrimeApp* _this, Il2CppString* game_server_url,
                                     Il2CppString* gateway_server_url, Il2CppString* session_id,
                                     Il2CppString* server_region)
  {
    // A new session drops every record; targets start over from the next responses. The generation changes
    // before original can deliver responses from the new session.
    auto  url   = il2cpp::ToUtf8(game_server_url);
    auto  id    = il2cpp::ToUtf8(session_id);
    auto& state = State();
    {
      std::scoped_lock lock(state.mutex);
      state.records.Reset();
      state.session.generation = state.records.Generation();
      state.session.server_url = std::move(url);
      state.session.session_id = std::move(id);
      state.generation         = state.records.Generation();
    }
    state.ready.notify_all();
    original(_this, game_server_url, gateway_server_url, session_id, server_region);
  }

  void GameServer_Initialise_Hook(auto original, void* _this, Il2CppString* session_id, Il2CppString* game_version,
                                  bool encrypt_requests, Il2CppString* server_region)
  {
    original(_this, session_id, game_version, encrypt_requests, server_region);
    auto             version = il2cpp::ToUtf8(game_version);
    auto&            state   = State();
    std::scoped_lock lock(state.mutex);
    state.session.version = std::move(version);
  }

  void GameServer_SetInstanceIdHeader_Hook(auto original, void* _this, int32_t instance_id)
  {
    original(_this, instance_id);
    auto&            state = State();
    std::scoped_lock lock(state.mutex);
    state.session.instance_id = instance_id;
  }

  bool InstallSync()
  {
    il2cpp::HookSet hooks("Sync");
    IL2CPP_STAGE(hooks, GameServerModelRegistry::Method("ProcessResultInternal"),
                 GameServerModelRegistry_ProcessResultInternal_Hook);
    IL2CPP_STAGE(hooks, PrimeApp::Class::Method("InitPrimeServer"), PrimeApp_InitPrimeServer_Hook);

    // Only captures the game version for the X-PRIME-VERSION header, which has a fallback (GameSession). Users
    // whose other mods detour the same method turn it off: a native and a managed detour on one prologue corrupt
    // each other and crash at login. Unlike every other [patches] key, game_version is a switch inside this
    // feature's installer, so it means nothing without syncpatches.
    if (Config::Get().patches.game_version)
      IL2CPP_STAGE(hooks, GameServer::Method("Initialise"), GameServer_Initialise_Hook);
    else
      spdlog::debug("[Sync] not capturing the game version ([patches] game_version = false)");

    IL2CPP_STAGE(hooks, GameServer::Method("SetInstanceIdHeader"), GameServer_SetInstanceIdHeader_Hook);

    // Each parser overrides ParseFinalPayload, so these are two distinct native methods.
    IL2CPP_STAGE(hooks, SlotAssignRtcParser::Method("ParseFinalPayload"), RtcParser_ParseFinalPayload_Hook);
    IL2CPP_STAGE(hooks, SlotClearRtcParser::Method("ParseFinalPayload"), RtcParser_ParseFinalPayload_Hook);
    // Without its hooks sync receives nothing, so its workers would only idle.
    if (!hooks.Commit())
      return false;

    std::thread(RunJobs).detach();
    std::thread(RunBattleLogs).detach();
    FollowSettings();
    return true;
  }

  // Frame work: settings changes reach the targets, then fleet state is polled.
  void SyncFrame()
  {
    FollowSettings();
    UpdateFleetSync();
  }

  // --- Acknowledged battle journals ---

  // The file the acknowledged journals persist in. `io` serializes reading and writing it and is taken before the
  // state mutex, never after; `removed` is guarded by the state mutex. Threads: the game thread (FollowSettings), each
  // target's upload thread (adopting, flushing) and the processing thread (BattleAckedEverywhere, through the battle
  // queue). Only upload threads do file I/O.
  //
  // Known limits, all of which only ever cost a replay: two game processes in one folder overwrite each other's
  // record, the file is renamed into place without a flush to disk, and acknowledgements from the last second or two
  // before the game exits are not written.
  struct BattleAckFile {
    std::mutex           io;
    bool                 loaded    = false;
    bool                 failing   = false;
    bool                 retry     = false;
    bool                 removed   = false;
    bool                 read_only = false;
    AckedByTarget        stored;
    Clock::time_point    saved_at{};
    std::chrono::seconds wait{1};
  };
  BattleAckFile& AckFile()
  {
    static auto& file = *new BattleAckFile; // Leaked, like the threads that use it.
    return file;
  }

  // Reads the file once. Missing, unreadable or malformed means empty: every journal is replayed, as without the
  // file. A file from a newer version is left alone for the whole session. Never throws. Holds `io`.
  void LoadBattleAcks(BattleAckFile& file)
  {
    if (std::exchange(file.loaded, true))
      return;
    std::string problem;
    bool        newer = false;
    try {
      file.stored = LoadAcked(File::Path(File::BattleAcks()), problem, newer);
    } catch (...) {
      file.stored = {};
      problem     = "the file could not be read";
    }
    file.read_only = newer;
    if (!problem.empty())
      logging::Error("load", "battle acknowledgements", "{}; every battle journal will be sent again", problem);
  }

  // A save that failed is tried again after a wait that doubles up to a minute; the first failure is logged.
  void NoteBattleAckFailure(BattleAckFile& file, const char* problem)
  {
    file.retry = true;
    file.wait  = file.failing ? std::min(file.wait * 2, std::chrono::seconds(60)) : std::chrono::seconds(2);
    if (!std::exchange(file.failing, true))
      logging::Error("save", "battle acknowledgements", "{}; trying again", problem);
  }

  // Asks the processing thread to decide again, for the last battle header list, which journals to fetch.
  void RequeueBattles()
  { pending_jobs.Push({RequeueLastBattles, {.generation = SessionGeneration()}}); }
} // namespace

// --- Shared state ---

SharedState& State()
{
  static auto& state = *new SharedState;
  return state;
}

void Publish(Stream stream, RecordSet records, uint64_t generation, Update mode)
{
  auto& state = State();
  {
    std::scoped_lock lock(state.mutex);
    if (generation != state.records.Generation() || !state.records.Apply(stream, std::move(records), mode))
      return;
  }
  // Wakes the upload threads only when a stream actually changed.
  state.ready.notify_all();
}

bool HasBattle(uint64_t id, uint64_t generation)
{
  auto&            state = State();
  std::scoped_lock lock(state.mutex);
  return generation == state.records.Generation() && state.records.Contains(Stream::kBattles, id);
}

bool BattleAckedEverywhere(uint64_t id)
{
  auto&            state = State();
  std::scoped_lock lock(state.mutex);
  return EveryBattleTargetAcked(state.targets, id);
}

void AdoptBattleAcks(const std::string& name, Delivery& delivery)
{
  bool replay = false;
  {
    auto&            file = AckFile();
    std::scoped_lock io(file.io);
    LoadBattleAcks(file);
    std::scoped_lock lock(State().mutex);
    const auto       stored = file.stored.find(name);
    delivery.AdoptAcked(stored != file.stored.end() ? stored->second : AckedBattles{});
    replay = delivery.TakeBattleReplay();
  }
  // A target added while the game runs may lack journals that were skipped before it was there.
  if (replay)
    RequeueBattles();
}

// At most one write a second, however many journals were acknowledged in between: a login acknowledges hundreds.
// Never throws: a failure is retried later.
void FlushBattleAcks()
{
  auto&            file = AckFile();
  std::unique_lock io(file.io, std::try_to_lock);
  if (!io.owns_lock())
    return;
  const auto now = Clock::now();
  if (now - file.saved_at < file.wait)
    return;
  try {
    LoadBattleAcks(file);
    if (file.read_only)
      return;
    AckedByTarget snapshot;
    bool          dirty = file.retry;
    {
      auto&            state = State();
      std::scoped_lock lock(state.mutex);
      dirty |= std::exchange(file.removed, false);
      snapshot = CollectAcked(state.targets, file.stored, dirty);
    }
    if (!dirty)
      return;
    std::string problem;
    file.saved_at = now;
    if (!SaveAcked(File::Path(File::BattleAcks(), true), snapshot, problem))
      return NoteBattleAckFailure(file, problem.c_str());
    // What the file holds now. A target removed from the settings is gone from it, so one added again later starts
    // without a record and is replayed everything.
    file.stored  = std::move(snapshot);
    file.retry   = false;
    file.failing = false;
    file.wait    = std::chrono::seconds(1);
  } catch (...) {
    file.saved_at = now;
    NoteBattleAckFailure(file, "the file could not be written");
  }
}

// Starts, reconfigures and retires target threads to match [sync] after a settings reload. Runs every frame, so
// it returns at once while the settings object is unchanged. Game thread only.
void FollowSettings()
{
  static std::shared_ptr<const Config::Sync> applied;
  const auto                                 config = Config::Sync::Get();
  if (applied == config)
    return;
  auto& state   = State();
  bool  requeue = false;
  {
    std::scoped_lock lock(state.mutex);
    // Dropping the registry's reference ends the target's thread after its current request (see UploadTarget).
    std::erase_if(state.targets, [&](const auto& entry) {
      if (config->targets.contains(entry.first))
        return false;
      entry.second->Retire();
      AckFile().removed = true;
      return true;
    });
    for (const auto& [name, options] : config->targets) {
      if (const auto found = state.targets.find(name); found != state.targets.end()) {
        found->second->Configure(options);
        // A target that has not adopted its record yet asks for this itself, once it has.
        requeue |= found->second->AckedAdopted() && found->second->TakeBattleReplay();
        continue;
      }
      auto target = std::make_shared<Delivery>();
      target->Configure(options);
      std::thread(UploadTarget, std::weak_ptr(target), name).detach();
      state.targets.emplace(name, std::move(target));
    }
  }
  applied = config;
  if (requeue)
    RequeueBattles();
  state.ready.notify_all();
}

uint64_t SessionGeneration()
{ return State().generation.load(); }

GameSession Session()
{
  auto&            state = State();
  std::scoped_lock lock(state.mutex);
  return state.session;
}
} // namespace data_sync

const Feature kSync{"Sync", &Config::Patches::syncpatches, data_sync::InstallSync, data_sync::SyncFrame};

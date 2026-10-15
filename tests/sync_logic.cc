// Production encoders against independent wire fixtures, and delivery against controlled HTTP outcomes/time.
#include "sync/sync.h"
#include <Digit.PrimeServer.Models.pb.h>
#include <iostream>
#include <stdexcept>
#include <stfc.sync.pb.h>

struct CountedLogValue {
  int* formats;
};
template <> struct fmt::formatter<CountedLogValue> : fmt::formatter<std::string_view> {
  auto format(const CountedLogValue& value, fmt::format_context& context) const
  {
    ++*value.formats;
    return fmt::formatter<std::string_view>::format("record", context);
  }
};

using nlohmann::json;
namespace game = Digit::PrimeServer::Models;
namespace
{
using namespace data_sync;
RecordStore           store;
auto                  settings = std::make_shared<Config::Sync>();
std::vector<uint64_t> rereads;
Clock::time_point     now{};
SyncTargetConfig      Options()
{
  SyncTargetConfig config;
  config.url = "http://example.invalid";
  for (const auto& option : SyncOptions)
    config.*option.option = true;
  return config;
}
void Check(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
Delivery Target()
{
  Delivery target;
  target.Configure(Options());
  return target;
}
Upload Take(Delivery& target)
{
  auto upload = target.Next(store, now);
  Check(upload.has_value(), "expected a pending upload");
  return std::move(*upload);
}
json Send(Delivery& target)
{
  auto upload  = Take(target);
  auto records = upload.Records();
  target.Complete(std::move(upload), true, store.generation, now);
  return records;
}
template <typename Message> void Process(EntityGroup::Kind kind, const Message& message)
{
  const auto process = ProcessorFor(kind);
  Check(process != nullptr, "processor available");
  process({store.generation, message.SerializeAsString()});
}
void Slots()
{
  auto              target = Target();
  game::EntitySlots slots;
  auto*             slot = slots.add_entityslots_();
  slot->set_id(17);
  slot->set_slotspecid(9);
  slot->mutable_slotitemid()->set_value(42);
  slot->set_slottype(game::SLOTTYPE_CONSUMABLE);
  slot->mutable_consumableslotparams()->mutable_expirytime()->set_seconds(100);
  Process(EntityGroup::Kind::EntitySlots, slots);
  Check(Send(target)
            == json::parse(
                R"([{"type":"slot","sid":17,"slot_type":0,"spec_id":9,"item_id":42,"params":{"expiry_time":100}}])"),
        "consumable wire fixture");
  Process(EntityGroup::Kind::EntitySlots, slots);
  Check(!target.Next(store, now), "unchanged slot is not uploaded again");
  slot->mutable_consumableslotparams()->mutable_expirytime()->set_seconds(200);
  Process(EntityGroup::Kind::EntitySlots, slots);
  Check(Send(target)[0]["params"]["expiry_time"] == 200, "expiry changes with the same item id are uploaded");
  ProcessRealtimeSlot(
      {store.generation,
       R"({"slot_id":17,"slot_type":0,"slot_spec_id":9,"slot_item_id":42,"consumable_slot_params":{"expiry_time":"1970-01-01T00:03:20Z"}})"});
  Check(!target.Next(store, now), "protobuf and realtime expiry encode identical records");
  ProcessRealtimeSlot(
      {store.generation,
       R"({"slot_id":17,"slot_type":0,"slot_spec_id":9,"slot_item_id":42,"consumable_slot_params":{"expiry_time":"1970-01-01T00:05:00Z"}})"});
  Check(Send(target)[0]["params"]["expiry_time"] == 300, "realtime expiry change is uploaded");
  ProcessRealtimeSlot({store.generation, "not JSON"});
  Check(!target.Next(store, now), "malformed JSON cannot change state");
}
void Presets()
{
  auto              target = Target();
  game::EntitySlots slots;
  auto*             slot = slots.add_entityslots_();
  slot->set_id(7);
  slot->set_slotspecid(3);
  slot->set_slottype(game::SLOTTYPE_FLEETPRESET);
  auto* preset = slot->mutable_fleetpresetslotparams();
  preset->set_name("Raid");
  preset->set_order(2);
  auto* setup = preset->add_setups();
  setup->set_drydockid(1);
  setup->add_shipids(99);
  setup->add_officerids(4);
  setup->add_officerids(5);
  Process(EntityGroup::Kind::EntitySlots, slots);
  Check(
      Send(target)
          == json::parse(
              R"([{"type":"slot","sid":7,"slot_type":7,"spec_id":3,"item_id":null,"params":{"name":"Raid","order":2,"setup":[{"drydock_id":1,"ship_id":99,"officer_ids":[4,5]}]}}])"),
      "fleet preset wire fixture");
  ProcessRealtimeSlot(
      {store.generation,
       R"({"slot_id":7,"slot_type":7,"slot_spec_id":3,"slot_item_id":null,"fleet_preset_slot_params":{"name":"Raid","order":2,"setups":[{"d":1,"s":[99],"o":[4,5]}]}})"});
  Check(!target.Next(store, now), "fleet preset transports agree");
}
void Snapshots()
{
  auto                   target = Target();
  game::OfficersResponse officers;
  auto*                  officer = officers.add_officers();
  officer->set_id(8);
  officer->set_rankindex(2);
  officer->set_level(10);
  officer->set_shardcount(15);
  Process(EntityGroup::Kind::Officers, officers);
  Check(Send(target) == json::parse(R"([{"type":"officer","oid":8,"rank":2,"level":10,"shard_count":15}])"),
        "officer wire fixture");
  game::ResourcesResponse resources;
  auto*                   resource = resources.add_resources();
  resource->set_id(3);
  resource->set_currentamount(100);
  Process(EntityGroup::Kind::Resources, resources);
  auto upload = Take(target);
  Check(upload.first_sync, "initial resources use the full-sync header");
  Check(upload.Records() == json::parse(R"([{"type":"resource","rid":3,"amount":100}])"), "resource wire fixture");
  target.Complete(std::move(upload), true, store.generation, now);
  ProcessorFor(EntityGroup::Kind::Json)(
      {store.generation, R"({"resources":{"3":{"current_amount":100}},"starbase_modules":{"x":{"id":4,"level":9}}})"});
  Check(Send(target) == json::parse(R"([{"type":"module","bid":4,"level":9}])"),
        "JSON modules share the canonical encoder");
  Check(!target.Next(store, now), "JSON and protobuf resources share one cursor");
  auto second = Target();
  Check(Take(second).Records().size() > 0, "new target receives unchanged cached state");
}
void EmptyAndRemoved()
{
  auto                            target = Target();
  game::GlobalActiveBuffsResponse buffs;
  auto*                           buff = buffs.add_globalactivebuffs();
  buff->set_buffid(5);
  buff->set_level(2);
  Process(EntityGroup::Kind::GlobalActiveBuffs, buffs);
  Check(Send(target) == json::parse(R"([{"type":"buff","bid":5,"level":2,"expiry_time":null}])"), "buff wire fixture");
  buffs.clear_globalactivebuffs();
  Process(EntityGroup::Kind::GlobalActiveBuffs, buffs);
  Check(Send(target) == json::parse(R"([{"type":"expired_buff","bid":5}])"),
        "empty full response expires missing buffs");
  game::ActiveMissionsResponse active;
  Process(EntityGroup::Kind::ActiveMissions, active);
  Check(Send(target) == json::parse(R"([{"type":"active_mission","mid":null}])"), "empty active mission sentinel");
  game::AwayAssignmentUserListResponse away;
  Process(EntityGroup::Kind::AwayAssignmentsList, away);
  auto upload = Take(target);
  Check(upload.first_sync && upload.Records().empty(), "empty initial snapshot is delivered");
}
void Jobs()
{
  auto              target = Target();
  game::JobResponse jobs;
  auto*             job = jobs.add_jobs();
  job->set_uuid("job-1");
  job->set_type(game::JOBTYPE_RESEARCH);
  job->mutable_researchparams()->set_projectid(7);
  job->mutable_researchparams()->set_level(2);
  job->mutable_starttime()->set_seconds(100);
  job->set_duration(50);
  job->set_reductioninseconds(5);
  Process(EntityGroup::Kind::Jobs, jobs);
  auto records = Send(target);
  Check(
      records
          == json::parse(
              R"([{"type":"job","job_type":3,"uuid":"job-1","start_time":100,"duration":50,"reduction":5,"rid":7,"level":2}])"),
      "research job wire fixture");
  job->set_reductioninseconds(20);
  Process(EntityGroup::Kind::Jobs, jobs);
  Check(Send(target)[0]["reduction"] == 20, "job speedups update the existing record");
  job->mutable_starttime()->set_seconds(120);
  job->set_duration(80);
  Process(EntityGroup::Kind::Jobs, jobs);
  const auto timing = Send(target);
  Check(timing[0]["start_time"] == 120 && timing[0]["duration"] == 80,
        "existing jobs re-emit changed start time and duration");
  jobs.clear_jobs();
  Process(EntityGroup::Kind::Jobs, jobs);
  Check(Send(target) == json::parse(R"([{"type":"completed_job","uuid":"job-1"}])"), "job completion wire fixture");
}
void HavenRecovery()
{
  auto       target  = Target();
  const auto process = ProcessorFor(EntityGroup::Kind::PlanetaryBaseData);
  process({store.generation, "\xff"});
  Check(!target.Next(store, now), "invalid protobuf publishes nothing");
  game::PlanetaryBase haven;
  Process(EntityGroup::Kind::PlanetaryBaseData, haven);
  Check(!target.Next(store, now), "an empty Haven list publishes nothing");
  (*haven.mutable_pieces())[8] = 2;
  Process(EntityGroup::Kind::PlanetaryBaseData, haven);
  Check(Send(target) == json::parse(R"([{"type":"haven_pieces","sid":8,"count":2}])"),
        "valid Haven pieces after parse failure are delivered");
  (*haven.mutable_pieces())[8] = 3;
  Process(EntityGroup::Kind::PlanetaryBaseData, haven);
  Check(Send(target)[0]["count"] == 3, "Haven piece updates are delivered");

  game::PlanetaryMapData map;
  auto&                  building = (*map.mutable_buildings())[5];
  building.set_specid(8);
  building.set_level(2);
  building.set_status(1);
  building.set_position(42);
  Process(EntityGroup::Kind::PlanetaryMapData, map);
  Check(Send(target) == json::parse(R"([{"type":"haven","id":5,"sid":8,"level":2,"status":1,"position":42}])"),
        "Haven buildings include their map identity, status and position");
  building.set_position(99);
  Process(EntityGroup::Kind::PlanetaryMapData, map);
  Check(Send(target)[0]["position"] == 99, "moving a Haven building updates its record");
  Process(EntityGroup::Kind::PlanetaryMapData, map);
  Check(!target.Next(store, now), "unchanged Haven buildings are suppressed");

  game::PlanetaryMapResponse response;
  *response.mutable_mapdata() = map;
  (*response.mutable_mapdata()->mutable_buildings())[5].set_level(3);
  response.set_owneruserid("other");
  const auto process_map = ProcessorFor(EntityGroup::Kind::PlanetaryMapResponse);
  Check(process_map != nullptr, "Haven map response processor exists");
  process_map({store.generation, response.SerializeAsString(), "local"});
  Check(!target.Next(store, now), "another player's Haven map is ignored");
  response.set_owneruserid("local");
  process_map({store.generation, response.SerializeAsString()});
  Check(!target.Next(store, now), "owned maps require a captured local identity");
  process_map({store.generation, response.SerializeAsString(), "local"});
  Check(Send(target)[0]["level"] == 3, "local player's Haven response is delivered");
}
void ShipTolerance()
{
  auto       target = Target();
  const auto apply  = [](int hull, double progress) {
    store.Apply(Stream::kShips, {{1,
                                  {{"psid", 1},
                                   {"hull_id", hull},
                                   {"tier", 2},
                                   {"level", 5},
                                   {"level_percentage", progress},
                                   {"components", json::array({3})}}}});
  };
  apply(9, 10.0);
  Send(target);
  apply(9, 10.005);
  Check(!target.Next(store, now), "ship progress tolerance is retained");
  apply(10, 10.005);
  Check(Send(target)[0]["hull_id"] == 10, "hull change is not hidden by progress tolerance");
  apply(10, 10.03);
  Check(Send(target)[0]["level_percentage"] == 10.03, "meaningful progress change is uploaded");
}
void RetryAndCoalescing()
{
  auto target = Target();
  store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", 100}}}}, Update::kFull);
  auto       upload = Take(target);
  const auto old    = upload.Records();
  target.Complete(std::move(upload), false, store.generation, now);
  Check(!target.Next(store, now), "failed upload backs off");
  now += std::chrono::seconds(1);
  upload = Take(target);
  Check(upload.first_sync && upload.Records() == old, "retry retains the initial-sync marker and record");
  target.Complete(std::move(upload), false, store.generation, now);
  for (int i = 0; i < 1000; ++i)
    store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", i}}}}, Update::kFull);
  now += std::chrono::seconds(2);
  upload = Take(target);
  Check(upload.Records().size() == 1 && upload.Records()[0]["amount"] == 999,
        "updates coalesce without a dropping upload queue");
  target.Complete(std::move(upload), true, store.generation, now);
  Check(!target.Next(store, now), "successful upload advances only that target");
  auto second = Target();
  Check(Take(second).Records()[0]["amount"] == 999, "targets acknowledge independently");
}
void AmbiguousRemoval()
{
  auto target = Target();
  store.Apply(Stream::kJobs, {}, Update::kReplace);
  Send(target);
  store.Apply(Stream::kJobs, {{"j", {{"uuid", "j"}}}}, Update::kReplace);
  auto upload = Take(target);
  target.Complete(std::move(upload), false, store.generation, now);
  store.Apply(Stream::kJobs, {}, Update::kReplace);
  now += std::chrono::seconds(1);
  Check(Send(target) == json::parse(R"([{"type":"completed_job","uuid":"j"}])"),
        "timeout after a possible server write still emits removal");
  Check(!target.Next(store, now), "removal is acknowledged once");
}
void EmptyBaselines()
{
  for (const auto stream : {Stream::kJobs, Stream::kBuffs, Stream::kInventory}) {
    store.Reset();
    auto target = Target();
    store.Apply(stream, {}, Update::kReplace);
    auto upload = Take(target);
    Check(upload.first_sync && upload.Records().empty(), "empty initial lists still establish a baseline");
    target.Complete(std::move(upload), true, store.generation, now);
    Check(!target.Next(store, now), "acknowledged empty baselines stop uploading");
    target.Reset(store.generation);
    Check(Take(target).first_sync, "reset cursors resend an empty baseline");
  }
}
void AcknowledgedAndUncertainRemovals()
{
  auto target = Target();
  store.Apply(Stream::kJobs, {{20, {{"uuid", 20}}}}, Update::kReplace);
  Send(target);
  store.Apply(Stream::kJobs, {{10, {{"uuid", 10}}}, {20, {{"uuid", 20}}}}, Update::kReplace);
  auto upload = Take(target);
  target.Complete(std::move(upload), false, store.generation, now);
  store.Apply(Stream::kJobs, {}, Update::kReplace);
  now += std::chrono::seconds(1);
  const auto removals = Send(target);
  Check(removals.size() == 2, "acknowledged and uncertain records are each removed once");
  for (const auto& record : removals)
    Check(record["type"] == "completed_job" && (record["uuid"] == 10 || record["uuid"] == 20),
          "both removal sources preserve their wire identity");
  Check(removals[0]["uuid"] != removals[1]["uuid"], "removal sources are disjoint");
  Check(!target.Next(store, now), "successful removal clears both cursor maps");
}
void InFlightUpdates()
{
  auto target = Target();
  store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", 100}}}}, Update::kFull);
  auto upload = Take(target);
  store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", 200}}}}, Update::kFull);
  target.Complete(std::move(upload), true, store.generation, now);
  Check(Send(target)[0]["amount"] == 200, "an in-flight acknowledgement cannot swallow a newer update");
  store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", 300}}}}, Update::kFull);
  upload = Take(target);
  target.Complete(std::move(upload), false, store.generation, now);
  store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", 200}}}}, Update::kFull);
  now += std::chrono::seconds(1);
  Check(Send(target)[0]["amount"] == 200, "after a timeout a reverted value must repair the possibly committed write");
  Check(!target.Next(store, now), "successful repair stops retries");
}
void RemovalBacklog()
{
  auto target = Target();
  store.Apply(Stream::kJobs, {}, Update::kReplace);
  Send(target);
  for (int id = 0; id <= 1024; ++id) {
    store.Apply(Stream::kJobs, {{id, {{"uuid", id}}}}, Update::kReplace);
    auto upload = Take(target);
    target.Complete(std::move(upload), false, store.generation, now);
    now += std::chrono::seconds(60);
  }
  store.Apply(Stream::kJobs, {{1025, {{"uuid", 1025}}}}, Update::kReplace);
  auto upload = Take(target);
  Check(!upload.first_sync && upload.Records().size() == 1024,
        "removal backlog remains bounded without discarding removals");
  for (const auto& record : upload.Records())
    Check(record["type"] == "completed_job" && record["uuid"] != 1024,
          "entities never introduced to the target need no removal");
  target.Complete(std::move(upload), true, store.generation, now);
  Check(Send(target)[0]["uuid"] == 1025, "delivery resumes the latest state once removals are acknowledged");
  Check(!target.Next(store, now), "no blocked update was falsely acknowledged");
}
void LargeBaseline()
{
  auto      target = Target();
  RecordSet jobs;
  for (int id = 0; id < 2500; ++id)
    jobs[id] = {{"uuid", id}};
  store.Apply(Stream::kJobs, std::move(jobs), Update::kReplace);
  auto upload = Take(target);
  Check(!upload.first_sync && upload.Records().size() == 1024, "a bounded partial upload cannot claim a full baseline");
  target.Complete(std::move(upload), true, store.generation, now);
  upload = Take(target);
  Check(!upload.first_sync && upload.snapshot.size() == 2048,
        "acknowledged entities do not consume the uncertain-write budget");
  target.Complete(std::move(upload), true, store.generation, now);
  upload = Take(target);
  Check(upload.first_sync && upload.Records().size() == 2500, "the complete baseline eventually includes every entity");
  target.Complete(std::move(upload), true, store.generation, now);
  Check(!target.Next(store, now), "the complete baseline is acknowledged only after delivery");
}
void DeltaBeforeBaseline()
{
  auto                         target = Target();
  game::ResourcesDeltaResponse delta;
  auto*                        resource = delta.add_resources();
  resource->set_id(7);
  resource->set_amount(50);
  Process(EntityGroup::Kind::ResourcesDelta, delta);
  auto upload = Take(target);
  Check(!upload.first_sync, "a resource delta cannot claim to be a complete baseline");
  target.Complete(std::move(upload), true, store.generation, now);
  game::ResourcesResponse baseline;
  auto*                   full = baseline.add_resources();
  full->set_id(7);
  full->set_currentamount(50);
  full = baseline.add_resources();
  full->set_id(8);
  full->set_currentamount(60);
  Process(EntityGroup::Kind::Resources, baseline);
  upload = Take(target);
  Check(upload.first_sync && upload.Records().size() == 2,
        "the first full list includes previously acknowledged deltas");
}
void ConfigurationAndSession()
{
  auto target = Target();
  store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", 100}}}}, Update::kFull);
  auto upload   = Take(target);
  auto options  = Options();
  options.token = "new-account";
  target.Configure(options);
  target.Complete(std::move(upload), true, store.generation, now);
  upload = Take(target);
  Check(upload.first_sync, "old configuration acknowledgement is rejected");
  target.Complete(std::move(upload), true, store.generation, now);
  options.resources = false;
  target.Configure(options);
  Check(!target.Next(store, now), "disabled category produces no upload");
  options.resources = true;
  target.Configure(options);
  upload = Take(target);
  Check(upload.first_sync, "re-enabled category receives a baseline");
  const auto old_generation = store.generation;
  store.Reset();
  target.Complete(std::move(upload), true, store.generation, now);
  Publish(Stream::kResources, {{3, {{"rid", 3}, {"amount", 999}}}}, old_generation, Update::kFull);
  Check(!target.Next(store, now), "old queued work cannot populate a new session");
  Publish(Stream::kResources, {{3, {{"rid", 3}, {"amount", 200}}}}, store.generation, Update::kFull);
  upload = Take(target);
  Check(upload.first_sync && upload.Records()[0]["amount"] == 200, "new session starts with its own baseline");
}
void SyncLogging()
{
  const auto previous_level   = spdlog::get_level();
  const auto previous_logging = settings->logging, previous_debug = settings->debug;
  int        formats = 0;
  settings->logging = settings->debug = true;
  spdlog::set_level(spdlog::level::off);
  logging::error("TEST", "target", "{}", CountedLogValue{&formats});
  logging::debug("TEST", "target", "{}", CountedLogValue{&formats});
  logging::trace("TEST", "target", "{}", CountedLogValue{&formats});
  const bool skipped = formats == 0;
  formats            = 0;
  spdlog::set_level(spdlog::level::trace);
  settings->logging = false;
  logging::error("TEST", "target", "{}", CountedLogValue{&formats});
  settings->logging = true;
  settings->debug   = false;
  logging::debug("TEST", "target", "{}", CountedLogValue{&formats});
  logging::trace("TEST", "target", "{}", CountedLogValue{&formats});
  const bool gated = formats == 0;
  formats          = 0;
  logging::error("TEST", "target", "{}", CountedLogValue{&formats});
  settings->debug = true;
  logging::debug("TEST", "target", "{}", CountedLogValue{&formats});
  logging::trace("TEST", "target", "{}", CountedLogValue{&formats});
  spdlog::set_level(previous_level);
  settings->logging = previous_logging;
  settings->debug   = previous_debug;
  Check(skipped, "globally disabled sync logs do not format their arguments");
  Check(gated, "sync logging and debug flags still suppress formatting");
  Check(formats == 3, "enabled sync messages format once");
}
void BattlesAndFairness()
{
  auto target = Target();
  for (int i = 1; i <= 305; ++i)
    store.Apply(Stream::kBattles, {{i, {{"journal", {{"id", i}}}, {"names", json::object()}}}});
  Check(!store.Contains(Stream::kBattles, 1) && store.Contains(Stream::kBattles, 305),
        "recent journals have a 300-record bound");
  store.Apply(Stream::kResources, {{1, {{"rid", 1}, {"amount", 7}}}}, Update::kFull);
  auto upload = Take(target);
  Check(upload.Records().size() == 1, "journals retain one-record request batches");
  target.Complete(std::move(upload), false, store.generation, now);
  upload = Take(target);
  Check(upload.stream == Stream::kResources, "unavailable journal category cannot starve resource uploads");
  target.Complete(std::move(upload), true, store.generation, now);
  now += std::chrono::seconds(1);
  for (int id = 6; id <= 305; ++id)
    Check(Send(target)[0]["journal"]["id"] == id, "each cached journal is acknowledged independently");
  Check(!target.Next(store, now), "journal delivery stops after acknowledgement");
  store.Apply(Stream::kBattles, {{306, {{"journal", {{"id", 306}}}, {"names", json::object()}}}});
  Check(Send(target)[0]["journal"]["id"] == 306, "pruning old journal cursors cannot suppress new journals");
  auto second = Target();
  Check(Send(second)[0]["journal"]["id"] == 7, "a new target receives the retained journals");
}
void FleetTransitions()
{
  auto       target = Target();
  const auto change = [](int sequence, const char* state, bool snapshot = false) {
    store.Apply(Stream::kFleets,
                {{json::array({sequence, snapshot}),
                  json::array({{{"fid", 5}, {"slot", 0}, {"state_name", state}, {"src", "client"}}})}});
  };
  change(1, "Mining");
  change(2, "Docked");
  change(3, "Repairing");
  auto upload = Take(target);
  Check(!upload.first_sync
            && upload.Records()
                   == json::parse(R"([{"type":"fleet","fid":5,"slot":0,"state_name":"Mining","src":"client"}])"),
        "client fleet wire fixture");
  target.Complete(std::move(upload), false, store.generation, now);
  Check(!target.Next(store, now), "a failed fleet change backs off");
  now += std::chrono::seconds(1);
  Check(Send(target)[0]["state_name"] == "Mining", "a failed fleet change is retried before later ones");
  Check(Send(target)[0]["state_name"] == "Docked", "changes to one fleet are not merged");
  Check(Send(target)[0]["state_name"] == "Repairing", "every fleet change is delivered in order");
  Check(!target.Next(store, now), "fleet delivery stops after acknowledgement");
  change(4, "Repairing", true);
  upload = Take(target);
  Check(upload.first_sync && upload.Records().size() == 1, "a fleet snapshot uses the full-sync header");
  target.Complete(std::move(upload), true, store.generation, now);
  auto second = Target();
  Check(Send(second)[0]["state_name"] == "Mining", "a new target receives the retained fleet changes");
  for (int sequence = 5; sequence <= 305; ++sequence)
    change(sequence, "Docked");
  Check(!store.Contains(Stream::kFleets, json::array({5, false}))
            && store.Contains(Stream::kFleets, json::array({6, false})),
        "fleet changes have a 300-entry bound");
}
void DeployedFleets()
{
  auto target = Target();
  rereads.clear();
  game::MyDeployedFleetsResponse response;
  auto&                          fleet = (*response.mutable_fleets())[5];
  fleet.set_state(game::DEPLOYEDFLEETSTATE_DOCKED);
  fleet.set_nodeid(77);
  fleet.add_hullids(3005922948);
  fleet.mutable_nodeaddress()->set_system(1234);
  (*fleet.mutable_stats())[game::CLIENTMODIFIERTYPE_MODCARGOCAPACITY]                                 = 1500;
  (*fleet.mutable_attributes())[game::CLIENTMODIFIERTYPE_FLEETSTRENGTH]                               = 14.364f;
  (*(*fleet.mutable_shipstats())[9].mutable_shipattributes_())[game::CLIENTMODIFIERTYPE_MODHULLHPMAX] = 1000;
  auto& damage =
      (*(*fleet.mutable_shipattributes())[9].mutable_shipattributes_())[game::CLIENTMODIFIERTYPE_SHIPHULLDAMAGE];
  damage = 250;
  fleet.set_pursuedbynum(2);
  Process(EntityGroup::Kind::MyDeployedFleets, response);
  auto records = Send(target);
  Check(records.size() == 1 && records[0]["t"].is_number_integer(), "server fleet records carry their read time");
  records[0].erase("t");
  Check(
      records
          == json::parse(
              R"([{"type":"fleet","fid":5,"src":"server","server_state":5,"server_state_name":"OnNode","node_id":77,"hull_id":3005922948,"system_id":1234,"hull_hp":750,"hull_max":1000,"cargo_cap":1500,"strength":14.364,"pursued_by":2}])"),
      "server fleet wire fixture");
  Check(rereads == std::vector<uint64_t>{5}, "a changed deployed fleet is re-read");
  Process(EntityGroup::Kind::MyDeployedFleets, response);
  Check(!target.Next(store, now) && rereads.size() == 1, "an unchanged deployed fleet is neither sent nor re-read");
  damage = 400;
  Process(EntityGroup::Kind::MyDeployedFleets, response);
  Check(Send(target)[0]["hull_hp"] == 600 && rereads.size() == 2, "a changed deployed fleet is sent and re-read");
  const auto departed = fleet;
  response.clear_fleets();
  Process(EntityGroup::Kind::MyDeployedFleets, response);
  Check(!target.Next(store, now) && rereads.size() == 3, "a departed fleet sends nothing and is re-read");
  (*response.mutable_fleets())[5] = departed;
  Process(EntityGroup::Kind::MyDeployedFleets, response);
  Check(Send(target)[0]["fid"] == 5, "a returning fleet is sent again");
}
void RepairJobs()
{
  auto target = Target();
  rereads.clear();
  game::JobResponse jobs;
  auto*             job = jobs.add_jobs();
  job->set_uuid("repair-1");
  job->set_type(game::JOBTYPE_REPAIRFLEET);
  job->mutable_repairfleetparams()->set_fleetid(5);
  job->mutable_starttime()->set_seconds(100);
  job->set_duration(30);
  settings->options.fleets = false;
  Process(EntityGroup::Kind::Jobs, jobs);
  settings->options.fleets = true;
  Check(Send(target).empty() && rereads.empty(), "repair jobs are left out without fleet sync");
  Process(EntityGroup::Kind::Jobs, jobs);
  Check(
      Send(target)
          == json::parse(
              R"([{"type":"job","job_type":5,"uuid":"repair-1","start_time":100,"duration":30,"reduction":0,"fid":5}])"),
      "repair job wire fixture");
  Check(rereads == std::vector<uint64_t>{5}, "a repair job re-reads its fleet");
}
} // namespace
std::shared_ptr<const Config::Sync> Config::Sync::Get()
{ return settings; }

namespace data_sync
{
void Publish(Stream stream, RecordSet records, uint64_t generation, Update mode)
{
  if (generation == store.generation)
    store.Apply(stream, std::move(records), mode);
}
void QueueNewBattles(const std::vector<uint64_t>&, uint64_t) {}
void CachePlayerNames(const Input&) {}
void CacheAllianceNames(const Input&) {}
void RereadFleet(uint64_t fleet_id)
{ rereads.push_back(fleet_id); }
} // namespace data_sync
int main()
{
  for (const auto& option : SyncOptions)
    settings->options.*option.option = true;
  int        failures = 0;
  const auto run      = [&](const char* name, void (*test)()) {
    store.Reset();
    now = {};
    try {
      test();
    } catch (const std::exception& e) {
      std::cerr << name << ": " << e.what() << '\n';
      ++failures;
    }
  };
  run("slots", Slots);
  run("presets", Presets);
  run("snapshots", Snapshots);
  run("removals", EmptyAndRemoved);
  run("jobs", Jobs);
  run("haven", HavenRecovery);
  run("ships", ShipTolerance);
  run("retry", RetryAndCoalescing);
  run("ambiguous removal", AmbiguousRemoval);
  run("empty baselines", EmptyBaselines);
  run("removal sources", AcknowledgedAndUncertainRemovals);
  run("in-flight updates", InFlightUpdates);
  run("removal backlog", RemovalBacklog);
  run("large baseline", LargeBaseline);
  run("delta before baseline", DeltaBeforeBaseline);
  run("configuration/session", ConfigurationAndSession);
  run("battles", BattlesAndFairness);
  run("fleet transitions", FleetTransitions);
  run("deployed fleets", DeployedFleets);
  run("repair jobs", RepairJobs);
  run("logging", SyncLogging);
  if (!failures)
    std::cout << "Sync wire and delivery tests passed\n";
  return failures ? 1 : 0;
}

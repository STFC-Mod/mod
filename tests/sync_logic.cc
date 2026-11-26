// Production encoders against independent wire fixtures, and delivery against controlled HTTP outcomes and time.
// Check throws instead of asserting, so one run reports every failing case.

#include "sync/journal.h"
#include "sync/sync.h"

#include <Digit.PrimePlatform.Content.pb.h>
#include <Digit.PrimeServer.Models.pb.h>
#include <stfc.sync.pb.h>

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

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
namespace game    = Digit::PrimeServer::Models;
namespace content = Digit::PrimePlatform::Content;
namespace
{
using namespace data_sync;
RecordStore          store;
auto                 settings = std::make_shared<Config::Sync>();
std::vector<int64_t> rereads;
Clock::time_point    now{};
SyncTargetConfig     Options()
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
  target.Complete(std::move(upload), true, store.Generation(), now);
  return records;
}
template <typename Message> void Process(EntityGroup::Kind kind, const Message& message)
{
  const auto process = ProcessorFor(kind);
  Check(process != nullptr, "processor available");
  process({store.Generation(), message.SerializeAsString()});
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
      {store.Generation(),
       R"({"slot_id":17,"slot_type":0,"slot_spec_id":9,"slot_item_id":42,"consumable_slot_params":{"expiry_time":"1970-01-01T00:03:20Z"}})"});
  Check(!target.Next(store, now), "protobuf and realtime expiry encode identical records");
  ProcessRealtimeSlot(
      {store.Generation(),
       R"({"slot_id":17,"slot_type":0,"slot_spec_id":9,"slot_item_id":42,"consumable_slot_params":{"expiry_time":"1970-01-01T00:05:00Z"}})"});
  Check(Send(target)[0]["params"]["expiry_time"] == 300, "realtime expiry change is uploaded");
  ProcessRealtimeSlot({store.Generation(), "not JSON"});
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
      {store.Generation(),
       R"({"slot_id":7,"slot_type":7,"slot_spec_id":3,"slot_item_id":null,"fleet_preset_slot_params":{"name":"Raid","order":2,"setups":[{"d":1,"s":[99],"o":[4,5]}]}})"});
  Check(!target.Next(store, now), "fleet preset transports agree");
}
void SlotTransports()
{
  auto              target = Target();
  game::EntitySlots slots;
  auto*             preset = slots.add_entityslots_();
  preset->set_id(1);
  preset->set_slotspecid(10);
  preset->set_slottype(game::SLOTTYPE_OFFICERPRESET);
  preset->mutable_officerpresetslotparams()->set_name("Mining");
  preset->mutable_officerpresetslotparams()->set_order(1);
  preset->mutable_officerpresetslotparams()->add_officerids(4);
  preset->mutable_officerpresetslotparams()->add_officerids(5);
  auto* commander = slots.add_entityslots_();
  commander->set_id(2);
  commander->set_slotspecid(11);
  commander->set_slottype(game::SLOTTYPE_FLEETCOMMANDER);
  commander->mutable_fleetcommanderslotparams()->set_order(3);
  auto* skill = slots.add_entityslots_();
  skill->set_id(3);
  skill->set_slotspecid(12);
  skill->set_slottype(game::SLOTTYPE_SELECTABLESKILL);
  skill->mutable_selectableskillslotparams()->mutable_cooldownexpiration()->set_seconds(300);
  auto* tech = slots.add_entityslots_();
  tech->set_id(4);
  tech->set_slotspecid(13);
  tech->set_slottype(game::SLOTTYPE_FORBIDDENTECH);
  tech->mutable_forbiddentechslotparams()->set_ownerid(6);
  tech->mutable_forbiddentechslotparams()->set_tier(2);
  tech->mutable_forbiddentechslotparams()->set_level(7);
  auto* worker = slots.add_entityslots_();
  worker->set_id(5);
  worker->set_slottype(game::SLOTTYPE_WORKER);
  Process(EntityGroup::Kind::EntitySlots, slots);
  Check(
      Send(target)
          == json::parse(
              R"([{"type":"slot","sid":1,"slot_type":2,"spec_id":10,"item_id":null,"params":{"name":"Mining","order":1,"officer_ids":[4,5]}},
                  {"type":"slot","sid":2,"slot_type":3,"spec_id":11,"item_id":null,"params":{"order":3}},
                  {"type":"slot","sid":3,"slot_type":4,"spec_id":12,"item_id":null,"params":{"cooldown_expiration":300}},
                  {"type":"slot","sid":4,"slot_type":6,"spec_id":13,"item_id":null,"params":{"owner_id":6,"tier":2,"level":7}}])"),
      "slot parameter wire fixtures, without unsupported slot types");
  const char* realtime[] = {
      R"({"slot_id":1,"slot_type":2,"slot_spec_id":10,"slot_item_id":null,"officer_preset_slot_params":{"name":"Mining","order":1,"officer_ids":[4,5]}})",
      R"({"slot_id":2,"slot_type":3,"slot_spec_id":11,"fleet_commander_slot_params":{"order":3}})",
      R"({"slot_id":3,"slot_type":4,"slot_spec_id":12,"selectable_skill_slot_params":{"cooldown_expiration":"1970-01-01T00:05:00Z"}})",
      R"({"slot_id":4,"slot_type":6,"slot_spec_id":13,"forbidden_tech_slot_params":{"owner_id":6,"tier":2,"level":7}})",
      R"({"slot_id":5,"slot_type":1,"slot_spec_id":0})",
  };
  for (const auto* payload : realtime)
    ProcessRealtimeSlot({store.Generation(), payload});
  Check(!target.Next(store, now), "every realtime slot type encodes the protobuf record");
  ProcessRealtimeSlot({store.Generation(), R"({"slot_id":2,"slot_type":3,"slot_spec_id":11,"slot_item_id":8})"});
  Check(Send(target)
            == json::parse(R"([{"type":"slot","sid":2,"slot_type":3,"spec_id":11,"item_id":8,"params":null}])"),
        "a realtime slot without parameters has null params");
  ProcessRealtimeSlot(
      {store.Generation(),
       R"({"slot_id":3,"slot_type":4,"slot_spec_id":12,"selectable_skill_slot_params":{"cooldown_expiration":"soon"}})"});
  Check(Send(target)[0]["params"] == json::parse(R"({"cooldown_expiration":null})"),
        "an unreadable realtime timestamp is null");
}
void AwayAndEmerald()
{
  auto                         target = Target();
  game::AwayAssignmentInstance away;
  away.set_id(4);
  (*away.mutable_officerids())[3]                                = 30;
  (*away.mutable_officerids())[1]                                = 10;
  (*away.mutable_officerids())[2]                                = 0;
  (*away.mutable_parameters()->mutable_criticaltraitscores())[9] = 5;
  (*away.mutable_parameters()->mutable_criticaltraitscores())[7] = 6;
  Process(EntityGroup::Kind::AwayAssignmentsInstance, away);
  const auto records = Send(target);
  Check(records.size() == 1 && records[0]["aid"] == 4 && records[0]["officer_ids"] == json::parse("[10,30]")
            && records[0]["critical_trait_scores"] == json::parse(R"([{"tid":7,"score":6},{"tid":9,"score":5}])"),
        "away assignment lists go out in key order without empty slots");
  game::AllianceGamePropertiesResponse properties;
  auto*                                other = properties.add_properties();
  other->set_propertyname("other");
  other->add_valuelist("99");
  auto* tiers = properties.add_properties();
  tiers->set_propertyname("claimed_loyalty_tiers");
  tiers->add_valuelist("2");
  tiers->add_valuelist("x");
  tiers->add_valuelist("5");
  Process(EntityGroup::Kind::AllianceGetGameProperties, properties);
  Check(Send(target) == json::parse(R"([{"type":"emerald_chain","level":5}])"), "emerald chain wire fixture");
  game::AllianceGamePropertiesResponse unclaimed;
  unclaimed.add_properties()->set_propertyname("claimed_loyalty_tiers");
  Process(EntityGroup::Kind::AllianceGetGameProperties, unclaimed);
  Check(Send(target) == json::parse(R"([{"type":"emerald_chain","level":-1}])"), "no claimed tier is level -1");
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
  target.Complete(std::move(upload), true, store.Generation(), now);
  ProcessorFor(EntityGroup::Kind::Json)(
      {store.Generation(),
       R"({"resources":{"3":{"current_amount":100}},"starbase_modules":{"x":{"id":4,"level":9}}})"});
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
  process({store.Generation(), "\xff"});
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
  process_map({store.Generation(), response.SerializeAsString(), "local"});
  Check(!target.Next(store, now), "another player's Haven map is ignored");
  response.set_owneruserid("local");
  process_map({store.Generation(), response.SerializeAsString()});
  Check(!target.Next(store, now), "owned maps require a captured local identity");
  process_map({store.Generation(), response.SerializeAsString(), "local"});
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
  target.Complete(std::move(upload), false, store.Generation(), now);
  Check(!target.Next(store, now), "failed upload backs off");
  now += std::chrono::seconds(1);
  upload = Take(target);
  Check(upload.first_sync && upload.Records() == old, "retry retains the initial-sync marker and record");
  target.Complete(std::move(upload), false, store.Generation(), now);
  for (int i = 0; i < 1000; ++i)
    store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", i}}}}, Update::kFull);
  now += std::chrono::seconds(2);
  upload = Take(target);
  Check(upload.Records().size() == 1 && upload.Records()[0]["amount"] == 999,
        "updates coalesce without a dropping upload queue");
  target.Complete(std::move(upload), true, store.Generation(), now);
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
  target.Complete(std::move(upload), false, store.Generation(), now);
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
    target.Complete(std::move(upload), true, store.Generation(), now);
    Check(!target.Next(store, now), "acknowledged empty baselines stop uploading");
    store.Reset();
    store.Apply(stream, {}, Update::kReplace);
    Check(Take(target).first_sync, "a new session resends an empty baseline");
  }
}
void AcknowledgedAndUncertainRemovals()
{
  auto target = Target();
  store.Apply(Stream::kJobs, {{20, {{"uuid", 20}}}}, Update::kReplace);
  Send(target);
  store.Apply(Stream::kJobs, {{10, {{"uuid", 10}}}, {20, {{"uuid", 20}}}}, Update::kReplace);
  auto upload = Take(target);
  target.Complete(std::move(upload), false, store.Generation(), now);
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
  target.Complete(std::move(upload), true, store.Generation(), now);
  Check(Send(target)[0]["amount"] == 200, "an in-flight acknowledgement cannot swallow a newer update");
  store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", 300}}}}, Update::kFull);
  upload = Take(target);
  target.Complete(std::move(upload), false, store.Generation(), now);
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
    target.Complete(std::move(upload), false, store.Generation(), now);
    now += std::chrono::seconds(60);
  }
  store.Apply(Stream::kJobs, {{1025, {{"uuid", 1025}}}}, Update::kReplace);
  auto upload = Take(target);
  Check(!upload.first_sync && upload.Records().size() == 1024,
        "removal backlog remains bounded without discarding removals");
  for (const auto& record : upload.Records())
    Check(record["type"] == "completed_job" && record["uuid"] != 1024,
          "entities never introduced to the target need no removal");
  target.Complete(std::move(upload), true, store.Generation(), now);
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
  target.Complete(std::move(upload), true, store.Generation(), now);
  upload = Take(target);
  Check(!upload.first_sync && upload.snapshot.size() == 2048,
        "acknowledged entities do not consume the uncertain-write budget");
  target.Complete(std::move(upload), true, store.Generation(), now);
  upload = Take(target);
  Check(upload.first_sync && upload.Records().size() == 2500, "the complete baseline eventually includes every entity");
  target.Complete(std::move(upload), true, store.Generation(), now);
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
  target.Complete(std::move(upload), true, store.Generation(), now);
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
  target.Complete(std::move(upload), true, store.Generation(), now);
  upload = Take(target);
  Check(upload.first_sync, "old configuration acknowledgement is rejected");
  target.Complete(std::move(upload), true, store.Generation(), now);
  options.resources = false;
  target.Configure(options);
  Check(!target.Next(store, now), "disabled category produces no upload");
  options.resources = true;
  target.Configure(options);
  upload = Take(target);
  Check(upload.first_sync, "re-enabled category receives a baseline");
  const auto old_generation = store.Generation();
  store.Reset();
  target.Complete(std::move(upload), true, store.Generation(), now);
  Publish(Stream::kResources, {{3, {{"rid", 3}, {"amount", 999}}}}, old_generation, Update::kFull);
  Check(!target.Next(store, now), "old queued work cannot populate a new session");
  Publish(Stream::kResources, {{3, {{"rid", 3}, {"amount", 200}}}}, store.Generation(), Update::kFull);
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
  logging::Error("TEST", "target", "{}", CountedLogValue{&formats});
  logging::Debug("TEST", "target", "{}", CountedLogValue{&formats});
  logging::Trace("TEST", "target", "{}", CountedLogValue{&formats});
  const bool skipped = formats == 0;
  formats            = 0;
  spdlog::set_level(spdlog::level::trace);
  settings->logging = false;
  logging::Error("TEST", "target", "{}", CountedLogValue{&formats});
  settings->logging = true;
  settings->debug   = false;
  logging::Debug("TEST", "target", "{}", CountedLogValue{&formats});
  logging::Trace("TEST", "target", "{}", CountedLogValue{&formats});
  const bool gated = formats == 0;
  formats          = 0;
  logging::Error("TEST", "target", "{}", CountedLogValue{&formats});
  settings->debug = true;
  logging::Debug("TEST", "target", "{}", CountedLogValue{&formats});
  logging::Trace("TEST", "target", "{}", CountedLogValue{&formats});
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
  target.Complete(std::move(upload), false, store.Generation(), now);
  upload = Take(target);
  Check(upload.stream == Stream::kResources, "unavailable journal category cannot starve resource uploads");
  target.Complete(std::move(upload), true, store.Generation(), now);
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
  target.Complete(std::move(upload), false, store.Generation(), now);
  Check(!target.Next(store, now), "a failed fleet change backs off");
  now += std::chrono::seconds(1);
  Check(Send(target)[0]["state_name"] == "Mining", "a failed fleet change is retried before later ones");
  Check(Send(target)[0]["state_name"] == "Docked", "changes to one fleet are not merged");
  Check(Send(target)[0]["state_name"] == "Repairing", "every fleet change is delivered in order");
  Check(!target.Next(store, now), "fleet delivery stops after acknowledgement");
  change(4, "Repairing", true);
  upload = Take(target);
  Check(upload.first_sync && upload.Records().size() == 1, "a fleet snapshot uses the full-sync header");
  target.Complete(std::move(upload), true, store.Generation(), now);
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
  Check(rereads == std::vector<int64_t>{5}, "a changed deployed fleet is re-read");
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
  Check(rereads == std::vector<int64_t>{5}, "a repair job re-reads its fleet");
}
void RetiredTarget()
{
  auto target = Target();
  store.Apply(Stream::kResources, {{3, {{"rid", 3}, {"amount", 100}}}}, Update::kFull);
  auto upload = Take(target);
  target.Retire();
  target.Complete(std::move(upload), true, store.Generation(), now);
  target.Configure(Options());
  Check(Take(target).first_sync, "an upload in flight when its target was retired acknowledges nothing");
}
void OnlyBattlesArePruned()
{
  for (int i = 1; i <= 305; ++i)
    store.Apply(Stream::kBattles, {{i, {{"journal", {{"id", i}}}}}});
  for (int i = 1; i <= 400; ++i)
    store.Apply(Stream::kResources, {{i, {{"rid", i}, {"amount", i}}}});
  Check(store.Contains(Stream::kResources, 1) && store.Contains(Stream::kResources, 400),
        "the journal bound does not prune other streams");
  Check(!store.Contains(Stream::kBattles, 5) && store.Contains(Stream::kBattles, 6),
        "the journal bound keeps the newest journals");
}
void UnwantedCategories()
{
  settings->options.officers   = false;
  settings->options.battlelogs = false;
  Check(ProcessorFor(EntityGroup::Kind::Officers) != nullptr,
        "a category no target wants is still stored, for a target that enables it later");
  Check(!ProcessorFor(EntityGroup::Kind::BattleResultHeaders), "battle logs are skipped while no target wants them");
  settings->options.officers   = true;
  settings->options.battlelogs = true;
  Check(ProcessorFor(EntityGroup::Kind::BattleResultHeaders) != nullptr, "and run once one does");
}
void Queues()
{
  BoundedQueue<int> queue{"test", 2};
  Check(!queue.Push(1) && !queue.Push(2), "a queue with room drops nothing");
  const auto dropped = queue.Push(3);
  Check(dropped == 1, "a full queue drops and returns its oldest entry");
  Check(queue.Pop() == 2 && queue.Pop() == 3, "the rest keep their order");
}
void NameCaches()
{
  const auto previous_ttl      = settings->resolver_cache_ttl;
  settings->resolver_cache_ttl = 300;
  NameCache<std::string, int> names;
  names.Store("a", 1, 2);
  Check(names.Find("a", 2) == 1, "a stored name is found in its session");
  Check(!names.Find("a", 1), "an older session's lookup finds nothing");
  names.Store("b", 2, 1);
  Check(!names.Find("b", 2), "an older session's late result is not stored");
  Check(!names.Find("a", 3) && names.Size() == 0, "a new session forgets the old names");
  for (size_t i = 0; i <= NameCache<std::string, int>::kCapacity; ++i)
    names.Store(std::to_string(i), static_cast<int>(i), 3);
  Check(names.Size() <= NameCache<std::string, int>::kCapacity, "the cache stays within its bound");
  settings->resolver_cache_ttl = 0;
  names.Store("c", 3, 3);
  Check(!names.Find("c", 3), "a zero TTL expires names at once");
  settings->resolver_cache_ttl = previous_ttl;
}
// Protobuf journals: a synthetic player-vs-NPC battle against the exact JSON journal shape, then bodies that
// must be refused. Values are invented; only their kinds matter.
std::string BattleReportBody(const game::JournalResponse& journal, int32_t type = game::EntityGroup::TYPE_BATTLEREPORT)
{
  game::ServiceResponse response;
  auto*                 group = response.add_entitygroups();
  group->set_type(static_cast<game::EntityGroup::Type>(type));
  group->set_group(journal.SerializeAsString());
  return response.SerializeAsString();
}
game::JournalResponse SyntheticJournal()
{
  game::JournalResponse journal;
  journal.set_id(2'860'000'000'000'000'001);
  journal.set_battleduration(5);
  journal.mutable_battletime()->set_seconds(1'763'600'000);
  journal.set_battletype(5);
  journal.set_initiatorwins(true);
  journal.set_initiatorid("player1");
  journal.set_targetid("mar_30");
  journal.set_systemid(1'234'567'890'123);
  journal.mutable_coords()->set_x(1.1f);
  journal.mutable_coords()->set_y(-2.5f);
  for (const char* entry : {"-96", "1234567890123456789", "5.0", "1.2000000000000002", "-97"})
    journal.add_battlelog(entry);
  (*journal.mutable_resourcesdropped())[2] = 100;
  journal.mutable_battlecontextdata();
  auto* chest = journal.mutable_chestdrop()->add_chestsgained();
  chest->set_type(game::INVENTORYITEMTYPE_INVENTORYCHEST);
  chest->set_count(1);
  chest->mutable_commonparams()->set_refid(77);
  journal.mutable_chestdrop()->add_chestidrefs()->set_locaid(9);

  auto* player = journal.mutable_initiatorfleetdata();
  player->set_cargomax(1000);
  player->set_officerrating(5);
  player->add_hullids(11);
  player->add_shipids(21);
  player->add_initialshiplevels(30);
  player->add_initialshiplevelpercentages(0.3f);
  player->add_initialshiphps(5000);
  player->mutable_initialcargo();
  auto* crew = player->add_fleetsofficers();
  crew->set_fleetid(31);
  crew->set_crewsize(10);
  auto* captain = crew->add_officers();
  captain->set_id(41);
  captain->set_level(50);
  captain->set_rank(3);
  auto* officer = crew->add_officers();
  officer->set_id(42);
  officer->set_level(40);
  officer->set_rank(2);
  officer->set_slotindex(3);
  auto* fleet = player->mutable_deployedfleet();
  fleet->set_uid("player1");
  fleet->set_fleetid(31);
  fleet->set_fleettype(game::DEPLOYEDFLEETTYPE_PLAYER);
  fleet->set_causeofdestruction(game::DAMAGESOURCES_NOTSET);
  fleet->add_shipids(21);
  fleet->add_hullids(11);
  (*fleet->mutable_attributes())[-9] = 1234.6f;
  (*fleet->mutable_stats())[64]      = 16.7f;
  (*fleet->mutable_stats())[63]      = 2.0f;
  auto& ship                         = *(*fleet->mutable_shipstats())[21].mutable_shipattributes_();
  ship[61]                           = 5000.0f;
  ship[60]                           = 2500.4f;
  (*fleet->mutable_shiplevels())[21] = 30;
  (*fleet->mutable_shiptiers())[21]  = 4;
  auto& components                   = (*fleet->mutable_deployedships())[21];
  components.add_componentids(101);
  components.add_componentids(-1);
  (*player->mutable_deployedfleets())[31] = *fleet;

  auto* npc = journal.mutable_targetfleetdata();
  npc->mutable_refids()->set_locaid(555);
  npc->add_shipids(0);
  auto* npc_fleet = npc->mutable_deployedfleet();
  npc_fleet->set_uid("mar_30");
  npc_fleet->set_fleetid(61);
  npc_fleet->set_fleettype(game::DEPLOYEDFLEETTYPE_MARAUDER);
  (*npc->mutable_deployedfleets())[61] = *npc_fleet;
  return journal;
}
// Hand-encoded wire bytes, so the declared protobuf types are checked against what the game sends and not against
// the same schema twice.
template <typename Message> Message ParseWire(const std::string& bytes)
{
  Message message;
  Check(message.ParseFromString(bytes), "wire fixture parses");
  return message;
}
std::string Field(char tag, const std::string& bytes)
{ return std::string{tag, static_cast<char>(bytes.size())} + bytes; }
void WireTypes()
{
  // OfferDetails: validCount (3) and sameRemainingCount (6) are zigzag varints: 1 is -1 and 10 is 5.
  const auto details = ParseWire<content::OfferDetails>(std::string("\x18\x01\x30\x0a", 4));
  Check(details.validcount() == -1 && details.sameremainingcount() == 5, "offer counts are zigzag encoded");
  // Bundle: priority (3) and sortOrder (4) are fixed32.
  const auto bundle = ParseWire<content::Bundle>(std::string("\x1d\x07\x00\x00\x00\x25\x2a\x00\x00\x00", 10));
  Check(bundle.priority() == 7 && bundle.sortorder() == 42, "bundle priority and sort order are fixed32");
  // Cost.quantity (2) is fixed32.
  const auto cost = ParseWire<content::Cost>(std::string("\x15\x09\x00\x00\x00", 5));
  Check(cost.quantity() == 9, "cost quantity is fixed32");
  // Resource.amountUlong (6) is fixed64.
  const auto resource = ParseWire<content::Resource>(std::string("\x31\x00\x94\x35\x77\x00\x00\x00\x00", 9));
  Check(resource.amountulong() == 2000000000u, "resource amountUlong is fixed64");
  const auto profile = ParseWire<game::UserProfile>(std::string("\x1d\x28\x00\x00\x00\x25\x27\x00\x00\x00", 10));
  Check(profile.level() == 40 && profile.levellastawarded() == 39, "profile levels are fixed32");
}
void StoreOfferCounts()
{
  auto target = Target();
  // Two faction bundles whose offer details come from wire bytes: 1 is -1 (unlimited), 10 is 5.
  const auto bundle = [](int64_t id, const std::string& details) {
    content::Bundle message;
    message.set_bundleid(id);
    message.set_category("factions");
    auto* consumable = message.mutable_content()->mutable_static_()->add_items();
    consumable->set_id("500");
    consumable->set_type("consumable");
    return Field('\x0a', message.SerializeAsString() + Field('\x72', details));
  };
  const std::string response =
      bundle(20, std::string("\x18\x01\x30\x01", 4)) + bundle(10, std::string("\x18\x0a\x30\x0a", 4));
  ProcessStoreOffers({store.Generation(), response});
  const auto records = Send(target);
  Check(records.size() == 1 && records[0]["type"] == "store_offer" && records[0]["offers"].size() == 2,
        "one store offer record");
  const auto& offers = records[0]["offers"];
  Check(offers[0]["bid"] == 10 && offers[0]["valid_count"] == 5 && offers[0]["same_remaining_count"] == 5,
        "store offer counts are the decoded values");
  Check(offers[1]["bid"] == 20 && offers[1]["valid_count"] == -1 && offers[1]["same_remaining_count"] == -1,
        "an unlimited store offer is -1");
}
// Every pending upload, keyed by event type.
std::map<std::string, json> Drain(Delivery& target)
{
  std::map<std::string, json> uploads;
  while (auto upload = target.Next(store, now)) {
    uploads[upload->Type()] = upload->Records();
    target.Complete(std::move(*upload), true, store.Generation(), now);
  }
  return uploads;
}
void AddItem(content::ContentItemList* list, const char* id, const char* kind, int64_t quantity)
{
  auto* item = list->add_items();
  item->set_id(id);
  item->set_type(kind);
  item->set_quantity(quantity);
}
content::Bundle* AddBundle(content::Bundles& response, int64_t id, const char* category)
{
  auto* bundle = response.add_bundles_();
  bundle->set_bundleid(id);
  bundle->set_category(category);
  return bundle;
}
content::Bundles StoreResponse()
{
  content::Bundles response;
  // Out of bundle id order on purpose.
  auto* refine = AddBundle(response, 30, "refining_materials");
  refine->set_sortorder(7);
  for (uint32_t quantity = 1; quantity <= 3; ++quantity) {
    auto* cost = refine->add_cost();
    cost->set_quantity(quantity);
    auto* resource = cost->add_cost_();
    resource->set_id("4000000000");
    resource->set_amount(quantity * 1000);
  }
  for (const char* id : {"21", "22"}) {
    auto* list = refine->mutable_content()->add_dynamic();
    AddItem(list, id, "resource", 3);
    AddItem(list, "23", "resource", 9);
  }
  refine->mutable_offerdetails()->set_validcount(-1);
  refine->mutable_offerdetails()->set_sameremainingcount(-1);
  AddBundle(response, 10, "refining_materials");
  auto* parts = AddBundle(response, 5, "refining_ship_parts");
  parts->mutable_offerdetails()->set_validcount(5);
  parts->mutable_offerdetails()->mutable_validfromtimestamp()->set_seconds(1700000000);
  parts->mutable_offerdetails()->mutable_validuntiltimestamp()->set_seconds(0);
  parts->mutable_offerdetails()->set_cooldownconfigured(true);
  auto* games       = AddBundle(response, 8, "alliance_games");
  auto* requirement = games->add_requirements();
  requirement->set_type("alliance_league");
  requirement->set_id("3");
  AddItem(games->mutable_content()->mutable_static_(), "blueprint-1", "blueprint", 2);
  AddBundle(response, 9, "event_store");
  auto* faction = AddBundle(response, 40, "factions");
  AddItem(faction->mutable_content()->mutable_static_(), "500", "consumable", 1);
  faction->mutable_offerdetails()->set_validcount(2);
  return response;
}
void StoreBundles()
{
  auto target   = Target();
  auto response = StoreResponse();
  ProcessStoreBundles({store.Generation(), response.SerializeAsString()});
  ProcessStoreOffers({store.Generation(), response.SerializeAsString()});
  const auto uploads = Drain(target);
  Check(uploads.size() == 2 && uploads.contains("store_bundle") && uploads.contains("store_offer"),
        "store bundles and offers are separate events");
  const auto& events = uploads.at("store_bundle");
  Check(events.size() == 3, "one event per listed category; others are ignored");
  json by_category;
  for (const auto& event : events)
    by_category[event.at("category").get<std::string>()] = event;
  const auto& materials = by_category.at("refining_materials").at("bundles");
  Check(materials.size() == 2 && materials[0].at("bid") == 10 && materials[1].at("bid") == 30,
        "bundles are sorted by id");
  const json expected = json::parse(R"({"bid":30,"sort":7,
    "cost":[{"qty":1,"res":[{"rid":4000000000,"amount":1000}]},{"qty":2,"res":[{"rid":4000000000,"amount":2000}]},
            {"qty":3,"res":[{"rid":4000000000,"amount":3000}]}],
    "req":[],"dynamic":[[{"id":21,"kind":"resource","qty":3},{"id":23,"kind":"resource","qty":9}],
                         [{"id":22,"kind":"resource","qty":3},{"id":23,"kind":"resource","qty":9}]],
    "static":[],"uses_left":-1,"same_left":-1,"valid_from":null,"valid_until":null,"cooldown":false})");
  Check(materials[1] == expected, "a refining bundle keeps cost and dynamic order with -1 counts and null times");
  Check(materials[0].at("cost").empty() && materials[0].at("dynamic").empty() && materials[0].at("uses_left") == 0,
        "empty arrays are present");
  const auto& parts = by_category.at("refining_ship_parts").at("bundles");
  Check(parts.size() == 1 && parts[0].at("uses_left") == 5 && parts[0].at("valid_from") == 1700000000
            && parts[0].at("valid_until").is_null() && parts[0].at("cooldown") == true,
        "seconds stay seconds and a zero time is null");
  const auto& games = by_category.at("alliance_games").at("bundles");
  Check(games.size() == 1 && games[0].at("req") == json::parse(R"([{"type":"alliance_league","id":3,"amount":0}])")
            && games[0].at("static") == json::parse(R"([{"id":"blueprint-1","kind":"blueprint","qty":2}])"),
        "requirements and static contents");
  const auto& offers = uploads.at("store_offer");
  Check(offers.size() == 1
            && offers[0] == json::parse(R"({"type":"store_offer","category":"factions","offers":[{"bid":40,"cid":500,
        "cost":[],"req":[],"valid_count":2,"same_remaining_count":0}]})"),
        "store offers are unchanged");

  ProcessStoreBundles({store.Generation(), response.SerializeAsString()});
  Check(Drain(target).empty(), "an unchanged snapshot sends nothing");

  // A response with only one category changes only that category and deletes no other.
  content::Bundles partial;
  AddItem(AddBundle(partial, 8, "alliance_games")->mutable_content()->mutable_static_(), "blueprint-1", "blueprint", 3);
  ProcessStoreBundles({store.Generation(), partial.SerializeAsString()});
  const auto changed = Drain(target);
  Check(changed.size() == 1 && changed.at("store_bundle").size() == 1
            && changed.at("store_bundle")[0].at("category") == "alliance_games",
        "only the changed category is sent");
  Check(store.Contains(Stream::kStoreBundles, "refining_materials")
            && store.Contains(Stream::kStoreBundles, "refining_ship_parts"),
        "a response without a category keeps its last record");
  ProcessStoreBundles({store.Generation(), content::Bundles{}.SerializeAsString()});
  Check(Drain(target).empty() && store.Contains(Stream::kStoreBundles, "alliance_games"),
        "an empty response removes nothing");

  // A target that does not enable the stream receives nothing from it.
  auto only_offers          = SyncTargetConfig{Options()};
  only_offers.store_bundles = false;
  Delivery other;
  other.Configure(only_offers);
  const auto got = Drain(other);
  Check(got.size() == 1 && got.contains("store_offer"), "store_bundles is honoured per target");
}
void ProtobufJournals()
{
  const auto body      = BattleReportBody(SyntheticJournal());
  const auto converted = JournalFromProtobuf(body);
  Check(!IsJsonBody(body) && converted.has_value(), "a protobuf battle report converts");
  // dump() compares the JSON number kinds too: 5.0 must stay a float and the ids integers.
  const auto expected = json::parse(R"(
    {"battle_context_data":{"context_data":{}},"battle_duration":5,"battle_log":[-96,1234567890123456789,5.0,
    1.2000000000000002,-97],"battle_time":"2025-11-20T00:53:20","battle_type":5,
    "chest_drop":{"chest_id_refs":[{"art_file_reference":null,"loca_id":9}],"chests_gained":[{"count":1,
    "expiry_time":null,"params":{"id_refs":{"art_file_reference":null,"loca_id":9},"ref_id":77},"type":2}],
    "user_id_refs":[]},"coords":{"x":1.1,"y":-2.5},"id":2860000000000000001,"initiator_fired_first":false,
    "initiator_fleet_data":{"armada_owner_id":null,"battle_data_type":0,"bridge_officers":[{"id":41,"level":50,
    "rank":3},null,null,{"id":42,"level":40,"rank":2},null,null,null,null,null,null],"cargo_max":1000,
    "cargo_max_list":[],"defense_rating":null,"deployed_fleet":{"active_buffs":[],"attributes":{"-9":1234.6},
    "battle_opponent_fleet_id":61,"battle_won":true,"cause_of_destruction":-1,"defense_rating":1235,"fleet_id":31,
    "hull_ids":[11],"pursued_by_num":0,"pursuit_target_id":null,"ship_attributes":{},"ship_components":{"21":[101,
    -1]},"ship_cosmetics":{},"ship_dmg":{},"ship_hps":{"21":5000},"ship_ids":[21],"ship_level_percentages":{},
    "ship_levels":{"21":30},"ship_shield_dmg":{},"ship_shield_hps":{"21":2500},
    "ship_shield_total_regeneration_durations":{},"ship_stats":{"21":{"60":2500.4,"61":5000.0}},
    "ship_tiers":{"21":4},"stats":{"63":2.0,"64":16.7},"status_counters":[],"status_effects":{},"type":1,
    "uid":"player1","warp_data":null,"warp_distance":16.7,"warp_speed":2.0,"warp_time":null},
    "deployed_fleets":{"31":{"active_buffs":[],"attributes":{"-9":1234.6},"battle_opponent_fleet_id":61,
    "battle_won":true,"cause_of_destruction":-1,"defense_rating":1235,"fleet_id":31,"hull_ids":[11],
    "pursued_by_num":0,"pursuit_target_id":null,"ship_attributes":{},"ship_components":{"21":[101,-1]},
    "ship_cosmetics":{},"ship_dmg":{},"ship_hps":{"21":5000},"ship_ids":[21],"ship_level_percentages":{},
    "ship_levels":{"21":30},"ship_shield_dmg":{},"ship_shield_hps":{"21":2500},
    "ship_shield_total_regeneration_durations":{},"ship_stats":{"21":{"60":2500.4,"61":5000.0}},
    "ship_tiers":{"21":4},"stats":{"63":2.0,"64":16.7},"status_counters":[],"status_effects":{},"type":1,
    "uid":"player1","warp_data":null,"warp_distance":16.7,"warp_speed":2.0,"warp_time":null}},"final_cargo_list":[],
    "final_ship_hps":[],"final_ship_level_percentages":[],"final_ship_levels":[],"final_ship_shps":[],"fleet_id":31,
    "fleets_officers":{"31":[{"id":41,"level":50,"rank":3},null,null,{"id":42,"level":40,"rank":2},null,null,null,
    null,null,null]},"health_rating":null,"hull_ids":[11],"initial_cargo":{"resources":{}},"initial_cargo_list":[],
    "initial_ship_hps":[5000],"initial_ship_level_percentages":[0.3],"initial_ship_levels":[30],
    "initial_ship_shps":[],"max_ship_hps":[],"max_ship_shps":[],"num_drydocks":0,"offense_rating":null,
    "officer_rating":5,"ref_ids":null,"ship_ids":[21],"xp_gained":null},"initiator_id":"player1",
    "initiator_wins":true,"journal_src":"pb1","resources_dropped":{"2":100},"resources_transferred":{},
    "system_id":1234567890123,"target_fleet_data":{"armada_owner_id":null,"battle_data_type":0,"bridge_officers":{},
    "cargo_max":null,"cargo_max_list":[],"defense_rating":null,"deployed_fleet":{"active_buffs":[],"attributes":{},
    "battle_opponent_fleet_id":31,"battle_won":false,"cause_of_destruction":0,"fleet_id":61,"hull_ids":[],
    "pursued_by_num":0,"pursuit_target_id":null,"ship_attributes":{},"ship_components":{},"ship_cosmetics":{},
    "ship_dmg":{},"ship_hps":{},"ship_ids":[],"ship_level_percentages":{},"ship_levels":{},"ship_shield_dmg":{},
    "ship_shield_hps":{},"ship_shield_total_regeneration_durations":{},"ship_stats":{},"ship_tiers":{},"stats":{},
    "status_counters":[],"status_effects":{},"type":2,"uid":"mar_30","warp_data":null,"warp_time":null},
    "deployed_fleets":{"61":{"active_buffs":[],"attributes":{},"battle_opponent_fleet_id":31,"battle_won":false,
    "cause_of_destruction":0,"fleet_id":61,"hull_ids":[],"pursued_by_num":0,"pursuit_target_id":null,
    "ship_attributes":{},"ship_components":{},"ship_cosmetics":{},"ship_dmg":{},"ship_hps":{},"ship_ids":[],
    "ship_level_percentages":{},"ship_levels":{},"ship_shield_dmg":{},"ship_shield_hps":{},
    "ship_shield_total_regeneration_durations":{},"ship_stats":{},"ship_tiers":{},"stats":{},"status_counters":[],
    "status_effects":{},"type":2,"uid":"mar_30","warp_data":null,"warp_time":null}},"final_cargo_list":[],
    "final_ship_hps":[],"final_ship_level_percentages":[],"final_ship_levels":[],"final_ship_shps":[],"fleet_id":61,
    "fleets_officers":{},"health_rating":null,"hull_ids":[],"initial_cargo_list":[],"initial_ship_hps":[],
    "initial_ship_level_percentages":[],"initial_ship_levels":[],"initial_ship_shps":[],"max_ship_hps":[],
    "max_ship_shps":[],"num_drydocks":0,"offense_rating":null,"officer_rating":0,
    "ref_ids":{"art_file_reference":null,"loca_id":555},"ship_ids":[0],"xp_gained":null},"target_id":"mar_30"}
  )");
  Check(converted && converted->dump() == expected.dump(), "a protobuf journal keeps the JSON journal shape");

  Check(IsJsonBody(" \n{\"journal\":{}}"), "a JSON body keeps the JSON path");
  Check(!JournalFromProtobuf(R"({"journal":{}})"), "a JSON body is not read as protobuf");
  Check(!JournalFromProtobuf(""), "an empty body is refused");
  Check(!JournalFromProtobuf("\xff\xff\xff\xff garbage"), "garbage is refused");
  Check(!JournalFromProtobuf(body.substr(0, body.size() / 2)), "a truncated body is refused");
  Check(!JournalFromProtobuf(BattleReportBody(SyntheticJournal(), game::EntityGroup::TYPE_BATTLERESULTHEADERS)),
        "a body without a battle report group is refused");
  Check(!JournalFromProtobuf(BattleReportBody(game::JournalResponse{})), "a journal without an id is refused");
}
} // namespace

// Stand-ins for config.cc, sync.cc, battlelogs.cc and fleets.cc, which this test does not link: no threads, no game,
// one store.
std::shared_ptr<const Config::Sync> Config::Sync::Get()
{ return settings; }

namespace data_sync
{
void Publish(Stream stream, RecordSet records, uint64_t generation, Update mode)
{
  if (generation == store.Generation())
    store.Apply(stream, std::move(records), mode);
}
void QueueNewBattles(const std::vector<uint64_t>&, uint64_t) {}
void CachePlayerNames(const Input&) {}
void CacheAllianceNames(const Input&) {}
void RereadFleet(int64_t fleet_id)
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
  run("slot transports", SlotTransports);
  run("away and emerald", AwayAndEmerald);
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
  run("retired target", RetiredTarget);
  run("battle bound", OnlyBattlesArePruned);
  run("category gating", UnwantedCategories);
  run("queues", Queues);
  run("name caches", NameCaches);
  run("logging", SyncLogging);
  run("protobuf journals", ProtobufJournals);
  run("wire types", WireTypes);
  run("store offer counts", StoreOfferCounts);
  run("store bundles", StoreBundles);
  if (!failures)
    std::cout << "Sync wire and delivery tests passed\n";
  return failures ? 1 : 0;
}

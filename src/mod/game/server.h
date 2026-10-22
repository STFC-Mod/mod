#pragma once

// The game server (Digit.PrimeServer): its sessions, the entity groups and realtime messages it sends, and the
// server models and services the mod reads, such as battle results, hull specs, buffs and events.

#include "app.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct ByteString : il2cpp::Object {
  using Class = il2cpp::Class<"Google.Protobuf", "Google.Protobuf", "ByteString">;

  il2cpp::Array<uint8_t>* bytes()
  { return Field<"bytes", il2cpp::Array<uint8_t>*>(); }
  int32_t Length()
  { return Get<"Length", int32_t>(); }
};

struct EntityGroup : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "EntityGroup">;

  // The payload type, which also says which protobuf message the group's bytes hold.
  enum class Kind : int32_t {
    UserProfiles                             = 0,   // 0x00000000
    HullSpecs                                = 1,   // 0x00000001
    ResourceSpecs                            = 2,   // 0x00000002
    ResourceConversionSpecs                  = 3,   // 0x00000003
    JobSpeedupResourceSpecs                  = 4,   // 0x00000004
    StarbaseSpecs                            = 5,   // 0x00000005
    OfficerSpecs                             = 7,   // 0x00000007
    FactionSpecs                             = 8,   // 0x00000008
    FactionBehaviourSpecs                    = 9,   // 0x00000009
    UserConsumableSpecs                      = 10,  // 0x0000000A
    PlayerXpSpecs                            = 11,  // 0x0000000B
    ComponentSpecs                           = 12,  // 0x0000000C
    ObjectiveDefinitions                     = 13,  // 0x0000000D
    AllianceRankSpecs                        = 14,  // 0x0000000E
    AllianceLevelSpecs                       = 15,  // 0x0000000F
    AlliancePermissionSpecs                  = 16,  // 0x00000010
    OfficerAbilityBuffSpecs                  = 17,  // 0x00000011
    OfficerCoreStatSpecs                     = 18,  // 0x00000012
    OfficerIntelRequirementSpecs             = 19,  // 0x00000013
    OfficerSynergyFactorSpecs                = 20,  // 0x00000014
    BlueprintSpecs                           = 21,  // 0x00000015
    NavigationConfig                         = 22,  // 0x00000016
    FleetConfig                              = 23,  // 0x00000017
    FleetIconConfig                          = 24,  // 0x00000018
    AllianceConfig                           = 25,  // 0x00000019
    ConsistencyConfig                        = 26,  // 0x0000001A
    FtueConfig                               = 27,  // 0x0000001B
    PlacementConfig                          = 28,  // 0x0000001C
    DialogConfig                             = 29,  // 0x0000001D
    FactionConfig                            = 30,  // 0x0000001E
    ResourceConfig                           = 31,  // 0x0000001F
    FtueProgressionConfig                    = 32,  // 0x00000020
    NewPlayerConfig                          = 33,  // 0x00000021
    ThreatConfig                             = 34,  // 0x00000022
    StationShieldConfig                      = 35,  // 0x00000023
    PlanetSlotsConfig                        = 36,  // 0x00000024
    OfficerConfig                            = 37,  // 0x00000025
    BattleConfig                             = 38,  // 0x00000026
    StarbaseConfig                           = 39,  // 0x00000027
    ShipXpConfig                             = 40,  // 0x00000028
    OptimisedGalaxy                          = 41,  // 0x00000029
    Json                                     = 42,  // 0x0000002A
    Officers                                 = 43,  // 0x0000002B
    OfficerCoreStatThresholdsSpecs           = 44,  // 0x0000002C
    OfficerPromotionSpecs                    = 45,  // 0x0000002D
    PlayerInventories                        = 46,  // 0x0000002E
    Notifications                            = 47,  // 0x0000002F
    ClientShipStatLookupSpecs                = 48,  // 0x00000030
    BaseShipTierSpecs                        = 49,  // 0x00000031
    ShipTierSpecs                            = 50,  // 0x00000032
    ShipBonusBuffSpecs                       = 51,  // 0x00000033
    MitigationCapsSpecs                      = 52,  // 0x00000034
    GlobalDamageReductionConfig              = 53,  // 0x00000035
    BuffTargetSpecs                          = 54,  // 0x00000036
    BuffTriggerSpecs                         = 55,  // 0x00000037
    Jobs                                     = 56,  // 0x00000038
    StarbaseDetailedScan                     = 57,  // 0x00000039
    MissionSpecs                             = 58,  // 0x0000003A
    AvailableMissions                        = 59,  // 0x0000003B
    NodeMissions                             = 60,  // 0x0000003C
    ActiveMissions                           = 61,  // 0x0000003D
    ActionSpecs                              = 62,  // 0x0000003E
    AllianceMembersStarbasesLocations        = 63,  // 0x0000003F
    CompletedMissions                        = 64,  // 0x00000040
    ShipLevelUpBonusBuffsSpecs               = 65,  // 0x00000041
    ResearchSpecs                            = 66,  // 0x00000042
    ResearchTreesState                       = 67,  // 0x00000043
    StarbaseBuffs                            = 68,  // 0x00000044
    GlobalActiveBuffs                        = 69,  // 0x00000045
    Requirements                             = 70,  // 0x00000046
    AllianceProfiles                         = 71,  // 0x00000047
    AllianceInvites                          = 72,  // 0x00000048
    AllianceLeaderInvites                    = 73,  // 0x00000049
    PvpBanding                               = 74,  // 0x0000004A
    UserTemplates                            = 75,  // 0x0000004B
    ArmadaAttack                             = 76,  // 0x0000004C
    ArmadaAttackSpecs                        = 77,  // 0x0000004D
    ArmadaAttackSystemList                   = 78,  // 0x0000004E
    ArmadaAttackUserList                     = 79,  // 0x0000004F
    ArmadaConfig                             = 80,  // 0x00000050
    ArmadaAttackAllianceAttackingList        = 81,  // 0x00000051
    ArmadaAttackIncomingThreatList           = 82,  // 0x00000052
    ServerTransferConfig                     = 83,  // 0x00000053
    MiningSetupConfig                        = 84,  // 0x00000054
    ScrapyardSpecs                           = 85,  // 0x00000055
    ScrapyardJob                             = 86,  // 0x00000056
    CosmeticSpecs                            = 87,  // 0x00000057
    Ceasefire                                = 88,  // 0x00000058
    SetAllianceDiplomacy                     = 89,  // 0x00000059
    ArmadaPveSpecs                           = 90,  // 0x0000005A
    ToolingLootRoll                          = 91,  // 0x0000005B
    ToolingRespawnTimes                      = 92,  // 0x0000005C
    ArmadaEnRouteInfoList                    = 93,  // 0x0000005D
    FleetRepairCosts                         = 94,  // 0x0000005E
    PrestigeData                             = 95,  // 0x0000005F
    TerritoryStaticData                      = 96,  // 0x00000060
    TerritoryAllOwners                       = 97,  // 0x00000061
    TerritoryOwner                           = 98,  // 0x00000062
    TerritoryAllTakeovers                    = 99,  // 0x00000063
    TerritoryTakeover                        = 100, // 0x00000064
    TerritoryTakeoverCanJoin                 = 101, // 0x00000065
    AllianceGetBankResources                 = 102, // 0x00000066
    TerritoryActiveServices                  = 103, // 0x00000067
    TerritoryServiceSlots                    = 104, // 0x00000068
    TerritoryAllianceSlots                   = 105, // 0x00000069
    TerritoryCanActivateService              = 106, // 0x0000006A
    WorkerSpecs                              = 107, // 0x0000006B
    BatchAttributeResponse                   = 108, // 0x0000006C
    BuffsGetAttribute                        = 109, // 0x0000006D
    AwayAssignmentsStatic                    = 110, // 0x0000006E
    AwayAssignmentsList                      = 111, // 0x0000006F
    AwayAssignmentsParameter                 = 112, // 0x00000070
    AwayAssignmentsInstance                  = 113, // 0x00000071
    ConsumableSpecs                          = 114, // 0x00000072
    SlotSpecs                                = 115, // 0x00000073
    ConsumableBuffs                          = 116, // 0x00000074
    EntitySlots                              = 117, // 0x00000075
    TraitsSpecs                              = 118, // 0x00000076
    OfficerTraitsSpecs                       = 119, // 0x00000077
    ActiveOfficerTraits                      = 120, // 0x00000078
    EntitySlotsData                          = 121, // 0x00000079
    LoyaltySpecs                             = 122, // 0x0000007A
    PeaceShieldRulesSpecs                    = 123, // 0x0000007B
    MarauderInfo                             = 124, // 0x0000007C
    AllianceStarbaseConfig                   = 125, // 0x0000007D
    StarbaseService                          = 126, // 0x0000007E
    Gameworld                                = 127, // 0x0000007F
    ActivatedAbilitySpecs                    = 128, // 0x00000080
    AchievementsConfig                       = 129, // 0x00000081
    ArmadaPvpSpecs                           = 130, // 0x00000082
    OfficerProgressRewardSpecs               = 131, // 0x00000083
    CommanderSkillSpecs                      = 132, // 0x00000084
    CommanderIntelRequirementSpecs           = 133, // 0x00000085
    HailingFreqConfig                        = 134, // 0x00000086
    OfficerLevelRewardsSpecs                 = 135, // 0x00000087
    ResourceGroupsSpec                       = 136, // 0x00000088
    ChallengeLadderSpecs                     = 137, // 0x00000089
    BundleRewardsSpecs                       = 138, // 0x0000008A
    ForbiddenTechSpecs                       = 139, // 0x0000008B
    ForbiddenTechs                           = 140, // 0x0000008C
    ForbiddenTechBuffs                       = 141, // 0x0000008D
    ForbiddenTechRemovalCosts                = 142, // 0x0000008E
    ForbiddenTechInstance                    = 143, // 0x0000008F
    ForbiddenTechChances                     = 144, // 0x00000090
    ForbiddenTechConfig                      = 145, // 0x00000091
    ChallengeConfig                          = 146, // 0x00000092
    ForbiddenTechUpgradeCosts                = 147, // 0x00000093
    HazardSpecs                              = 148, // 0x00000094
    ActivatedShipAbilitiesConfigs            = 149, // 0x00000095
    WaveDefenseStaticData                    = 150, // 0x00000096
    WaveDefensePartyData                     = 151, // 0x00000097
    WaveDefenseSyncData                      = 152, // 0x00000098
    WaveDefenseChallengeData                 = 153, // 0x00000099
    UserProfileSettings                      = 154, // 0x0000009A
    ActiveWormholes                          = 155, // 0x0000009B
    MirrorUniversePortalData                 = 156, // 0x0000009C
    MirrorUniverseDuration                   = 157, // 0x0000009D
    CheckAccountResponse                     = 158, // 0x0000009E
    AllianceLoyaltyStaticData                = 159, // 0x0000009F
    AllianceGetGameProperties                = 160, // 0x000000A0
    LoyaltyTierRewards                       = 161, // 0x000000A1
    GameActivityRanksData                    = 162, // 0x000000A2
    GameActivity                             = 163, // 0x000000A3
    GameActivitySpecs                        = 164, // 0x000000A4
    GameActivityParticipantSpecs             = 165, // 0x000000A5
    GameActivityDetailedSpec                 = 166, // 0x000000A6
    GameActivityScheduleSpec                 = 167, // 0x000000A7
    AllianceParties                          = 168, // 0x000000A8
    PlayerParty                              = 169, // 0x000000A9
    Party                                    = 200, // 0x000000C8
    CreatedParty                             = 201, // 0x000000C9
    PartySpecs                               = 202, // 0x000000CA
    ResourceAutoConvertSpecs                 = 203, // 0x000000CB
    VisitedSystems                           = 204, // 0x000000CC
    LeasedQueueSpecs                         = 205, // 0x000000CD
    LeasedQueueRecord                        = 206, // 0x000000CE
    AllianceTagSpecs                         = 207, // 0x000000CF
    GameActivityRewardsSpec                  = 208, // 0x000000D0
    GameActivityIndex                        = 209, // 0x000000D1
    AllianceApplication                      = 210, // 0x000000D2
    ScrapyardRewards                         = 211, // 0x000000D3
    FactionStanding                          = 212, // 0x000000D4
    ArmadaAttackUserListSyncResponse         = 213, // 0x000000D5
    ArmadaAttackGetFromAllianceResponse      = 214, // 0x000000D6
    ArmadaAttackIncomingThreatListResponse   = 215, // 0x000000D7
    CrossAllianceArmadasFromRegionalResponse = 216, // 0x000000D8
    PlayerPartyResponse                      = 217, // 0x000000D9
    UserHistory                              = 218, // 0x000000DA
    BattleResultHeaders                      = 219, // 0x000000DB
    HazardResultHeaders                      = 220, // 0x000000DC
    BattleReport                             = 221, // 0x000000DD
    ActivatedAbilityData                     = 222, // 0x000000DE
    MySkillData                              = 223, // 0x000000DF
    CheatConfig                              = 224, // 0x000000E0
    AllianceDetails                          = 225, // 0x000000E1
    AllianceMembers                          = 226, // 0x000000E2
    AllianceMember                           = 227, // 0x000000E3
    AllianceDiplomacy                        = 228, // 0x000000E4
    AllianceContributions                    = 229, // 0x000000E5
    AllianceMemberActivity                   = 230, // 0x000000E6
    AllianceJobHelpInfo                      = 231, // 0x000000E7
    AllianceRankChange                       = 232, // 0x000000E8
    AllianceCreateApplication                = 233, // 0x000000E9
    Resources                                = 234, // 0x000000EA
    ResourcesDelta                           = 235, // 0x000000EB
    ResourcesDeltaAlliance                   = 236, // 0x000000EC
    OpenedChests                             = 237, // 0x000000ED
    MyShieldState                            = 238, // 0x000000EE
    StarbaseModules                          = 239, // 0x000000EF
    ResourceProducers                        = 240, // 0x000000F0
    Starbase                                 = 241, // 0x000000F1

    NodeSystem            = 242, // 0x000000F2
    DockingPoints         = 243, // 0x000000F3
    PlayerContainer       = 244, // 0x000000F4
    MarauderQuickScanData = 245, // 0x000000F5
    AllianceContainer     = 246, // 0x000000F6
    QuickScanResults      = 247, // 0x000000F7
    DeployedFleets        = 248, // 0x000000F8
    MyDeployedFleets      = 249, // 0x000000F9
    Fleets                = 250, // 0x000000FA
    Ships                 = 251, // 0x000000FB
    ResourceHarvesters    = 252, // 0x000000FC

    PlanetaryBaseData            = 1213, // 0x000004BD
    PlanetaryResourceProducers   = 1214, // 0x000004BE
    PlanetaryMapData             = 1215, // 0x000004BF
    PlanetaryMapProsperity       = 1216, // 0x000004C0
    PlanetaryMapLock             = 1217, // 0x000004C1
    PlanetaryMapClearablePoints  = 1218, // 0x000004C2
    PlanetaryMapResponse         = 1219, // 0x000004C3
    PlanetaryHelpResponse        = 1220, // 0x000004C4
    PlanetaryHelpRecordsResponse = 1221, // 0x000004C5
    PlanetaryHelpData            = 1222, // 0x000004C6
    PlanetaryStaticSpecs         = 1223, // 0x000004C7

    OutpostStaticData      = 8401, // 0x000020D1
    OutpostUpgradeResponse = 8402, // 0x000020D2
    OutpostSync            = 8403, // 0x000020D3
    OutpostDefaultResponse = 8404, // 0x000020D4
    OutpostStaticSpecs     = 8405, // 0x000020D5

    GalacticAnomalyBuffSpecs = 8501, // 0x00002135
    DynamicGalaxyDataSync    = 8502, // 0x00002136

    Tracker              = 9301,  // 0x00002455
    CrossAllianceArmadas = 87001, // 0x000153D9
    ResearchConfig       = 91100, // 0x000163DC
  };

  Kind Type()
  { return Get<"Type", Kind>(); }
  ByteString* Group()
  { return Get<"Group", ByteString*>(); }
};

struct ServiceResponse : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ServiceResponse">;

  // Google.Protobuf.Collections.RepeatedField<EntityGroup>; List resolves Count/get_Item on it.
  il2cpp::List<EntityGroup*>* EntityGroups()
  { return Get<"EntityGroups", il2cpp::List<EntityGroup*>*>(); }
};

// The platform service's responses (store offers among them), Digit.Platform.Models. They pass through the same
// model registry code as game server responses, but their entity groups have a type enum of their own.
struct PlatformEntityGroup : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Platform.Models", "EntityGroup">;

  enum class Kind : int32_t {
    StaticTranslations             = 0,  // 0x00000000
    DynamicTranslations            = 1,  // 0x00000001
    StoreOffers                    = 2,  // 0x00000002
    OrderVirtualGood               = 3,  // 0x00000003
    Json                           = 4,  // 0x00000004
    TournamentBundles              = 5,  // 0x00000005
    EventRanking                   = 6,  // 0x00000006
    EventsModel                    = 7,  // 0x00000007
    LeaderboardRanking             = 8,  // 0x00000008
    LeaderboardsModel              = 9,  // 0x00000009
    EventNotification              = 10, // 0x0000000A
    PlayerStats                    = 11, // 0x0000000B
    GameNews                       = 12, // 0x0000000C
    FileSignedUrls                 = 13, // 0x0000000D
    GameInfo                       = 14, // 0x0000000E
    PlayerProfiles                 = 15, // 0x0000000F
    PlayerlifecycleNotification    = 16, // 0x00000010
    MinigamesList                  = 17, // 0x00000011
    GameNewsVideo                  = 18, // 0x00000012
    PromoCodeRedeem                = 19, // 0x00000013
    StoreGroups                    = 20, // 0x00000014
    AllianceGameState              = 21, // 0x00000015
    AllianceGameChangeNotification = 22, // 0x00000016
    ApmFiltersStatus               = 23, // 0x00000017
    ApmFiltersUpdated              = 24, // 0x00000018
    ListReferralConnections        = 25, // 0x00000019
    RewardRedeem                   = 26, // 0x0000001A
    LoginStreakRecalculate         = 27, // 0x0000001B
    OrdersVirtualGood              = 28, // 0x0000001C
  };

  Kind Type()
  { return Get<"Type", Kind>(); }
  ByteString* Group()
  { return Get<"Group", ByteString*>(); }
};

struct PlatformServiceResponse : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Platform.Models", "ServiceResponse">;

  il2cpp::List<PlatformEntityGroup*>* EntityGroups()
  { return Get<"EntityGroups", il2cpp::List<PlatformEntityGroup*>*>(); }
};

// Hands each server response's entity groups to the client's models.
using GameServerModelRegistry =
    il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Core", "GameServerModelRegistry">;
// The client's session with the game server.
using GameServer = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Core", "GameServer">;

// Realtime (Centrifugo) messages that change an entity slot.
using SlotAssignRtcParser =
    il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Parsers", "SlotAssignRtcParser">;
using SlotClearRtcParser =
    il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Parsers", "SlotClearRtcParser">;

// Digit.Networking.RTC.CentrifugoInfo, a value type passed by value: a hook must keep it by value so the arguments
// after it keep their ABI positions.
struct CentrifugoInfo {
  Il2CppString* Channel;
  int32_t       Offset;
};
static_assert(sizeof(CentrifugoInfo) == 16);

struct RealtimeDataPayload : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Networking.RTC", "RealtimeDataPayload">;

  // The DataType enum as a number in a string: "0" is JSON.
  Il2CppString* DataType()
  { return Get<"DataType", Il2CppString*>(); }
  Il2CppString* Target()
  { return Get<"Target", Il2CppString*>(); }
  Il2CppString* Data()
  { return Get<"Data", Il2CppString*>(); }
};

enum class HullType : int32_t {
  Any          = -1,
  Destroyer    = 0,
  Survey       = 1,
  Explorer     = 2,
  Battleship   = 3,
  Defense      = 4,
  ArmadaTarget = 5,
};

struct IdRefs : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "IdRefs">;

  Il2CppString* locaStringId()
  { return Field<"_locaStringId", Il2CppString*>(); }
  int64_t locaId()
  { return Field<"locaId_", int64_t>(); }
};

struct HullSpec : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "HullSpec">;

  int64_t Id()
  { return Field<"id_", int64_t>(); }
  // The internal key, such as "Hull_L30_Destroyer_Klingon_LIVE".
  Il2CppString* name()
  { return Field<"name_", Il2CppString*>(); }
  HullType Type()
  { return Get<"Type", HullType>(); }
  IdRefs* idRefs()
  { return Field<"idRefs_", IdRefs*>(); }
};

// IShipModel, read on each ship's runtime class.
struct ShipModel : il2cpp::Object {
  HullSpec* Hull()
  { return Get<"Hull", HullSpec*>(); }
  float HullDamage()
  { return Get<"HullDamage", float>(); }
};

struct FleetService : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "FleetService">;

  ShipModel* GetShip(int64_t ship_id)
  { return Call<"GetShip", ShipModel*>(ship_id); }
};

struct SpecService : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "SpecService">;

  HullSpec* GetHull(int64_t hullId)
  { return Call<"GetHull", HullSpec*>(hullId); }
};

struct UserProfile : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "UserProfile">;

  // NPC and marauder profiles have no name; the game names them by this localization id.
  int64_t locaId()
  { return Field<"_locaId", int64_t>(); }
  Il2CppString* name()
  { return Field<"name_", Il2CppString*>(); }
  Il2CppString* UserId()
  { return Get<"UserId", Il2CppString*>(); }
  int32_t Level()
  { return Get<"Level", int32_t>(); }
};

struct UserProfileManager : MonoSingleton<UserProfileManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.PlayerProfile", "UserProfileManager">;

  UserProfile* GetLocalUserProfile()
  { return Call<"GetLocalUserProfile", UserProfile*>(); }
};

enum class BattleType : int32_t {
  Fleet                       = 0,
  Base                        = 1,
  PassiveMarauder             = 2,
  NpcInstantiated             = 3,
  DockingPoint                = 4,
  ActiveMarauder_MarauderInit = 5,
  ActiveMarauder_PlayerInit   = 6,
  ArmadaBase                  = 7,
  ArmadaMarauder              = 8,
  PveDockingPoint             = 9,
  ArmadaAsb                   = 10,
  ArmadaMta                   = 11,
  Hazard                      = 12,
  PveCuttingBeam              = 13,
  PvpCuttingBeam              = 14,
  PveChainShot                = 15,
  PvpChainShot                = 16,
};

// What a battle toast's Data is.
struct BattleResultHeader : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BattleResultHeader">;

  // The profiles are read untyped and treated as UserProfile, as the game does.
  UserProfile* PlayerUserProfile()
  { return static_cast<UserProfile*>(Get<"PlayerUserProfile", Il2CppObject*>()); }
  UserProfile* EnemyUserProfile()
  { return static_cast<UserProfile*>(Get<"EnemyUserProfile", Il2CppObject*>()); }
  SpecService* specService()
  { return static_cast<SpecService*>(Field<"_specService", Il2CppObject*>()); }
  // The BattleType property; named Type so it does not hide the enum.
  ::BattleType Type()
  { return Get<"BattleType", ::BattleType>(); }
  int64_t PlayerShipHullId()
  { return Get<"PlayerShipHullId", int64_t>(); }
  int64_t EnemyShipHullId()
  { return Get<"EnemyShipHullId", int64_t>(); }
};

// What an ArmadaCreated toast's Data is. These classes are read on their runtime classes: the toast payload types
// have moved between client builds, and the fields below are all the mod needs.
struct ArmadaCreatedData : il2cpp::Object {
  struct Profile : il2cpp::Object {
    Il2CppString* name()
    { return Field<"name_", Il2CppString*>(); }
    int64_t locaId()
    { return Field<"_locaId", int64_t>(); }
  };

  struct Attack : il2cpp::Object {
    Profile* Owner()
    { return Field<"<Owner>k__BackingField", Profile*>(); }
    Profile* TargetUserProfile()
    { return Field<"<TargetUserProfile>k__BackingField", Profile*>(); }
    int32_t TargetLevel()
    { return Field<"<TargetLevel>k__BackingField", int32_t>(); }
  };

  struct AllianceProfile : il2cpp::Object {
    Il2CppString* tag()
    { return Field<"tag_", Il2CppString*>(); }
  };

  Attack* ArmadaAttack()
  { return Field<"ArmadaAttack", Attack*>(); }
  AllianceProfile* Alliance()
  { return Field<"Alliance", AllianceProfile*>(); }
};

// Digit.PrimePlatform.Models.EventCategories, a struct around one int32. The binding layer checks the kind, so it
// is read as the struct, not as an int.
struct EventCategories {
  int32_t value;
};

// What an event toast's Data is.
struct EventModel : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimePlatform.Models", "EventModel">;

  int32_t category()
  { return Field<"category_", EventCategories>().value; }
};

struct ResourceSpec : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ResourceSpec">;

  // Such as "Resource_FactionPoint_Federation".
  Il2CppString* resourceId()
  { return Field<"resourceId_", Il2CppString*>(); }
};

struct ForbiddenTech : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ForbiddenTech">;

  int64_t Id()
  { return Get<"Id", int64_t>(); }
};

struct ForbiddenTechSlotParams : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ForbiddenTechSlotParams">;

  // The ship the tech is equipped on, whether or not it has a dock.
  int64_t OwnerId()
  { return Get<"OwnerId", int64_t>(); }
};

struct InventoryItem : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "InventoryItem">;

  ResourceSpec* resourceSpec()
  { return Field<"_resourceSpec", ResourceSpec*>(); }
  ForbiddenTech* forbiddenTech()
  { return Get<"ForbiddenTech", ForbiddenTech*>(); }
};

// The context of a cargo bar.
using CargoProgressData =
    il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CargoProgressData">;

// Works out which buffs apply. The hooks only pass the buff types through.
using BuffService = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "BuffService">;

struct IBuffData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit", "IBuffData">;
};

struct IBuffComparer : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "IBuffComparer">;
};

// Digit.PrimeServer.Models.ClientModifierType: which stat a buff modifies. The mod passes it through unread.
enum class ClientModifierType : int32_t {};

// Digit.PrimeServer.Models.BuffCondition; only the value the mod tests is named.
enum class BuffCondition : int32_t {
  CondSelfAtStation = 114,
};

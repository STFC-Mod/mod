#pragma once

// Server payloads: entity groups, realtime data, battle results, and buff data.

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

  enum Type : int32_t {
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

  Type Type_()
  { return Get<"Type", Type>(); }
  ByteString* Group()
  { return Get<"Group", ByteString*>(); }
};

struct ServiceResponse : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ServiceResponse">;

  // Google.Protobuf.Collections.RepeatedField<EntityGroup>; List resolves Count/get_Item on it.
  il2cpp::List<EntityGroup*>* EntityGroups()
  { return Get<"EntityGroups", il2cpp::List<EntityGroup*>*>(); }
};

// The platform service's responses, Digit.Platform.Models. They pass through the same model registry code as game
// server responses, but their entity groups have their own type enum.
struct PlatformEntityGroup : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Platform.Models", "EntityGroup">;

  enum Type : int32_t {
    StaticTranslations             = 0,
    DynamicTranslations            = 1,
    StoreOffers                    = 2,
    OrderVirtualGood               = 3,
    Json                           = 4,
    TournamentBundles              = 5,
    EventRanking                   = 6,
    EventsModel                    = 7,
    LeaderboardRanking             = 8,
    LeaderboardsModel              = 9,
    EventNotification              = 10,
    PlayerStats                    = 11,
    GameNews                       = 12,
    FileSignedUrls                 = 13,
    GameInfo                       = 14,
    PlayerProfiles                 = 15,
    PlayerlifecycleNotification    = 16,
    MinigamesList                  = 17,
    GameNewsVideo                  = 18,
    PromoCodeRedeem                = 19,
    StoreGroups                    = 20,
    AllianceGameState              = 21,
    AllianceGameChangeNotification = 22,
    ApmFiltersStatus               = 23,
    ApmFiltersUpdated              = 24,
    ListReferralConnections        = 25,
    RewardRedeem                   = 26,
    LoginStreakRecalculate         = 27,
    OrdersVirtualGood              = 28,
  };

  Type Type_()
  { return Get<"Type", Type>(); }
  ByteString* Group()
  { return Get<"Group", ByteString*>(); }
};

struct PlatformServiceResponse : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Platform.Models", "ServiceResponse">;

  il2cpp::List<PlatformEntityGroup*>* EntityGroups()
  { return Get<"EntityGroups", il2cpp::List<PlatformEntityGroup*>*>(); }
};

enum DataType : int32_t
{
  Unspecified = -1,
  JSON = 0,
  MsgPack = 1,
  ProtoBuff = 2,
  UrlEncoded = 3,
};

struct RealtimeDataPayload : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Networking.RTC", "RealtimeDataPayload">;

  // Managed DataType is a string, not the DataType enum above.
  Il2CppString* DataType()
  { return Get<"DataType", Il2CppString*>(); }
  Il2CppString* ChannelId()
  { return Get<"ChannelId", Il2CppString*>(); }
  int32_t InstanceIdJson()
  { return Get<"InstanceIdJson", int32_t>(); }
  Il2CppString* Target()
  { return Get<"Target", Il2CppString*>(); }
  Il2CppString* Source()
  { return Get<"Source", Il2CppString*>(); }
  Il2CppString* Data()
  { return Get<"Data", Il2CppString*>(); }
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
  PvpChainShot                = 16
};

enum class BattleResultType : int32_t {
  Defeat         = 0,
  Victory        = 1,
  PartialVictory = 2
};

enum class FleetDataType : int32_t {
  DeployedFleet = 0,
  Starbase      = 1,
  Armada        = 2
};

struct BattleResultHeader : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "BattleResultHeader">;

  Il2CppObject* get_PlayerUserProfile()
  { return Get<"PlayerUserProfile", Il2CppObject*>(); }
  Il2CppObject* get_EnemyUserProfile()
  { return Get<"EnemyUserProfile", Il2CppObject*>(); }
  Il2CppObject* get_SpecService()
  { return Field<"_specService", Il2CppObject*>(); }
  int32_t get_BattleType()
  { return Get<"BattleType", int32_t>(); }

  int64_t PlayerShipHullId()
  { return Get<"PlayerShipHullId", int64_t>(); }
  int64_t EnemyShipHullId()
  { return Get<"EnemyShipHullId", int64_t>(); }
};

enum class ClientModifierType : int32_t {
  Invalid                                 = -0x7FFF,
  FleetOfficerBonusHealth                 = -17,
  FleetOfficerBonusDefense                = -16,
  FleetOfficerBonusAttack                 = -15,
  FleetStrength                           = -14,
  FleetOfficerRating                      = -13,
  FleetSensorRating                       = -12,
  FleetDeflectorRating                    = -11,
  FleetHealthRating                       = -10,
  FleetDefenseRating                      = -9,
  FleetOffenseRating                      = -8,
  FleetGrade                              = -7,
  ShipShieldDamage                        = -6,
  ShipHullDamage                          = -5,
  FleetMiningBonus                        = -4,
  ShipPlating                             = -3,
  ShipAbsorption                          = -2,
  ShipDamagePerRound                      = -1,
  ModEnergyDamage                         = 0,
  ModKineticDamage                        = 1,
  ModAllDamage                            = 2,
  ModShotsPerAttack                       = 3,
  ModAllLoadSpeed                         = 4,
  ModAllReloadSpeed                       = 5,
  ModAccuracy                             = 6,
  ModArmorPiercing                        = 7,
  ModShieldPiercing                       = 8,
  ModCritChance                           = 9,
  ModCritDamage                           = 10,
  ModShipDodge                            = 11,
  ModShipArmor                            = 12,
  ModShields                              = 13,
  ModCptManeuverEffect                    = 14,
  ModOffAbilityEffect                     = 15,
  ModShieldHpRepair                       = 16,
  ModHullHpRepair                         = 17,
  ModAddState                             = 18,
  ModRemoveState                          = 19,
  ModParsteelProtection                   = 20,
  ModTritaniumProtection                  = 21,
  ModDilithiumProtection                  = 22,
  ModCombatParsteelReward                 = 23,
  ModCombatTritaniumReward                = 24,
  ModCombatDilithiumReward                = 25,
  ModCombatIntelReward                    = 26,
  ModCombatLootDropChance                 = 27,
  ModCombatLootDropQuantity               = 28,
  ModCombatXpReward                       = 29,
  ModMissionsParsteelReward               = 30,
  ModMissionsTritaniumReward              = 31,
  ModMissionsDilithiumReward              = 32,
  ModMissionsIntelReward                  = 33,
  ModMissionsLootDropChance               = 34,
  ModMissionsLootDropQuantity             = 35,
  ModMissionsXpReward                     = 36,
  ModFactionPointsFederationReward        = 37,
  ModFactionPointsKlingonReward           = 38,
  ModFactionPointsRomulanReward           = 39,
  ModFactionPointsAllReward               = 40,
  ModMiningRateParsteel                   = 41,
  ModMiningRateTritanium                  = 42,
  ModMiningRateDilithium                  = 43,
  ModMiningRateOre                        = 44,
  ModMiningRateHydrocarbon                = 45,
  ModMiningRewardParsteel                 = 46,
  ModMiningRewardTritanium                = 47,
  ModMiningRewardDilithium                = 48,
  ModMiningRewardOre                      = 49,
  ModMiningRewardHydrocarbon              = 50,
  ModRepairCostsParsteel                  = 51,
  ModRepairCostsTritanium                 = 52,
  ModRepairCostsDilithium                 = 53,
  ModRepairCostsAll                       = 54,
  ModRepairTime                           = 55,
  ModOfficerStatAttack                    = 56,
  ModOfficerStatDefense                   = 57,
  ModOfficerStatHealth                    = 58,
  ModOfficerStatAll                       = 59,
  ModShieldHpMax                          = 60,
  ModHullHpMax                            = 61,
  ModImpulseSpeed                         = 62,
  ModWarpSpeed                            = 63,
  ModWarpDistance                         = 64,
  ModEnterWarpDelay                       = 65,
  ModCargoCapacity                        = 66,
  ModCargoProtection                      = 67,
  ModAutoRecall                           = 68,
  ModEnergyLoadSpeed                      = 69,
  ModKineticLoadSpeed                     = 70,
  ModEnergyReloadSpeed                    = 71,
  ModKineticReloadSpeed                   = 72,
  ModAllDefenses                          = 73,
  ModAllPiercing                          = 74,
  ModShieldRegenTime                      = 75,
  ModShieldMitigation                     = 76,
  ModResourceProtection                   = 100,
  ModCombatResourceReward                 = 101,
  ModMissionResourceReward                = 102,
  ModFactionPointsRewards                 = 103,
  ModFactionPointsLosses                  = 104,
  ModMiningRate                           = 105,
  ModMiningReward                         = 106,
  ModRepairCosts                          = 107,
  ModStarbaseModuleConstructionSpeed      = 108,
  ModStarbaseModuleConstructionCost       = 109,
  ModResearchSpeed                        = 110,
  ModResearchCost                         = 111,
  ModResourceGeneration                   = 112,
  ModResourceStorage                      = 113,
  ModForcefieldRechargeSpeed              = 114,
  ModForcefieldHp                         = 115,
  ModComponentCost                        = 116,
  ModShipConstructionSpeed                = 117,
  ModShipConstructionCost                 = 118,
  ModTierUpSpeed                          = 119,
  ModFactionPointsPenaltyRecution         = 120,
  ModFactionStoreLootBonus                = 121,
  ModOfficerLevelUpCost                   = 122,
  ModOfficerPromoteCost                   = 123,
  ModScanShip                             = 124,
  ModScanStation                          = 125,
  ModScanSystem                           = 126,
  ModIncomingStationAttackInformation     = 127,
  ModIncomingStationAttackAdvancedWarning = 128,
  ModShipScrapResourceBonus               = 129,
  ModShipScrapLootBonus                   = 130,
  ModShipScrapSpeed                       = 131,
  ModAwayMissionSpeed                     = 132,
  ModAwayMissionResourceRewards           = 133,
  ModAwayMissionLootBonus                 = 134,
  ModResourceProducerResourceStorage      = 135,
  ModWarehouseResourceStorage             = 136,
  ModVaultResourceStorage                 = 137,
  ModHullId                               = 138,
  ModShipInventorySize                    = 139,
  ModScanCost                             = 140,
  ModTradeTax                             = 141,
  ModRepairStation                        = 142,
  ModArmadaSize                           = 143,
  ModJumpAndTowCostEff                    = 147,
  ModSkillCloakingCooldown                = 150,
  ModSkillCloakingDuration                = 151,
  ModSkillCloakingCost                    = 152,
  ModSkillCloakingSneakChance             = 153
};

struct IBuffData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit", "IBuffData">;

  ClientModifierType ModifierType()
  { return Get<"ModifierType", ClientModifierType>(); }
};

// Interface binding; the getters dispatch to the receiver's implementation.
struct IBuffComparer : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "IBuffComparer">;

  int64_t get_HullId()
  { return Get<"HullID", int64_t>(); }
  int64_t get_FactionId()
  { return Get<"FactionID", int64_t>(); }
};

// Digit.PrimeServer.Models.BuffCondition; only the values the mod or curious readers care about are named.
enum class BuffCondition : int32_t {
    Invalid = -1,
    SelfExplorer = 0,
    SelfInterceptor = 1,
    SelfBattleship = 2,
    SelfSurveyor = 3,
    EnemyExplorer = 4,
    EnemyInterceptor = 5,
    EnemyBattleship = 6,
    EnemySentinel = 8,
    CondCargoFull = 9,
    CondCargoEmpty = 10,
    CondSelfMining = 12,
    SelfOrSentinelAttacked = 14,
    // TODO(alexander): Find a better name
    EnemyStronger = 16,
    CondEnemyHullFaction = 17,
    CondEnemyHostile = 27,
    CondEnemyPlayer = 28,
    CondSelfHasMorale = 33,
    CondSelfHasHullBreach = 34,
    CondSelfIsBurning = 35,
    CondEnemyHullBreach = 37,
    CondEnemyBurning = 38,
    CondModuleEnergy = 39,
    CondModuleKinetic = 40,
    CondHitEnemyWithEnergy = 41,
    CondHitEnemyWithKinetic = 42,
    CondHullHealthBelow = 43,
    CondHullHealthBelowStartOfCombat = 44,
    // I wonder what could be here...maybe some server only things? Who knows
    CondSelfShipGrade = 102,
    CondSelfHullFaction = 113,
    CondSelfAtStation = 114,
    SelfSentinel = 116,
    CondEnemySentinel = 117, // TODO(alexander): What wait???? We have the before aswell, why is this a thing again??? see condition 8
    CondResourceId = 118,
    CondSelfModuleId = 119,
    CondSelfShipgrade1 = 120,
    CondSelfShipgrade2 = 121,
    CondSelfShipgrade3 = 122,
    CondSelfShipgrade4 = 123,
    CondSelfIsFederation = 124, // Not sure why they haven't used CondSelfHullFaction for this...but hey whatever
    CondSelfIsRomulan = 125,
    CondSelfIsKlingon = 126,
    CondSelfFranklin = 127,
    CondSelfIsArmada = 129,
    CondSelfIsNotArmada = 131,
    CondTargetShipGrade1 = 132,
    CondTargetShipGrade2 = 133,
    CondTargetShipGrade3 = 134,
    CondTargetShipGrade4 = 135,
    CondSelfHullMudd = 150,
    CondSelfHullBotanyBay = 151,
    CondSelfHullDiscovery = 152,
    CondSelfHullSarcophagus = 153,
    CondSelfHullVidar = 154,
    CondSelfHullDvor = 155,
    CondSelfHullFranklin = 156,
    CondSelfHullBlackJellyfish = 157,
    CondSelfHullFranklin2 = 158,
    CondSelfHullEnterprise = 159,
    CondSelfHullEnterpriseA = 160,
    CondSelfHullD4 = 161,
    CondSelfHullHeghta = 162,
    CondSelfHullAugur = 163,
    CondSelfHullTribune = 164,
    CondSelfHullSaladin = 165,
    CondSelfHullCenturion = 166,
    CondSelfHullBortas = 167,
    CondSelfIsSynciate30 = 203,
    CondSelfIsMantis = 207,
};

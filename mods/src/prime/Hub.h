#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class SectionID : int32_t {
  ArtifactHall_Inventory                   = -2058246328,
  Alliance_Contribution                    = -2035860712,
  Alliance_Diplomacy_List                  = -1998741427,
  Navigation_Combat_Debug                  = -1993831053,
  SlideShowDialog                          = -1974142186,
  Tournament_Leaderboard                   = -1944855001,
  Research_LandingPage                     = -1936877406,
  Bookmarks_Main                           = -1863601413,
  Leaderboard_Alliances                    = -1853908838,
  Missions_AcceptedList                    = -1833888323,
  ShipCosmetics_Preview                    = -1783421199,
  OfficerAbilities                         = -1770476846,
  GameNews                                 = -1704696315,
  Missions_Achievements                    = -1663314948,
  Shop_AllianceChests                      = -1658878702,
  Combat_Scan                              = -1617308440,
  Starbase_Armada_Building                 = -1582567019,
  OfficerReveal                            = -1560454013,
  ChallengeSelection                       = -1547300727,
  Reentry                                  = -1545897473,
  Starbase_Interior                        = -1518342580,
  OfficerGacha                             = -1446106612,
  Alliance_Permission                      = -1437368617,
  Tournament_Selection                     = -1398895946,
  ShipCosmetics_Cloaking                   = -1396274167,
  Alliance_Create                          = -1339482620,
  Navigation_Galaxy                        = -1300757605,
  Alliance_Armadas_Threats                 = -1240094941,
  Chat_Private_Message                     = -1232447932,
  Alliance_Invite                          = -1170887663,
  Alliance_EditProfile                     = -1168827337,
  TerritoryCapture_JoinTakeover            = -1155055432,
  OfficerInventory                         = -1154490138,
  PlayerProfile                            = -1152456856,
  BattlePass_Main                          = -1079277745,
  Bookmarks_Details                        = -1075632704,
  IntroVideo                               = -1052852849,
  Officer_Presets                          = -1042713813,
  Navigation_Planet                        = -1033252317,
  Tournament_Store_Bucket_Content          = -985604392,
  Alliance_Help                            = -935334327,
  Alliance_Join                            = -935265230,
  Alliance_Main                            = -935189311,
  Navigation_System                        = -934817094,
  Missions_Rewards                         = -904826036,
  Shop_Holodeck                            = -820683876,
  AwayTeamAssignments                      = -791072837,
  ShipManagement_Details                   = -763928158,
  InventoryList                            = -726704134,
  Alliance_SendInvite                      = -726475335,
  OfficerShowcase                          = -726094109,
  Chat_Main                                = -672065984,
  Shop_Showcase                            = -653955370,
  Shop_MainFactions                        = -653267623,
  Missions_DailyGoals                      = -644919297,
  Alliance_Starbase_Management             = -590011913,
  FleetCommander_Showcase                  = -562276158,
  OfficerConfirmSale                       = -548222927,
  CutsceneVideo                            = -476329231,
  ZDummyTerminator                         = -476329230,
  Inbox_Main                               = -461074414,
  OnScreen_Fleet                           = -412514502,
  Bookmarks_Search_Coordinates             = -393757818,
  Alliance_Choose_Emblem                   = -342315942,
  Alliance_Armadas                         = -341922839,
  Chat_Private_List                        = -329495679,
  Starbase_Level_UpgradeWarning            = -328717641,
  ForbiddenTech_Management                 = -261899117,
  ForbiddenTech_Showcase                   = -207181827,
  AwayTeamsRewardScreen                    = -117555229,
  Shop_List                                = -100955065,
  Tournament_Detail                        = -72573081,
  ChallengeLadder                          = -39891387,
  Leaderboard_General                      = -15226394,
  AR_Ship                                  = -3902038,
  AppInit                                  = 0,
  Shop                                     = 2576150,
  Navigation_Default                       = 72648470,
  Login                                    = 73596745,
  Chat_Settings                            = 120009002,
  DebugEmpty                               = 129029850,
  TerritoryCapture_HUB                     = 198032386,
  AvatarSelection                          = 228326419,
  HailingFrequencies_Inventory             = 240618353,
  OfficerGachaSelection                    = 253555936,
  ServerTransferProgress                   = 301530299,
  Starbase_Exterior                        = 385452890,
  ServerTransfer                           = 449193070,
  Shop_Bundle_List                         = 462754450,
  Chat_Alliance                            = 492322110,
  Combat_Report                            = 518419679,
  Missions_Archived                        = 538435770,
  ShipManagement_Selection                 = 561547500,
  Shop_Refining_List                       = 620333674,
  Chat_Block                               = 630965478,
  ShipConstruction_HullSelection           = 658582829,
  TerritoryCapture_ContributionLeaderboard = 665740250,
  Interstitial                             = 769047372,
  Consumables                              = 805312080,
  ShipConstruction_Details                 = 883531056,
  Achievement_List_Events                  = 960162474,
  Missions_AwayTeamsList                   = 963848782,
  Shop_Refining_Showcase                   = 985768825,
  SpecialRewardScreen                      = 1014999988,
  Alliance_Starbase_Build                  = 1027529786,
  Tournament_Detail_ServerClash            = 1090412274,
  Starbase_ModuleDetails                   = 1100274458,
  TerritoryCapture_Embassy                 = 1121865311,
  Shop_Summary                             = 1180221277,
  FleetCommander_SkillTrees                = 1192159385,
  SlideShow                                = 1207462958,
  OnScreen_Self                            = 1233999840,
  Research_TreeView                        = 1240388999,
  Achievement_List_Buckets                 = 1302142040,
  Alliance_Members                         = 1346039057,
  ShipCosmetics_Management                 = 1381234810,
  ShipConstruction_Reveal                  = 1399203615,
  Treasury                                 = 1448052451,
  ShipCosmetics_Research                   = 1471406994,
  Officer_Assignment                       = 1547574710,
  Alliance_Management                      = 1575235915,
  Leaderboard_Details                      = 1622643520,
  ArtifactHall_Details                     = 1664944942,
  Alliance_Inventory                       = 1690509140,
  Missions_AvailableList                   = 1719419183,
  ShipManagement_Upgrade                   = 1742145916,
  GameSettings                             = 1754181205,
  Starbase_Planetary                       = 1781357748,
  ShipScrapping_List                       = 1844785206,
  OnScreen_Station                         = 1883947040,
  RewardScreen                             = 1890775195,
  Shop_Reveal                              = 1934589238,
  PrepareForReentry                        = 1937864029,
  Missions_Dialogue                        = 2020901552,
  Loyalty                                  = 2023382054,
  FleetCommander_Management                = 2089832472,
  OfficerRevealSummary                     = 2134661539,
  Tournament_Group_Selection               = 2138751318,
};

struct SectionStorage : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Sections", "SectionStorage">;

  void* GetState(SectionID section)
  { return Call<"GetState", void*>(section); }
};

struct SectionManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Sections", "SectionManager">;

  SectionID CurrentSection()
  { return Get<"CurrentSection", SectionID>(); }
  SectionStorage* sectionStorage()
  { return Field<"_sectionStorage", SectionStorage*>(); }

  // The (SectionData) overload is not used here; args is any managed object or null.
  bool TriggerSectionChange(SectionID nextSectionID, void* args, bool forcedSectionChange = false,
                            bool isGoBackStep = false, bool allowSameSection = false)
  {
    return Call<"TriggerSectionChange", bool>(nextSectionID, args, forcedSectionChange, isGoBackStep, allowSameSection);
  }
};

// Digit.Client.Core.CustomReloadArgs and Nullable<CustomReloadArgs>, laid out as the runtime does.
struct CustomReloadArgs {
  Il2CppObject* SectionArgs;
  bool          ForceFakeReload;
};
struct NullableCustomReloadArgs {
  bool             hasValue;
  CustomReloadArgs value;
};

struct PrimeApp : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Core", "PrimeApp">;

  void Quit()
  { Call<"Quit">(); }
  void Reload()
  { Call<"Reload">(false, NullableCustomReloadArgs{}, false); }
};

// Digit.Client.Core.Hub is a static class.
struct Hub {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Core", "Hub">;

  static SectionManager* get_SectionManager()
  { return Class::Get<"SectionManager", SectionManager*>(); }
  static PrimeApp* get_App()
  { return Class::Get<"App", PrimeApp*>(); }

  static SectionID CurrentSection()
  {
    auto* manager = get_SectionManager();
    return manager ? manager->CurrentSection() : SectionID::AppInit;
  }

  static bool IsInChat()
  {
    auto current_section = CurrentSection();
    return current_section == SectionID::Chat_Private_Message || current_section == SectionID::Chat_Alliance
           || current_section == SectionID::Chat_Main || current_section == SectionID::Chat_Private_List;
  }

  static bool IsInSystemOrGalaxyOrStarbase()
  {
    auto current_section = CurrentSection();
    return current_section == SectionID::Navigation_Galaxy || current_section == SectionID::Navigation_System
           || current_section == SectionID::Starbase_Interior || current_section == SectionID::Starbase_Exterior;
  }
};

#pragma once

#include "EntityGroup.h"

#include <il2cpp/il2cpp.h>

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

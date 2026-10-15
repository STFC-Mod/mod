#pragma once

#include "FleetDeployedData.h"
#include "FleetPlayerData.h"
#include "MonoSingleton.h"
#include "Vector3.h"

#include <il2cpp/il2cpp.h>

struct DeploymentService : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Services", "DeploymentService">;

  // The compiler-generated <PlanCourse>d__N iterator. Unbound so MoveNext resolves on whatever class the game
  // returns; the suffix changes between client builds.
  struct IEnumerator_PlanCourse : il2cpp::Object {
    bool MoveNext()
    { return Call<"MoveNext", bool>(); }
  };
};

// The managed class is DeploymentManager; the misspelled C++ name is kept for existing callers.
struct DeploymentManger : MonoSingleton<DeploymentManger> {
  using Class = il2cpp::Class<"Assembly-CSharp", "", "DeploymentManager">;

  void SetTowRequest(uint64_t towedFleetId, uint64_t towingFleetId)
  {
    // The game expects a CallbackContainer<int>; an empty one without handlers is enough. Take the concrete
    // generic instance from the method signature instead of spelling the inflated class out here.
    static Il2CppClass* const callback_class = [] {
      auto method = Class::Method("SetTowRequest", 3);
      return method ? il2cpp_class_from_type(method.Info()->parameters[2]) : nullptr;
    }();
    if (!callback_class)
      return;
    Call<"SetTowRequest">(towedFleetId, towingFleetId, il2cpp_object_new(callback_class));
  }

  bool HasInstantWarpAbility(FleetPlayerData* selectedFleet)
  { return Call<"HasInstantWarpAbility", bool>(selectedFleet); }

  // targetAddress is a NodeAddress; the trailing data arguments are optional references.
  DeploymentService::IEnumerator_PlanCourse* PlanCourse(FleetPlayerData* selectedFleet, void* targetAddress,
                                                        Vector3 targetPosition, FleetDeployedData* targetDeployedFleet,
                                                        void* starbaseData, void* allianceStarbaseData,
                                                        void* outpost = nullptr)
  {
    return Call<"PlanCourse", DeploymentService::IEnumerator_PlanCourse*>(
        selectedFleet, targetAddress, targetPosition, targetDeployedFleet, starbaseData, allianceStarbaseData, outpost);
  }
};

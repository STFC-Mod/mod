#pragma once

#include "FleetDeployedData.h"
#include "FleetPlayerData.h"
#include "MonoSingleton.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct FleetsManager : MonoSingleton<FleetsManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.FleetManagement", "FleetsManager">;

  void RequestViewFleet(FleetPlayerData* fleetData, bool showSystemInfo = false)
  { Call<"RequestViewFleet">(fleetData, showSystemInfo); }

  // The int overload takes a fleet slot; the long overload takes a fleet id.
  FleetPlayerData* GetFleetPlayerData(int32_t idx)
  { return Call<"GetFleetPlayerData", FleetPlayerData*>(idx); }

  FleetPlayerData* GetSelectedFleetData()
  { return Call<"GetSelectedFleetData", FleetPlayerData*>(); }

  FleetDeployedData* targetFleetData()
  { return Field<"_targetFleetData", FleetDeployedData*>(); }

  // _fleetServiceCache is a CachedService<FleetService> struct whose HasService getter resolves and
  // stores the service lazily. It has to run on the field in place, not on a boxed copy, so this
  // stays on the raw runtime API.
  bool HasFleetService()
  {
    static FieldInfo* const field = Class::Raw() ? il2cpp_class_get_field_from_name(Class::Raw(), "_fleetServiceCache")
                                                 : nullptr;
    static const MethodInfo* const has_service =
        field ? il2cpp_class_get_method_from_name(il2cpp_class_from_type(il2cpp_field_get_type(field)),
                                                  "get_HasService", 0)
              : nullptr;
    if (!has_service) {
      return false;
    }

    auto* cache = reinterpret_cast<char*>(this) + il2cpp_field_get_offset(field);
    if (*reinterpret_cast<void**>(cache)) {
      return true;
    }

    Il2CppException* exception = nullptr;
    auto*            result    = il2cpp_runtime_invoke(has_service, cache, nullptr, &exception);
    return !exception && result && *static_cast<bool*>(il2cpp_object_unbox(result));
  }
};

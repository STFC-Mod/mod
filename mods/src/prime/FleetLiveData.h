#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>
#include <optional>

// Cargo, mining slot and timers off FleetPlayerData. The interfaces are bound as such: their getters dispatch to the
// runtime class, which also covers FleetMiningSlot's explicit IMiningSlot implementation.

// System.DateTime: ticks in the low 62 bits, the Kind in the top two.
struct DateTime {
  uint64_t dateData;

  // Unix seconds, or empty for DateTime.MinValue (unset).
  std::optional<int64_t> UnixSeconds() const
  {
    const auto ticks = static_cast<int64_t>(dateData & 0x3FFFFFFFFFFFFFFFull);
    if (ticks <= 0)
      return std::nullopt;
    return (ticks - 621355968000000000LL) / 10000000LL;
  }
};

// Concrete: ProgressData, CargoProgressData.
struct IProgressData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "IProgressData">;

  double CurrentValue()
  { return Get<"CurrentValue", double>(); }
  double MaxValue()
  { return Get<"MaxValue", double>(); }
};

struct CargoHoldData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "CargoHoldData">;

  IProgressData* CurrentCargo()
  { return Get<"CurrentCargo", IProgressData*>(); }
  IProgressData* ProtectedCargoProgress()
  { return Get<"ProtectedCargoProgress", IProgressData*>(); }
};

struct IMiningNodeParams : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "IMiningNodeParams">;

  int64_t Amount()
  { return Get<"Amount", int64_t>(); }
};

// Concrete: FleetMiningSlot, MiningSlot.
struct IMiningSlot : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "IMiningSlot">;

  int64_t ParentId()
  { return Get<"ParentId", int64_t>(); }
  int64_t ResourceId()
  { return Get<"ResourceId", int64_t>(); }
  double AmountMined()
  { return Get<"AmountMined", double>(); }
  DateTime OccupyTimeUtc()
  { return Get<"OccupyTimeUtc", DateTime>(); }
  IMiningNodeParams* PointData()
  { return Get<"PointData", IMiningNodeParams*>(); }
};

// Concrete: MiningSlotTimer and others.
struct ITimerData : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.PrimeServer.Models", "ITimerData">;

  DateTime DueTime()
  { return Get<"DueTime", DateTime>(); }
};

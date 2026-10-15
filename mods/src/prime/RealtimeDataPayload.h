#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

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

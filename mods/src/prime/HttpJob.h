#pragma once

#include "HttpRequest.h"
#include "UnityWebRequest.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class HttpJobState : int32_t {
  None                 = 0,
  Queued               = 1,
  RequestSending       = 2,
  ResponseReceiving    = 3,
  ResponseReceived     = 4,
  ResponseBuilt        = 5,
  WaitForAsyncCallback = 6,
  Complete             = 7
};

struct HttpJob : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Engine.HttpClient.Runtime", "Digit.Engine.HttpClient", "HttpJob">;

  HttpRequest* Request()
  { return Field<"Request", HttpRequest*>(); }
  UnityWebRequest* UnityRequest()
  { return Field<"UnityRequest", UnityWebRequest*>(); }
  HttpJobState State()
  { return Field<"State", HttpJobState>(); }
  // Writes the field directly and bypasses the managed SetState(HttpJobState).
  void SetState(HttpJobState state)
  { SetField<"State">(state); }
};

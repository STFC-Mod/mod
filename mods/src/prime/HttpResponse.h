#pragma once

#include "HttpRequest.h"

#include <il2cpp/il2cpp.h>

struct HttpResponse : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Engine.HttpClient.Runtime", "Digit.Engine.HttpClient", "HttpResponse">;

  HttpRequest* get_Request()
  { return Get<"Request", HttpRequest*>(); }
  Il2CppString* Body()
  { return Get<"Body", Il2CppString*>(); }
};

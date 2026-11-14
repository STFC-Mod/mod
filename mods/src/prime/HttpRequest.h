#pragma once

#include <il2cpp/il2cpp.h>

struct HttpRequest : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Engine.HttpClient.Runtime", "Digit.Engine.HttpClient", "HttpRequest">;

  const Il2CppChar* get_URL()
  {
    auto* url = Get<"URL", Il2CppString*>();
    return url ? il2cpp_string_chars(url) : nullptr;
  }
};

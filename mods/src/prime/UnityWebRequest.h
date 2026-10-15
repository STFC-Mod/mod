#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct UnityWebRequest : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UnityWebRequestModule", "UnityEngine.Networking", "UnityWebRequest">;

  uintptr_t get_uploadHandler()
  { return reinterpret_cast<uintptr_t>(Get<"uploadHandler", Il2CppObject*>()); }
};

#pragma once

#include "str_utils.h"

#include <il2cpp/il2cpp.h>

#include <string>

struct GameObject : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "GameObject">;

  std::string Name()
  {
    auto* name = Get<"name", Il2CppString*>();
    return name ? to_string(name) : std::string{};
  }
  bool activeInHierarchy()
  { return Get<"activeInHierarchy", bool>(); }
  void SetActive(bool active)
  { Call<"SetActive">(active); }

  // Passing System.Type selects the (Type) overloads over the string and generic ones. A null
  // Type would throw in managed code, so a missing class yields null instead.
  template <typename T> T* GetComponent()
  {
    auto* type = T::Class::TypeObject();
    return type ? Call<"GetComponent", T*>(type) : nullptr;
  }
  template <typename T> T* AddComponent()
  {
    auto* type = T::Class::TypeObject();
    return type ? Call<"AddComponent", T*>(type) : nullptr;
  }
};

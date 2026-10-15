#pragma once

#include <il2cpp/il2cpp.h>

// Scene is a struct wrapping an int handle; the internal calls below take and fill it directly.
struct SceneManager {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine.SceneManagement", "SceneManager">;

  static const Il2CppChar* GetSceneName(void* scene)
  {
    static auto GetSceneName =
        il2cpp::ResolveICall<Il2CppString*(void*)>("UnityEngine.SceneManagement.Scene::GetNameInternal(System.Int32)");
    auto name = GetSceneName(scene);
    return il2cpp_string_chars(name);
  }

  static void* GetActiveScene()
  {
    static auto GetActiveScene_Injected = il2cpp::ResolveICall<void(void*)>(
        "UnityEngine.SceneManagement.SceneManager::GetActiveScene_Injected(UnityEngine.SceneManagement.Scene&)");
    void* scene = nullptr;
    GetActiveScene_Injected(&scene);
    return scene;
  }
};

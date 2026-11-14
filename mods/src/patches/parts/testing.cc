#include "config.h"

#include "prime/ActionQueueManager.h"
#include "prime/ActionRequirement.h"
#include "prime/BlurController.h"
#include "prime/BookmarksManager.h"
#include "prime/ChatManager.h"
#include "prime/ChatMessageListLocalViewController.h"
#include "prime/ClientModifierType.h"
#include "prime/DeploymentManager.h"
#include "prime/FleetLocalViewController.h"
#include "prime/FleetsManager.h"
#include "prime/FullScreenChatViewController.h"
#include "prime/Hub.h"
#include "prime/InventoryForPopup.h"
#include "prime/KeyCode.h"
#include "prime/NavigationSectionManager.h"
#include "prime/ScanTargetViewController.h"
#include "prime/SceneManager.h"
#include "prime/ScreenManager.h"
#include <prime/UIBehaviour.h>

#include <il2cpp/il2cpp.h>
#include <spud/detour.h>
#include <spud/signature.h>

struct AppConfig : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Core", "AppConfig">;

  Il2CppString* PlatformSettingsUrl()
  { return Get<"PlatformSettingsUrl", Il2CppString*>(); }
  void SetPlatformSettingsUrl(Il2CppString* value)
  { Set<"PlatformSettingsUrl">(value); }
  Il2CppString* PlatformApiKey()
  { return Get<"PlatformApiKey", Il2CppString*>(); }
  void SetPlatformApiKey(Il2CppString* value)
  { Set<"PlatformApiKey">(value); }
  Il2CppString* AssetUrlOverride()
  { return Get<"AssetUrlOverride", Il2CppString*>(); }
  void SetAssetUrlOverride(Il2CppString* value)
  { Set<"AssetUrlOverride">(value); }
};

struct Model : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Core", "Model">;

  AppConfig* appConfig()
  { return Field<"_appConfig", AppConfig*>(); }
};

void Cursor_SetCursor(auto original, void* _this, ptrdiff_t texture, Vector2* hotspot, int cursorMode)
{
#if _WIN32
  if (!Config::Get().allow_cursor) {
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    ClipCursor(nullptr); // free cursor from any Unity clipping
    return;
  }
#endif

  return original(_this, texture, hotspot, cursorMode);
}

void Model_LoadConfigs(auto original, Model* _this)
{
  original(_this);
  auto config = _this->appConfig();
  if (!config) {
    return;
  }

  if (!Config::Get().config_settings_url.empty()) {
    config->SetPlatformSettingsUrl(il2cpp_string_new(Config::Get().config_settings_url.c_str()));
  }

  if (!Config::Get().config_assets_url_override.empty()) {
    config->SetAssetUrlOverride(il2cpp_string_new(Config::Get().config_assets_url_override.c_str()));
  }
}

void SetActive_hook(auto original, void* _this, bool active)
{
  static auto IsActiveSelf = il2cpp::ResolveICall<bool(void*)>("UnityEngine.GameObject::get_activeSelf()");

  if (active && IsActiveSelf(_this)) {
    return;
    // __debugbreak();
  }
  return original(_this, active);
}

bool IsQueueEnabled(auto original, void* _this)
{
  if (Config::Get().queue_enabled) {
    return original(_this);
  }

  return false;
}

void InstallTestPatches()
{
  using Cursor = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Cursor">;
  if (auto m = Cursor::Method("SetCursor_Injected"))
    SPUD_STATIC_DETOUR(m.Pointer(), Cursor_SetCursor);

  if (auto m = Model::Class::Method("LoadConfigs", 0))
    SPUD_STATIC_DETOUR(m.Pointer(), Model_LoadConfigs);

  // this method is not accessible in v48084
  //   static auto SetActive = il2cpp::ResolveICall<void(void*, bool)>("UnityEngine.GameObject::SetActive(System.Boolean)");
  //   if (SetActive)
  //     SPUD_STATIC_DETOUR(SetActive, SetActive_hook);

  if (auto m = ActionQueueManager::Class::Method("IsQueueUnlocked"))
    SPUD_STATIC_DETOUR(m.Pointer(), IsQueueEnabled);
}

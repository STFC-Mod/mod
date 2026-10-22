// [config] settings_url and assets_url_override: replace the server addresses in the game's own app config as it
// loads.

#include "config.h"
#include "features/features.h"

#include "game/app.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>

namespace
{
void Model_LoadConfigs_Hook(auto original, Model* _this)
{
  original(_this);
  auto* config = _this->appConfig();
  if (!config)
    return;
  const auto& game = Config::Get().game;
  if (!game.settings_url.empty())
    config->SetPlatformSettingsUrl(il2cpp::NewString(game.settings_url));
  if (!game.assets_url_override.empty())
    config->SetAssetUrlOverride(il2cpp::NewString(game.assets_url_override));
}

bool InstallGameConfigOverrides()
{
  il2cpp::HookSet hooks("GameConfigOverrides");
  IL2CPP_STAGE(hooks, Model::Class::Method<void()>("LoadConfigs"), Model_LoadConfigs_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kGameConfigOverrides{"GameConfigOverrides", &Config::Patches::testpatches, InstallGameConfigOverrides};

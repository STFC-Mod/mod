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
    config->SetPlatformSettingsUrl(il2cpp::NewString(game.settings_url.c_str()));
  if (!game.assets_url_override.empty())
    config->SetAssetUrlOverride(il2cpp::NewString(game.assets_url_override.c_str()));
}

void InstallGameConfigOverrides()
{ IL2CPP_HOOK(Model::Class::Method<void()>("LoadConfigs"), Model_LoadConfigs_Hook); }
} // namespace

const Feature kGameConfigOverrides{"GameConfigOverrides", &Config::Patches::testpatches, InstallGameConfigOverrides};

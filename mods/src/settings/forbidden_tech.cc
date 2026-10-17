#include "forbidden_tech.h"
#include "config.h"
#include "defaultconfig.h"
#include "persisted_setting.h"

namespace mod_settings
{
BooleanSetting& ForbiddenTechConfirmationSetting()
{
  static BooleanSetting setting(PersistedDefinition<bool>(
      {"community_mod.ft_confirmation", "Confirm Forbidden Tech upgrades",
       [] {
         return ForbiddenTechControlsAvailable() ? ReadResult::Known(!Config::Get().auto_confirm_ft_upgrade, 1)
                                                 : ReadResult{};
       },
       [](bool confirm, std::uint64_t generation) {
         if (generation != 1 || !ForbiddenTechControlsAvailable())
           return ApplyResult::Rejected;
         Config::Get().auto_confirm_ft_upgrade = !confirm;
         return ApplyResult::Applied;
       }},
      {"ui", "auto_confirm_ft_upgrade", "Ask before upgrading Forbidden Tech.", "", ApplyTiming::Live},
      !DefaultConfig::UI::auto_confirm_ft_upgrade, [](const bool& confirm) -> config_edit::Value { return !confirm; }));
  return setting;
}
} // namespace mod_settings

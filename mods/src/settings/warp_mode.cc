#include "warp_mode.h"
#include "config.h"
#include "defaultconfig.h"
#include "persisted_setting.h"
#include <algorithm>
#include <array>
#include <spdlog/spdlog.h>

namespace mod_settings
{
namespace
{
  constexpr std::array modes{"none", "warp", "jump"};
  constexpr int        DefaultMode()
  {
    for (std::size_t i = 0; i < modes.size(); ++i)
      if (std::string_view(modes[i]) == DefaultConfig::UI::auto_confirm_instant_warp)
        return static_cast<int>(i);
    return -1;
  }
  static_assert(DefaultMode() >= 0);
} // namespace
ChoiceSetting& WarpModeSetting()
{
  static ChoiceSetting setting(
      PersistedDefinition<int>(
          {"community_mod.warp_mode", "Instant warp mode",
           [] {
             return Config::Get().installInstantWarpConfirmationHooks
                        ? ValueReadResult<int>::Known(static_cast<int>(Config::Get().auto_confirm_instant_warp), 1)
                        : ValueReadResult<int>{Availability::Unsupported};
           },
           [](int value, std::uint64_t generation) {
             if (generation != 1 || !Config::Get().installInstantWarpConfirmationHooks || value < 0
                 || value >= static_cast<int>(modes.size()))
               return ApplyResult::Rejected;
             Config::Get().auto_confirm_instant_warp = static_cast<InstantWarpConfirmation>(value);
             spdlog::info("Auto-confirm instant warp set to {}", modes[value]);
             return ApplyResult::Applied;
           }},
          {"ui", "auto_confirm_instant_warp",
           "Choose which instant-travel action is confirmed automatically. Ship filters can override this mode.", "",
           ApplyTiming::Live},
          DefaultMode(), [](const int& value) -> config_edit::Value { return std::string(modes.at(value)); }),
      {"Ask", "Warp", "Jump"});
  return setting;
}

void CycleWarpMode()
{
  auto&      setting  = WarpModeSetting().state();
  const auto snapshot = setting.Observe();
  if (snapshot.state.known())
    setting.SetFromUser((*snapshot.state.value + 1) % 3, snapshot);
}
} // namespace mod_settings

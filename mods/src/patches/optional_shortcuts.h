#pragma once
#include "defaultconfig.h"
#include "patches/gamefunctions.h"
#include <array>
#include <string_view>

namespace shortcut_options
{
struct Definition {
  GameFunction     action;
  std::string_view key, default_binding;
  bool             experimental;
};
inline constexpr auto definitions = std::to_array<Definition>({
    {GameFunction::ZoomMax, "zoom_max", DefaultConfig::Shortcuts::zoom_max, false},
    {GameFunction::ZoomMin, "zoom_min", DefaultConfig::Shortcuts::zoom_min, false},
    {GameFunction::ZoomReset, "zoom_reset", DefaultConfig::Shortcuts::zoom_reset, false},
    {GameFunction::ShowAlliance, "show_alliance", DefaultConfig::Shortcuts::show_alliance, false},
    {GameFunction::ShowAllianceHelp, "show_alliance_help", DefaultConfig::Shortcuts::show_alliance_help, true},
    {GameFunction::ShowAllianceArmada, "show_alliance_armada", DefaultConfig::Shortcuts::show_alliance_armada, true},
    {GameFunction::ShowBookmarks, "show_bookmarks", DefaultConfig::Shortcuts::show_bookmarks, false},
    {GameFunction::ShowLookup, "show_lookup", DefaultConfig::Shortcuts::show_lookup, true},
    {GameFunction::SetZoomPreset1, "set_zoom_preset1", DefaultConfig::Shortcuts::set_zoom_preset1, false},
    {GameFunction::SetZoomPreset2, "set_zoom_preset2", DefaultConfig::Shortcuts::set_zoom_preset2, false},
    {GameFunction::SetZoomPreset3, "set_zoom_preset3", DefaultConfig::Shortcuts::set_zoom_preset3, false},
    {GameFunction::SetZoomPreset4, "set_zoom_preset4", DefaultConfig::Shortcuts::set_zoom_preset4, false},
    {GameFunction::SetZoomPreset5, "set_zoom_preset5", DefaultConfig::Shortcuts::set_zoom_preset5, false},
    {GameFunction::SetZoomDefault, "set_zoom_default", DefaultConfig::Shortcuts::set_zoom_default, false},
    {GameFunction::TogglePreviewLocate, "toggle_preview_locate", DefaultConfig::Shortcuts::toggle_preview_locate,
     false},
    {GameFunction::TogglePreviewRecall, "toggle_preview_recall", DefaultConfig::Shortcuts::toggle_preview_recall,
     false},
    {GameFunction::ToggleCargoDefault, "toggle_cargo_default", DefaultConfig::Shortcuts::toggle_cargo_default, false},
    {GameFunction::ToggleCargoPlayer, "toggle_cargo_player", DefaultConfig::Shortcuts::toggle_cargo_player, false},
    {GameFunction::ToggleCargoStation, "toggle_cargo_station", DefaultConfig::Shortcuts::toggle_cargo_station, false},
    {GameFunction::ToggleCargoHostile, "toggle_cargo_hostile", DefaultConfig::Shortcuts::toggle_cargo_hostile, false},
    {GameFunction::ToggleCargoArmada, "toggle_cargo_armada", DefaultConfig::Shortcuts::toggle_cargo_armada, false},
});
// Includes both conditionally loaded actions and conditionally dispatched zoom
// actions. Always load metadata and configured bindings; dispatch retains its startup
// availability. Disabled actions remain discoverable with a reason in Settings.
template <typename Load> void LoadAll(bool extended, bool experimental, Load load)
{
  for (const auto& definition : definitions)
    load(definition, extended && (!definition.experimental || experimental));
}
} // namespace shortcut_options

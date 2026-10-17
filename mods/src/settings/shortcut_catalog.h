#pragma once
#include "page_catalog.h"
#include "patches/gamefunctions.h"
#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mod_settings
{
// Change + Remove per binding, Add, status/Undo, Restore, optional explanation
// and save notice. Retain the established binding limit with three spare slots.
// Oversized player-authored lists stay live; the editor presents their prefix.
inline constexpr std::size_t ShortcutFixedRowLimit       = 8;
inline constexpr std::size_t ShortcutBindingDisplayLimit = (PageCatalog::NativeChildLimit - ShortcutFixedRowLimit) / 2;
constexpr std::size_t        VisibleShortcutBindingCount(std::size_t count)
{ return std::min(count, ShortcutBindingDisplayLimit); }
constexpr bool ShortcutCountFitsEdit(std::size_t before, std::size_t after)
{ return after <= ShortcutBindingDisplayLimit || after <= before; }
enum class ShortcutGroup {
  Screens,
  Previews,
  Interface,
  Fleet,
  Travel,
  Camera,
  Chat,
  Client,
  Uncategorized,
  Diagnostics
};
struct ShortcutGroupInfo {
  ShortcutGroup    group;
  std::string_view id, label;
};
// Category registration is display order: alphabetized, with Diagnostics last.
inline constexpr auto ShortcutGroups = std::to_array<ShortcutGroupInfo>({
    {ShortcutGroup::Camera, "camera", "Camera"},
    {ShortcutGroup::Chat, "chat", "Chat"},
    {ShortcutGroup::Client, "client", "Client"},
    {ShortcutGroup::Fleet, "fleet", "Fleet Controls"},
    {ShortcutGroup::Screens, "screens", "Game Screens"},
    {ShortcutGroup::Interface, "interface", "Interface Controls"},
    {ShortcutGroup::Travel, "travel", "Map & Travel"},
    {ShortcutGroup::Previews, "previews", "Previews & Cargo"},
    {ShortcutGroup::Uncategorized, "uncategorized", "Uncategorized"},
    {ShortcutGroup::Diagnostics, "diagnostics", "Diagnostics"},
});
struct ShortcutInfo {
  GameFunction     action;
  ShortcutGroup    group;
  std::string_view label;
};
constexpr std::string_view ShortcutExplanation(GameFunction action)
{
  switch (action) {
    case MoveUp:
    case MoveDown:
      return "Pan the system or galaxy map. Disabled when keyboard movement is turned off.";
    case MoveLeft:
    case MoveRight:
      return "Pan the map, rotate the station exterior, or change selection in supported screens.";
    case Quit:
      return "Force closes the client; allows up to 0.5s for pending saves.";
    case NativeShortcutGalaxy:
    case NativeShortcutEvents:
      return "Uses the game's own shortcut behavior for this screen.";
    default:
      return {};
  }
}
// Optional presentation overrides. Missing actions use their registered config
// key for a fallback label in Uncategorized. Config/defaults/dispatch/save
// identities remain owned by MapKey/config; opening a screen is a UI action.
inline constexpr auto ShortcutCatalog = std::to_array<ShortcutInfo>({
    {MoveLeft, ShortcutGroup::Camera, "Pan left"},
    {MoveRight, ShortcutGroup::Camera, "Pan right"},
    {MoveUp, ShortcutGroup::Camera, "Pan up"},
    {MoveDown, ShortcutGroup::Camera, "Pan down"},
    {SelectChatAlliance, ShortcutGroup::Chat, "Switch to alliance chat"},
    {SelectChatGlobal, ShortcutGroup::Chat, "Switch to global chat"},
    {SelectChatPrivate, ShortcutGroup::Chat, "Switch to private chat"},
    {SelectShip1, ShortcutGroup::Fleet, "Select ship 1"},
    {SelectShip2, ShortcutGroup::Fleet, "Select ship 2"},
    {SelectShip3, ShortcutGroup::Fleet, "Select ship 3"},
    {SelectShip4, ShortcutGroup::Fleet, "Select ship 4"},
    {SelectShip5, ShortcutGroup::Fleet, "Select ship 5"},
    {SelectShip6, ShortcutGroup::Fleet, "Select ship 6"},
    {SelectShip7, ShortcutGroup::Fleet, "Select ship 7"},
    {SelectShip8, ShortcutGroup::Fleet, "Select ship 8"},
    {SelectCurrent, ShortcutGroup::Fleet, "Locate selected ship"},
    {ShowAlliance, ShortcutGroup::Screens, "Open alliance"},
    {ShowAllianceArmada, ShortcutGroup::Screens, "Open alliance armadas"},
    {ShowAllianceHelp, ShortcutGroup::Screens, "Open alliance help"},
    {ShowArtifacts, ShortcutGroup::Screens, "Open artifacts"},
    {ShowOfficers, ShortcutGroup::Screens, "Open officers"},
    {ShowOfficerPresets, ShortcutGroup::Screens, "Open officer presets"},
    {ShowOfficerSwap, ShortcutGroup::Fleet, "Swap officers"},
    {ShowShipSwap, ShortcutGroup::Fleet, "Swap ship"},
    {ShowHaven, ShortcutGroup::Screens, "Open Haven"},
    {ShowCommander, ShortcutGroup::Screens, "Open Fleet Commanders"},
    {ShowRefinery, ShortcutGroup::Screens, "Open refinery"},
    {ShowQTrials, ShortcutGroup::Screens, "Open Q's Trials"},
    {ShowBookmarks, ShortcutGroup::Travel, "Open bookmarks"},
    {ShowLookup, ShortcutGroup::Travel, "Find coordinates"},
    {ShowExoComp, ShortcutGroup::Screens, "Open Exocomps"},
    {ShowFactions, ShortcutGroup::Screens, "Open factions"},
    {ShowGifts, ShortcutGroup::Screens, "Open gifts"},
    {ShowDaily, ShortcutGroup::Screens, "Open daily goals"},
    {ShowAwayTeam, ShortcutGroup::Screens, "Open Away Teams"},
    {ShowMissions, ShortcutGroup::Screens, "Open missions"},
    {ShowResearch, ShortcutGroup::Screens, "Open research"},
    {ShowScrapYard, ShortcutGroup::Screens, "Open scrapyard"},
    {ShowShips, ShortcutGroup::Screens, "Manage selected ship"},
    {ShowInventory, ShortcutGroup::Screens, "Open inventory"},
    {ShowStationInterior, ShortcutGroup::Travel, "View station interior"},
    {ShoWStationExterior, ShortcutGroup::Travel, "View station exterior"},
    {ShowGalaxy, ShortcutGroup::Travel, "View galaxy"},
    {NativeShortcutGalaxy, ShortcutGroup::Travel, "View galaxy (native shortcut)"},
    {ShowSystem, ShortcutGroup::Travel, "View system"},
    {ShowChat, ShortcutGroup::Chat, "Open chat"},
    {ShowChatSide1, ShortcutGroup::Chat, "Open side chat 1"},
    {ShowChatSide2, ShortcutGroup::Chat, "Open side chat 2"},
    {ShowEvents, ShortcutGroup::Screens, "Open events"},
    {NativeShortcutEvents, ShortcutGroup::Screens, "Open events (native shortcut)"},
    {ShowSettings, ShortcutGroup::Screens, "Open settings"},
    {ToggleShortcutHints, ShortcutGroup::Interface, "Toggle shortcut hints"},
    {ZoomPreset1, ShortcutGroup::Camera, "Use zoom preset 1"},
    {ZoomPreset2, ShortcutGroup::Camera, "Use zoom preset 2"},
    {ZoomPreset3, ShortcutGroup::Camera, "Use zoom preset 3"},
    {ZoomPreset4, ShortcutGroup::Camera, "Use zoom preset 4"},
    {ZoomPreset5, ShortcutGroup::Camera, "Use zoom preset 5"},
    {ZoomIn, ShortcutGroup::Camera, "Zoom in"},
    {ZoomOut, ShortcutGroup::Camera, "Zoom out"},
    {ZoomMin, ShortcutGroup::Camera, "Zoom to minimum"},
    {ZoomMax, ShortcutGroup::Camera, "Zoom to maximum"},
    {ZoomReset, ShortcutGroup::Camera, "Reset zoom"},
    {UiScaleUp, ShortcutGroup::Interface, "Increase interface size"},
    {UiScaleDown, ShortcutGroup::Interface, "Decrease interface size"},
    {UiShipScaleUp, ShortcutGroup::Interface, "Increase ship panel size"},
    {UiShipScaleDown, ShortcutGroup::Interface, "Decrease ship panel size"},
    {UiViewerScaleUp, ShortcutGroup::Interface, "Increase viewer size"},
    {UiViewerScaleDown, ShortcutGroup::Interface, "Decrease viewer size"},
    {ActionPrimary, ShortcutGroup::Fleet, "Primary action"},
    {ActionSecondary, ShortcutGroup::Fleet, "Secondary action"},
    {ActionQueue, ShortcutGroup::Fleet, "Queue action"},
    {ActionQueueClear, ShortcutGroup::Fleet, "Clear action queue"},
    {ActionView, ShortcutGroup::Fleet, "View target details"},
    {ActionRecall, ShortcutGroup::Fleet, "Recall selected ship"},
    {ActionRecallCancel, ShortcutGroup::Fleet, "Cancel recall"},
    {ActionRepair, ShortcutGroup::Fleet, "Repair selected ship"},
    {SetZoomPreset1, ShortcutGroup::Camera, "Save zoom preset 1"},
    {SetZoomPreset2, ShortcutGroup::Camera, "Save zoom preset 2"},
    {SetZoomPreset3, ShortcutGroup::Camera, "Save zoom preset 3"},
    {SetZoomPreset4, ShortcutGroup::Camera, "Save zoom preset 4"},
    {SetZoomPreset5, ShortcutGroup::Camera, "Save zoom preset 5"},
    {SetZoomDefault, ShortcutGroup::Camera, "Save default zoom"},
    {DisableHotKeys, ShortcutGroup::Client, "Disable mod shortcuts"},
    {EnableHotKeys, ShortcutGroup::Client, "Enable mod shortcuts"},
    {ToggleQueue, ShortcutGroup::Fleet, "Toggle action queue"},
    {ToggleAutoConfirmInstantWarp, ShortcutGroup::Travel, "Cycle instant warp mode"},
    {TogglePreviewLocate, ShortcutGroup::Previews, "Toggle Locate on previews"},
    {TogglePreviewRecall, ShortcutGroup::Previews, "Toggle Recall on previews"},
    {ToggleCargoDefault, ShortcutGroup::Previews, "Toggle automatic cargo previews"},
    {ToggleCargoPlayer, ShortcutGroup::Previews, "Toggle player cargo previews"},
    {ToggleCargoStation, ShortcutGroup::Previews, "Toggle station cargo previews"},
    {ToggleCargoHostile, ShortcutGroup::Previews, "Toggle hostile cargo previews"},
    {ToggleCargoArmada, ShortcutGroup::Previews, "Toggle armada cargo previews"},
    {LogLevelDebug, ShortcutGroup::Diagnostics, "Set logging to Debug"},
    {LogLevelInfo, ShortcutGroup::Diagnostics, "Set logging to Info"},
    {LogLevelTrace, ShortcutGroup::Diagnostics, "Set logging to Trace"},
    {LogLevelError, ShortcutGroup::Diagnostics, "Set logging to Error"},
    {LogLevelWarn, ShortcutGroup::Diagnostics, "Set logging to Warning"},
    {LogLevelOff, ShortcutGroup::Diagnostics, "Turn logging off"},
    {Restart, ShortcutGroup::Client, "Clear localization cache and reload"},
    {Quit, ShortcutGroup::Client, "Force close client"},
    {FocusSearch, ShortcutGroup::Interface, "Focus search"},

    {ShowShipConstruction, ShortcutGroup::Screens, "Open ship construction"},
    {ShowShields, ShortcutGroup::Screens, "Open shields"},
    {ShowBattlelogs, ShortcutGroup::Screens, "Open battle logs"},
});
constexpr bool        ValidShortcutCatalog(std::span<const ShortcutInfo> catalog)
{
  std::array<bool, GameFunction::Max> seen{};
  for (const auto& info : catalog) {
    if (info.action < 0 || info.action >= GameFunction::Max || info.label.empty() || seen[info.action])
      return false;
    bool groupFound = false;
    for (const auto& group : ShortcutGroups)
      groupFound |= group.group == info.group;
    if (!groupFound)
      return false;
    seen[info.action] = true;
  }
  return true;
}
static_assert(ValidShortcutCatalog(ShortcutCatalog), "Shortcut overrides need unique valid actions, labels and groups");

inline std::string HumanizeShortcutKey(std::string_view key)
{
  std::string label;
  bool        space = false;
  for (char c : key) {
    if (c == '_' || c == '-' || c == ' ') {
      space = !label.empty();
      continue;
    }
    if (space)
      label += ' ';
    space = false;
    label += label.empty() && c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
  }
  return label.empty() ? "Shortcut" : label;
}

// Own generated text: descriptions survive sorting and never borrow temporary
// labels. The same resolver names editors, summaries and overlap warnings.
struct ShortcutDescription {
  GameFunction  action;
  ShortcutGroup group;
  std::string   label;
  bool          fallback;
};
inline ShortcutDescription DescribeShortcut(GameFunction action, std::string_view key,
                                            std::span<const ShortcutInfo> catalog = ShortcutCatalog)
{
  if (action < 0 || action >= GameFunction::Max)
    throw std::out_of_range("shortcut action");
  for (const auto& info : catalog)
    if (info.action == action)
      return {action, info.group, std::string(info.label), false};
  return {action, ShortcutGroup::Uncategorized, HumanizeShortcutKey(key), true};
}

// Discover registered actions once at startup, including unbound (NONE) actions.
// An empty key excludes an unregistered or unavailable action. This never scans
// TOML or infers a setting type from its value.
inline std::vector<ShortcutDescription> DiscoverShortcuts(const auto&                   registeredKey,
                                                          std::span<const ShortcutInfo> catalog = ShortcutCatalog)
{
  std::vector<ShortcutDescription> result;
  for (int i = 0; i < GameFunction::Max; ++i) {
    const auto action = static_cast<GameFunction>(i);
    const auto key    = registeredKey(action);
    if (!key.empty())
      result.push_back(DescribeShortcut(action, key, catalog));
  }
  // ASCII folding preserves existing English label ordering (Away Teams, etc.).
  std::stable_sort(result.begin(), result.end(), [](const auto& first, const auto& second) {
    return std::lexicographical_compare(
        first.label.begin(), first.label.end(), second.label.begin(), second.label.end(), [](char a, char b) {
          const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
          return lower(a) < lower(b);
        });
  });
  return result;
}
} // namespace mod_settings

// The game's keybind badges, relabelled with the mod's shortcuts. The game shows a badge per input action when
// its show_keybindings action is toggled; with mod hotkeys on, each badge shows the first binding of the mod
// action that replaces it.

#include "config.h"
#include "features/features.h"
#include "features/ship_shortcut_hint.h"
#include "shortcuts.h"

#include "game/fleet.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/tracker.h>

#include <array>
#include <string_view>

namespace
{
struct NativeAction {
  std::string_view name;
  Action           primary;
  Action           fallback = Action::kCount;
};

// The game's input action names and the mod actions that replace them.
constexpr auto kNativeActions = std::to_array<NativeAction>({
    {"interior_view", Action::kShowStationInterior},
    {"exterior_view", Action::kShowStationExterior},
    {"system_view", Action::kShowSystem},
    {"galaxy_view", Action::kShowGalaxy},
    {"events", Action::kShowEvents},
    {"ship_a", Action::kSelectShip1},
    {"ship_b", Action::kSelectShip2},
    {"ship_c", Action::kSelectShip3},
    {"ship_d", Action::kSelectShip4},
    {"ship_e", Action::kSelectShip5},
    {"ship_f", Action::kSelectShip6},
    {"ship_g", Action::kSelectShip7},
    {"ship_h", Action::kSelectShip8},
    {"alliance", Action::kShowAlliance},
    {"chat", Action::kShowChat},
    {"side_chat", Action::kShowChatSide1, Action::kShowChatSide2},
    {"away_teams", Action::kShowAwayTeam},
    {"missions", Action::kShowMissions},
    {"daily_goals", Action::kShowDaily},
    {"ship_locate", Action::kSelectCurrent},
    {"ship_manage", Action::kShowShips},
    {"research", Action::kShowResearch},
    {"consumables", Action::kShowExoComp},
    {"ship_recall", Action::kActionRecall},
    {"show_keybindings", Action::kToggleShortcutHints},
    {"gifts", Action::kShowGifts},
    {"help_alliance", Action::kShowAllianceHelp},
    {"officers", Action::kShowOfficers},
    {"factions", Action::kShowFactions},
    {"items", Action::kShowInventory},
    {"refinery", Action::kShowRefinery},
    {"commanders", Action::kShowCommander},
    {"challenges", Action::kShowQTrials},
});

constexpr const NativeAction* FindNativeAction(std::string_view name)
{
  for (const auto& action : kNativeActions)
    if (action.name == name)
      return &action;
  return nullptr;
}

consteval bool NativeActionsAreUnique()
{
  for (size_t i = 0; i < kNativeActions.size(); ++i)
    for (size_t j = i + 1; j < kNativeActions.size(); ++j)
      if (kNativeActions[i].name == kNativeActions[j].name)
        return false;
  return true;
}

static_assert(NativeActionsAreUnique());
static_assert(FindNativeAction("side_chat")->fallback == Action::kShowChatSide2);
static_assert(FindNativeAction("unknown") == nullptr);

// The badge text for a native action: its mod binding, or "-" when it has none.
std::string BadgeText(std::string_view action_name)
{
  const auto* action = FindNativeAction(action_name);
  if (!action)
    return "-";
  auto hint_action = action->primary;
  if (hint_action >= Action::kSelectShip1 && hint_action <= Action::kSelectShip8) {
    // Use the same context array as FleetBarViewController::RequestSelect(int, bool), not the native dock index.
    auto* bar     = unity::Newest<FleetBarViewController>();
    auto* context = bar ? bar->CanvasContext() : nullptr;
    auto* fleets  = context ? context->FleetData() : nullptr;
    hint_action   = ShipShortcutHintAction(hint_action, fleets ? fleets->size() : 0, [fleets](std::size_t position) {
      auto* fleet = (*fleets)[position];
      return fleet ? fleet->Index() : -1;
    });
  }
  auto hint = hint_action == Action::kCount ? std::string{} : Shortcut::Hint(hint_action);

  if (hint.empty() && action->fallback != Action::kCount)
    hint = Shortcut::Hint(action->fallback);
  return hint.empty() ? "-" : hint;
}

bool badges_installed = false;

void ShortcutKeybindHint_UpdateText_Hook(auto original, ShortcutKeybindHint* _this)
{
  auto* label = _this->keyTextLocalizer();
  if (!label)
    return original(_this);

  // UpdateText does not clear an existing override; clear ours first so native, disabled and unknown actions
  // cannot keep a stale mod badge.
  label->ClearTextOverride();
  original(_this);

  const auto& config = Config::Get();
  if (!config.control.hotkeys_enabled || config.control.use_scopely_hotkeys)
    return;

  auto* reference = _this->inputAction();
  auto* action    = reference ? reference->action() : nullptr;
  auto* name      = action ? action->name() : nullptr;
  if (name)
    label->OverrideLocalizedText(il2cpp::NewString(BadgeText(il2cpp::ToUtf8(name)).c_str()));
}

void ShortcutKeybindHint_UpdateVisibility_Hook(auto original, ShortcutKeybindHint* _this, bool visible)
{
  original(_this, visible);
  // A badge that becomes visible keeps the text it had while hidden. Update its text again, which runs the text hook,
  // so it shows the current binding.
  if (visible)
    _this->UpdateText();
}
} // namespace

bool ToggleKeybindBadges()
{
  if (!badges_installed)
    return false;
  auto* manager = ShortcutsManager::Instance();
  if (!manager)
    spdlog::warn("[Hotkeys] the game's shortcut manager is not up; no badges to toggle");
  // The same gate the game's own show_keybindings action checks.
  else if (ShortcutsManager::CanUseShortcuts())
    manager->SetShowKeybindings(!manager->ShowKeybindings());
  return true;
}

void HideKeybindBadges()
{
  auto* manager = badges_installed ? ShortcutsManager::Instance() : nullptr;
  if (manager && manager->ShowKeybindings())
    manager->SetShowKeybindings(false);
}

void InstallKeybindBadges()
{
  // Settings are parsed before installation; NONE, empty and invalid bindings opt out.
  if (!Shortcut::HasBinding(Action::kToggleShortcutHints))
    return;

  // Only the detour targets gate the feature. Members the badge path reads resolve on first use; a missing one is
  // reported once and reads as null, which leaves the game's own badge text.
  il2cpp::HookSet hooks("KeybindBadges");
  IL2CPP_STAGE(hooks, ShortcutKeybindHint::Class::Method("UpdateText", 0), ShortcutKeybindHint_UpdateText_Hook);
  IL2CPP_STAGE(hooks, ShortcutKeybindHint::Class::Method("UpdateVisibility", 1),
               ShortcutKeybindHint_UpdateVisibility_Hook);
  badges_installed = hooks.Commit();
}

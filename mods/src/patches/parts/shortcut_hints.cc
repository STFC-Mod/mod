// The game's keybind badges, relabelled with the mod's shortcuts. The game shows a badge per input action when
// its show_keybindings action is toggled; with mod hotkeys on, each badge shows the first binding of the mod
// action that replaces it.

#include "config.h"
#include "patches/object_tracker.h"
#include "patches/patches.h"
#include "patches/ship_shortcut_hint.h"
#include "patches/shortcuts.h"
#include "prime/fleet.h"
#include "str_utils.h"

#include <prime/ui.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <array>
#include <string_view>

namespace
{
struct InputAction : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.InputSystem", "UnityEngine.InputSystem", "InputAction">;

  Il2CppString* name()
  { return Get<"name", Il2CppString*>(); }
};

struct InputActionReference : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.InputSystem", "UnityEngine.InputSystem", "InputActionReference">;

  InputAction* action()
  { return Get<"action", InputAction*>(); }
};

struct ShortcutKeybindHint : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GameInput", "ShortcutKeybindHint">;

  InputActionReference* inputAction()
  { return Field<"_inputAction", InputActionReference*>(); }
  TextLocalizer* keyTextLocalizer()
  { return Field<"_keyTextLocalizer", TextLocalizer*>(); }
};

struct NativeAction {
  std::string_view name;
  GameFunction     primary;
  GameFunction     fallback = GameFunction::Max;
};

// The game's input action names and the mod actions that replace them.
constexpr auto kNativeActions = std::to_array<NativeAction>({
    {"interior_view", GameFunction::ShowStationInterior},
    {"exterior_view", GameFunction::ShowStationExterior},
    {"system_view", GameFunction::ShowSystem},
    {"galaxy_view", GameFunction::ShowGalaxy},
    {"events", GameFunction::ShowEvents},
    {"ship_a", GameFunction::SelectShip1},
    {"ship_b", GameFunction::SelectShip2},
    {"ship_c", GameFunction::SelectShip3},
    {"ship_d", GameFunction::SelectShip4},
    {"ship_e", GameFunction::SelectShip5},
    {"ship_f", GameFunction::SelectShip6},
    {"ship_g", GameFunction::SelectShip7},
    {"ship_h", GameFunction::SelectShip8},
    {"alliance", GameFunction::ShowAlliance},
    {"chat", GameFunction::ShowChat},
    {"side_chat", GameFunction::ShowChatSide1, GameFunction::ShowChatSide2},
    {"away_teams", GameFunction::ShowAwayTeam},
    {"missions", GameFunction::ShowMissions},
    {"daily_goals", GameFunction::ShowDaily},
    {"ship_locate", GameFunction::SelectCurrent},
    {"ship_manage", GameFunction::ShowShips},
    {"research", GameFunction::ShowResearch},
    {"consumables", GameFunction::ShowExoComp},
    {"ship_recall", GameFunction::ActionRecall},
    {"show_keybindings", GameFunction::ToggleShortcutHints},
    {"gifts", GameFunction::ShowGifts},
    {"help_alliance", GameFunction::ShowAllianceHelp},
    {"officers", GameFunction::ShowOfficers},
    {"factions", GameFunction::ShowFactions},
    {"items", GameFunction::ShowInventory},
    {"refinery", GameFunction::ShowRefinery},
    {"commanders", GameFunction::ShowCommander},
    {"challenges", GameFunction::ShowQTrials},
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
static_assert(FindNativeAction("side_chat")->fallback == GameFunction::ShowChatSide2);
static_assert(FindNativeAction("unknown") == nullptr);

// The badge text for a native action: its mod binding, or "-" when it has none.
std::string BadgeText(std::string_view action_name)
{
  const auto* action = FindNativeAction(action_name);
  if (!action)
    return "-";
  auto hint_action = action->primary;
  if (hint_action >= GameFunction::SelectShip1 && hint_action <= GameFunction::SelectShip8) {
    // Use the same context array as FleetBarViewController::RequestSelect(int, bool), not the native dock index.
    auto* bar     = ObjectFinder<FleetBarViewController>::Get();
    auto* context = bar ? bar->CanvasContext() : nullptr;
    auto* fleets  = context ? context->FleetData() : nullptr;
    hint_action   = ShipShortcutHintAction(hint_action, fleets ? fleets->size() : 0, [fleets](std::size_t position) {
      auto* fleet = (*fleets)[position];
      return fleet ? fleet->Index() : -1;
    });
  }
  auto hint = hint_action == GameFunction::Max ? std::string{} : Shortcut::Hint(hint_action);

  if (hint.empty() && action->fallback != GameFunction::Max)
    hint = Shortcut::Hint(action->fallback);
  return hint.empty() ? "-" : hint;
}

using UpdateTextFn = void(ShortcutKeybindHint*);

bool          installed            = false;
UpdateTextFn* original_update_text = nullptr;

void ShortcutKeybindHint_UpdateText_Hook(auto original, ShortcutKeybindHint* _this)
{
  auto* label = _this && installed ? _this->keyTextLocalizer() : nullptr;
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
    label->OverrideLocalizedText(il2cpp_string_new(BadgeText(to_string(name)).c_str()));
}

void ShortcutKeybindHint_UpdateVisibility_Hook(auto original, ShortcutKeybindHint* _this, bool visible)
{
  original(_this, visible);
  if (visible && installed && original_update_text)
    ShortcutKeybindHint_UpdateText_Hook(original_update_text, _this);
}
} // namespace

bool ToggleShortcutBadges()
{
  if (!installed)
    return false;
  auto* manager = ShortcutsManager::Instance();
  if (!manager)
    spdlog::warn("[Hotkeys] native shortcut hints are unavailable");
  // The same gate the game's own show_keybindings action checks.
  else if (ShortcutsManager::CanUseShortcuts())
    manager->SetShowKeybindings(!manager->ShowKeybindings());
  return true;
}

void HideShortcutBadges()
{
  auto* manager = installed ? ShortcutsManager::Instance() : nullptr;
  if (manager && manager->ShowKeybindings())
    manager->SetShowKeybindings(false);
}

void InstallShortcutHintHooks()
{
  // Settings are parsed before installation; NONE, empty and invalid bindings opt out.
  if (!Shortcut::HasBinding(GameFunction::ToggleShortcutHints))
    return;

  // Only the detour targets gate the feature. Members the badge path reads resolve on first use; a missing one is
  // reported once and reads as null, which leaves the game's own badge text.
  auto update_text       = ShortcutKeybindHint::Class::Method("UpdateText", 0);
  auto update_visibility = ShortcutKeybindHint::Class::Method("UpdateVisibility", 1);
  if (!update_text || !update_visibility)
    return;

  original_update_text = SPUD_STATIC_DETOUR(update_text.Pointer(), ShortcutKeybindHint_UpdateText_Hook);
  SPUD_STATIC_DETOUR(update_visibility.Pointer(), ShortcutKeybindHint_UpdateVisibility_Hook);
  installed = true;
}

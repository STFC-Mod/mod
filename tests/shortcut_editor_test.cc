#include "settings/shortcut_capture.h"
#include "settings/shortcut_catalog.h"
#include "settings/shortcut_draft.h"
#include "settings/shortcut_popup_keys.h"
#include "settings/shortcut_undo.h"
#include <cstdlib>
#include <iostream>

using namespace mod_settings;
void Check(bool ok, const char* label)
{
  if (!ok) {
    std::cerr << label << '\n';
    std::exit(1);
  }
}
int main()
{
  constexpr auto limit = ShortcutBindingDisplayLimit;
  Check(ShortcutCountFitsEdit(limit - 1, limit) && !ShortcutCountFitsEdit(limit, limit + 1),
        "native row budget rejects oversized growth before publication");
  Check(ShortcutCountFitsEdit(62, 62) && ShortcutCountFitsEdit(62, 61) && !ShortcutCountFitsEdit(62, 63),
        "existing oversized TOML lists can be rebound/reduced but not grown in UI");
  Check(VisibleShortcutBindingCount(0) == 0 && VisibleShortcutBindingCount(1) == 1,
        "empty/single-binding pages have no extra binding rows");
  for (std::size_t count : {61u, 62u, 1000u}) {
    Check(VisibleShortcutBindingCount(count) == limit,
          "existing long lists present a bounded prefix without dropping other settings pages");
    Check(2 * VisibleShortcutBindingCount(count) + ShortcutFixedRowLimit <= PageCatalog::NativeChildLimit,
          "Change and Remove plus fixed rows fit native page capacity");
  }
  Check(DescribeShortcut(ShowInventory, "show_inventory").group == ShortcutGroup::Screens
            && DescribeShortcut(ShowArtifacts, "show_artifacts").group == ShortcutGroup::Screens,
        "opening inventory/artifacts belongs to game screens");
  Check(DescribeShortcut(ToggleAutoConfirmInstantWarp, "toggle_instant_warp").group == ShortcutGroup::Travel,
        "instant warp shortcut belongs to map and travel");
  Check(DescribeShortcut(Restart, "restart").label == "Clear localization cache and reload",
        "cache-clearing shortcut must not promise a plain restart");
  Check(DescribeShortcut(Quit, "quit").label == "Force close client" && !ShortcutExplanation(Quit).empty(),
        "force close must be identified before binding it");

  // A sparse override list models adding an action without editing UI metadata.
  // Discovery depends on registered keys, never on whether a binding is present.
  constexpr auto partial = std::to_array<ShortcutInfo>({
      {MoveRight, ShortcutGroup::Camera, "Pan right"},
      {ShowArtifacts, ShortcutGroup::Screens, "Open artifacts"},
  });
  static_assert(ValidShortcutCatalog(partial));
  constexpr auto duplicates = std::to_array<ShortcutInfo>({
      {MoveRight, ShortcutGroup::Camera, "Pan right"},
      {MoveRight, ShortcutGroup::Travel, "Duplicate"},
  });
  static_assert(!ValidShortcutCatalog(duplicates));
  constexpr auto invalidAction = std::to_array<ShortcutInfo>({{GameFunction::Max, ShortcutGroup::Camera, "Invalid"}});
  constexpr auto invalidGroup  = std::to_array<ShortcutInfo>({{MoveRight, static_cast<ShortcutGroup>(99), "Invalid"}});
  constexpr auto emptyLabel    = std::to_array<ShortcutInfo>({{MoveRight, ShortcutGroup::Camera, ""}});
  static_assert(!ValidShortcutCatalog(invalidAction) && !ValidShortcutCatalog(invalidGroup)
                && !ValidShortcutCatalog(emptyLabel));
  std::array<std::string_view, GameFunction::Max> registered{};
  registered[MoveLeft]  = "toggle_new_feature";
  registered[MoveRight] = "move_right";
  registered[MoveUp]    = "test_value_2";
  auto       key        = [&](GameFunction action) { return registered[action]; };
  const auto discovered = DiscoverShortcuts(key, partial);
  Check(discovered.size() == 3 && discovered[0].action == MoveRight && discovered[0].label == "Pan right"
            && !discovered[0].fallback && discovered[0].group == ShortcutGroup::Camera,
        "registered actions keep explicit labels and categories; metadata alone cannot expose an unregistered action");
  Check(discovered[1].action == MoveUp && discovered[1].label == "Test value 2" && discovered[1].fallback
            && discovered[2].action == MoveLeft && discovered[2].label == "Toggle new feature"
            && discovered[2].group == ShortcutGroup::Uncategorized,
        "uncatalogued registered actions are discovered and sorted with readable labels");
  const auto promoted = std::to_array<ShortcutInfo>({{MoveLeft, ShortcutGroup::Interface, "Toggle polished feature"}});
  const auto description = DescribeShortcut(MoveLeft, registered[MoveLeft], promoted);
  Check(description.action == discovered[2].action && description.group == ShortcutGroup::Interface
            && description.label == "Toggle polished feature" && !description.fallback,
        "a later override changes presentation without changing action identity");
  registered[MoveLeft] = {};
  Check(DiscoverShortcuts(key, partial).size() == 2,
        "startup availability filtering excludes actions before fallback discovery");
  registered = {};
  Check(DiscoverShortcuts(key, partial).empty(), "an empty registry cannot create fallback editors");
  Check(ShortcutGroups[ShortcutGroups.size() - 2].group == ShortcutGroup::Uncategorized
            && ShortcutGroups.back().group == ShortcutGroup::Diagnostics,
        "Uncategorized precedes Diagnostics at the bottom");
  Check(HumanizeShortcutKey("__open--panel_2__") == "Open panel 2" && HumanizeShortcutKey("__") == "Shortcut",
        "fallback labels collapse separators, retain numbers and never become blank");

  registered            = {};
  registered[MoveUp]    = "move_up";
  registered[MoveDown]  = "move_down";
  registered[MoveLeft]  = "move_left";
  registered[MoveRight] = "move_right";
  const auto movement   = DiscoverShortcuts(key);
  Check(movement.size() == 4, "all registered movement actions have in-game editors");
  for (const auto& entry : movement)
    Check(entry.group == ShortcutGroup::Camera && !entry.fallback && !ShortcutExplanation(entry.action).empty(),
          "movement editors have Camera labels and help text");

  ShortcutList               live{"LCTRL-G", "F8"};
  int                        writes = 0;
  ValueSetting<ShortcutList> owner({"shortcuts.test", "Test",
                                    [&] { return ValueReadResult<ShortcutList>::Known(live, 1); },
                                    [&](ShortcutList value, std::uint64_t) {
                                      ++writes;
                                      live = value;
                                      return ApplyResult::Applied;
                                    }});
  ShortcutDraft              draft(owner);
  draft.Begin();
  draft.Stage(1, "ALT-I");
  Check(writes == 0 && live == ShortcutList{"LCTRL-G", "F8"}, "recording must not write");
  Check(draft.Apply() == Outcome::AppliedVerified && live == ShortcutList{"LCTRL-G", "ALT-I"},
        "replace preserves alternatives and sided modifiers");
  draft.Begin();
  draft.Stage(2, "F9");
  draft.Cancel();
  Check(draft.Apply() == Outcome::Suppressed && writes == 1, "cancel never writes");
  draft.Begin();
  draft.Stage(2, "F9");
  Check(draft.Apply() == Outcome::AppliedVerified && live.size() == 3, "append preserves previous alternatives");
  draft.Begin();
  live = {"F10"};
  draft.Stage(0, "F7");
  Check(draft.Apply() == Outcome::Conflict && live == ShortcutList{"F10"},
        "change during capture cannot be overwritten");
  draft.Begin();
  draft.Stage(0, {});
  Check(draft.Apply() == Outcome::AppliedVerified && live.empty(), "explicit removal can unbind last key");
  draft.Begin();
  draft.Stage(0, "CTRL-I");
  Check(draft.Apply() == Outcome::AppliedVerified && live == ShortcutList{"CTRL-I"}, "unbound action can be rebound");
  const auto beforeDuplicates = writes;
  draft.Begin();
  Check(draft.Stage(1, "CTRL-I") == ShortcutStage::AlreadyBound && !draft.pending(),
        "append must reject an existing binding");
  Check(draft.Apply() == Outcome::Suppressed && writes == beforeDuplicates,
        "duplicate append must not publish or save");
  draft.Begin();
  Check(draft.Stage(0, "CTRL-I") == ShortcutStage::AlreadyBound && !draft.pending(),
        "re-recording the selected binding is a no-op");
  live = {"SHIFT-I", "ALT-I"};
  draft.Begin();
  Check(draft.Stage(1, "F8") == ShortcutStage::Staged && draft.pending(), "stage another alternative");
  Check(draft.Stage(1, "SHIFT-I") == ShortcutStage::AlreadyBound && !draft.pending(),
        "duplicate replacement clears any preceding draft");
  Check(draft.Apply() == Outcome::Suppressed && writes == beforeDuplicates && live == ShortcutList{"SHIFT-I", "ALT-I"},
        "duplicate replacement preserves all alternatives without writing");
  live = {"SHIFT-I", "SHIFT-I", "ALT-I"};
  draft.Begin();
  Check(writes == beforeDuplicates && live.size() == 3, "opening does not clean up existing duplicates");
  Check(draft.Stage(0, {}) == ShortcutStage::Staged && draft.Apply() == Outcome::AppliedVerified
            && live == ShortcutList{"SHIFT-I", "ALT-I"} && writes == beforeDuplicates + 1,
        "explicit removal can clean up one existing duplicate without losing the shortcut");
  const auto beforeRestore = writes;
  draft.Begin();
  Check(draft.Restore({"F8", "CTRL-G"}) == ShortcutStage::Staged && writes == beforeRestore,
        "restoring defaults stages a full action without writing");
  draft.Cancel();
  Check(draft.Apply() == Outcome::Suppressed && writes == beforeRestore,
        "cancelling default restoration preserves current bindings");
  draft.Begin();
  draft.Restore({"F8", "CTRL-G"});
  live = {"F10"};
  Check(draft.Apply() == Outcome::Conflict && live == ShortcutList{"F10"},
        "restoration cannot overwrite an action changed since staging");
  draft.Begin();
  Check(draft.Restore({}) == ShortcutStage::Staged && draft.Apply() == Outcome::AppliedVerified && live.empty(),
        "a NONE default explicitly unbinds the action");
  draft.Begin();
  Check(draft.Restore({}) == ShortcutStage::AlreadyBound && !draft.pending(),
        "restoring an already active default is a no-op");
  ShortcutUndo removal(owner);
  live                     = {"LCTRL-G", "F8", "LCTRL-G"};
  const auto beforeRemoval = writes;
  Check(removal.Remove(1) == Outcome::AppliedVerified && writes == beforeRemoval + 1
            && live == ShortcutList{"LCTRL-G", "LCTRL-G"} && removal.available() && removal.message() == "Removed F8",
        "Remove writes immediately without a pending Apply and offers the removed key for Undo");
  Check(removal.Undo() == Outcome::AppliedVerified && writes == beforeRemoval + 2
            && live == ShortcutList{"LCTRL-G", "F8", "LCTRL-G"} && !removal.available(),
        "Undo writes immediately and restores ordering, sided modifiers and existing duplicates exactly");
  Check(removal.Undo() == Outcome::Suppressed && writes == beforeRemoval + 2, "repeated Undo cannot write again");
  live = {"F8"};
  Check(removal.Remove(0) == Outcome::AppliedVerified && live.empty() && removal.Undo() == Outcome::AppliedVerified
            && live == ShortcutList{"F8"},
        "removing the last binding makes the action unbound and remains undoable");
  removal.Remove(0);
  live                       = {"F9"};
  const auto beforeStaleUndo = writes;
  Check(removal.Undo() == Outcome::Conflict && live == ShortcutList{"F9"} && writes == beforeStaleUndo
            && !removal.available(),
        "stale Undo cannot overwrite a newer binding or persist it");
  live = {"F7", "F8", "F9"};
  removal.Remove(0);
  removal.Remove(0);
  Check(removal.message() == "Removed F8" && removal.Undo() == Outcome::AppliedVerified
            && live == ShortcutList{"F8", "F9"},
        "Undo belongs to the most recent removal only");
  Check(removal.Remove(99) == Outcome::Rejected && !removal.available(), "invalid removal cannot offer Undo");
  removal.Remove(0);
  removal.Clear();
  const auto afterClear = writes;
  Check(removal.Undo() == Outcome::Suppressed && writes == afterClear,
        "leaving the editor or starting another edit clears Undo without changing bindings");
  live                     = {"LEFT", "N"};
  const auto beforeDefault = writes;
  Check(removal.Restore({"LEFT"}) == Outcome::AppliedVerified && live == ShortcutList{"LEFT"}
            && writes == beforeDefault + 1 && removal.message() == "Restored defaults" && removal.available(),
        "Restore replaces the complete list and persists immediately, without pending Apply");
  Check(removal.Undo() == Outcome::AppliedVerified && live == ShortcutList{"LEFT", "N"} && writes == beforeDefault + 2,
        "Undo default restoration immediately restores every previous alternative");
  Check(removal.Restore({}) == Outcome::AppliedVerified && live.empty() && removal.Undo() == Outcome::AppliedVerified
            && live == ShortcutList{"LEFT", "N"},
        "NONE defaults apply immediately and can be undone");
  const auto beforeNoopRestore = writes;
  Check(removal.Restore(live) == Outcome::Unchanged && writes == beforeNoopRestore && !removal.available(),
        "restoring an already matching default neither writes nor advertises Undo");
  removal.Restore({"LEFT"});
  live                             = {"RIGHT"};
  const auto beforeRestoreConflict = writes;
  Check(removal.Undo() == Outcome::Conflict && live == ShortcutList{"RIGHT"} && writes == beforeRestoreConflict,
        "Undo restoration cannot overwrite a subsequent edit");
  {
    ShortcutList               oversized(limit + 2, "F8");
    ShortcutUndo*              activeRemoval = nullptr;
    bool                       reject        = false;
    ValueSetting<ShortcutList> boundedOwner({"shortcuts.large", "Large",
                                             [&] { return ValueReadResult<ShortcutList>::Known(oversized, 1); },
                                             [&](ShortcutList value, std::uint64_t) {
                                               if (reject
                                                   || (!ShortcutCountFitsEdit(oversized.size(), value.size())
                                                       && !(activeRemoval && activeRemoval->Restoring(value))))
                                                 return ApplyResult::Rejected;
                                               oversized = std::move(value);
                                               return ApplyResult::Applied;
                                             }});
    ShortcutUndo               largeRemoval(boundedOwner);
    activeRemoval = &largeRemoval;
    Check(largeRemoval.Remove(0) == Outcome::AppliedVerified && oversized.size() == limit + 1
              && largeRemoval.Undo() == Outcome::AppliedVerified && oversized.size() == limit + 2,
          "oversized TOML lists permit an exact removal undo");
    auto addition = oversized;
    addition.push_back("F9");
    Check(boundedOwner.SetFromUser(addition, boundedOwner.Observe()).outcome == Outcome::Rejected,
          "Undo does not grant ordinary additions permission to exceed the row budget");
    Check(largeRemoval.Restore({"LEFT"}) == Outcome::AppliedVerified && largeRemoval.Undo() == Outcome::AppliedVerified
              && oversized.size() == limit + 2,
          "Undo default restoration can recover the original oversized player list");
    reject = true;
    Check(largeRemoval.Remove(0) == Outcome::Rejected && !largeRemoval.available() && oversized.size() == limit + 2,
          "failed removal cannot offer success or Undo");
    Check(largeRemoval.Restore({"LEFT"}) == Outcome::Rejected && !largeRemoval.available()
              && oversized.size() == limit + 2,
          "failed restoration cannot offer success or Undo");
  }
  ShortcutCapture   capture;
  ShortcutPopupKeys popupKeys;
  Check(popupKeys.Tick(false, true, true, false) == ShortcutPopupKey::None, "Enter during recording cannot confirm");
  Check(popupKeys.Tick(true, true, true, false) == ShortcutPopupKey::None,
        "captured Enter cannot confirm itself when the preview becomes ready");
  Check(popupKeys.Tick(true, false, false, false) == ShortcutPopupKey::None
            && popupKeys.Tick(true, true, true, false) == ShortcutPopupKey::Confirm,
        "after release a fresh Enter confirms a ready preview");
  Check(popupKeys.Tick(true, true, false, false) == ShortcutPopupKey::None, "holding Enter cannot repeat confirmation");
  popupKeys.Tick(true, false, false, false);
  Check(popupKeys.Tick(true, true, true, true) == ShortcutPopupKey::Cancel,
        "Escape takes priority over simultaneous confirmation");
  Check(popupKeys.Tick(false, false, false, true) == ShortcutPopupKey::Cancel,
        "Escape cancels even when confirmation is unavailable");
  popupKeys.Tick(true, false, false, false);
  popupKeys.Tick(false, false, false, false);
  Check(popupKeys.Tick(true, true, true, false) == ShortcutPopupKey::None,
        "Record again resets confirmation readiness");
  ShortcutCapture::Keys held{}, down{};
  auto                  modifier = [](KeyCode key) { return key == KeyCode::LeftControl; };
  auto                  tick     = [&] { return capture.Tick(held, down, true, modifier); };
  capture.Begin();
  held[(int)KeyCode::Mouse0] = true;
  down[(int)KeyCode::Mouse0] = true;
  Check(!tick() && !capture.listening(), "opening click cannot be captured");
  held = {};
  down = {};
  tick();
  Check(capture.listening(), "all opening keys must release");
  held[(int)KeyCode::LeftControl] = true;
  down[(int)KeyCode::LeftControl] = true;
  Check(!tick() && capture.listening(), "modifier alone is not a binding");
  down                  = {};
  held[(int)KeyCode::I] = true;
  down[(int)KeyCode::I] = true;
  Check(tick() == KeyCode::I && capture.active(), "record primary but retain input ownership");
  down = {};
  tick();
  Check(capture.active(), "held captured chord cannot leak to gameplay");
  held[(int)KeyCode::I] = false;
  tick();
  Check(capture.active(), "modifier release also required");
  held = {};
  tick();
  Check(!capture.active(), "release ends ownership");
  capture.Begin();
  tick();
  held[(int)KeyCode::Escape] = true;
  down[(int)KeyCode::Escape] = true;
  Check(!tick() && capture.active(), "escape cancels instead of binding");
  down = {};
  held = {};
  tick();
  Check(!capture.active(), "escape release ends cancellation drain");
  capture.Begin();
  tick();
  held[(int)KeyCode::I] = true;
  down[(int)KeyCode::I] = true;
  Check(!capture.Tick(held, down, false, modifier) && capture.active(), "focus loss cancels and drains");
  held = {};
  down = {};
  capture.Tick(held, down, false, modifier);
  Check(capture.active(), "an empty unfocused input sample must not end ownership");
  held[(int)KeyCode::I] = true;
  tick();
  Check(capture.active(), "refocusing with the chord still held remains blocked");
  held = {};
  tick();
  Check(!capture.active(), "a focused release ends the cancellation drain");
  // The popup can close while capture is idle (preview/confirmation). Its
  // closing input must be drained just like a captured chord.
  capture.Begin();
  capture.Cancel();
  held[(int)KeyCode::Return] = true;
  down                       = held;
  Check(!tick() && capture.active(), "closing preview retains the submit key");
  held = {};
  down = {};
  Check(!capture.Tick(held, down, false, modifier) && capture.active(),
        "closing preview cannot release ownership on an unfocused sample");
  tick();
  Check(!capture.active(), "closing preview releases ownership after focused key release");
  capture.Begin();
  tick();
  held[(int)KeyCode::I] = held[(int)KeyCode::G] = true;
  down                                          = held;
  Check(!tick() && !capture.listening(), "two simultaneous primary keys are ambiguous");
  std::cout << "Shortcut draft and capture fixtures passed\n";
}

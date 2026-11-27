# Experimental occupied mining-node inspection

This contribution seeks help completing a native inspection cycle:

mining-node information -> occupying ship loot -> ship strength -> original mining-node information.

**The complete cycle has not been demonstrated successfully.** The current PR
selects the occupying player fleet and requests its native rewards panel. Native
ActionView handles rewards/strength after selection. Returning to the original
node is the unresolved step. This is an experimental contribution, not a claim
of a completed feature.

## Build validation

The separate saved-node restoration experiment has compiled as a Windows x64
release DLL and passed its identity/binding regressions. Its installer also passed
embedded-payload and install/rollback checks in [the isolated test workflow](https://github.com/MorbidKittyLabs/Run17-builds/actions/runs/46022115033).
These are build/fixture results, not a successful in-game inspection cycle.

## show_info_pending: origin and revision scope

The PR is based on `main` at `8b7013e4643e85b4fe08e849d5f211d594b88c8e`.
In that revision, `mods/src/patches/parts/hotkeys.cc` already declares:

```cpp
static int show_info_pending = 0;
```

It is a translation-unit-local, short-lived cargo display request counter. The
ActionView handler sets it to 5. During subsequent eligible ScreenManager update
calls, the existing loop looks for a visible PreScanTargetWidget, obtains its
rewards controller, calls Show(true) if needed, and decrements the counter.
Five means update attempts, not seconds. No mining-node identity is stored there.

The declaration is at line 76; the delayed-show loop is at lines 1234-1254 in that
base. The original contribution adds an assignment after selecting the occupant,
because the new native ship card/rewards controller may bind on a later frame.
The declaration and loop were not omitted from this base; they are unchanged
upstream code and therefore absent from the PR diff.

This state was already present in the original public source commit
`875c617ea993509b16ec83962f961bb55fbb5eda` (2023-02-22), in
`src/patches/parts/hotkeys.cc:34`. The 2024 xmake layout change moved that code
under mods/src. It did not originate with this contribution.

The current dev revision inspected was
`abae1d5353b89a44b2b2598da2ddf19c7ac63c9c`. Its implementation differs: cargo
state lives in `src/mod/features/fleet_actions.cc` as `show_cargo_frames`, with
ToggleTargetCargo/ShowPendingTargetCargo. The old show_info_pending global is
absent. The November 14 ownership refactor (#366,
`0af9b261b5b95ee7ea1d6e5bf8080d9f253697b0`) removed it from the old hotkey file.
This explains a possible main/dev mismatch; it does not establish which revision
was being reviewed. Porting to dev should integrate with its current cargo owner,
rather than adding a duplicate global declaration.

## Dependencies missing from the original PR extract

The original PR changed only hotkeys.cc and referenced three wrappers that were
not supplied by the pinned upstream headers:

- FleetDeployedData::GetId64(): reads the managed Int64 ID without truncating it
  to Windows C++ long (32-bit).
- NavigationManager::CanSelectPoi(Int64): native Boolean-returning selection check.
- NavigationManager::SelectPoi(Int64): native Boolean-returning selection entry.

These are now included, together with navigation_binding.h, which checks the
managed method signature and handles managed invocation failures. Existing
TryGetFleetType() is already present in the PR base. No duplicate
show_info_pending declaration is needed for that base.

## Historical inspection evidence

In the recovered inspection-only diagnostic sequence 112-125, the mining scan
buttons' battle context becomes null as the mining viewer hides, and the same
battle context/fleet subsequently binds to PreScanTargetWidget. The target card
starts showing about 500 ms after the ActionView request. The original node POI
identity was not logged. These observations do not establish any valid hidden
widget restoration path and must not be treated as evidence that a battle context
is the inherited navigation context. A bounded wait for the matching live target
is more reliable than assuming the new card is available immediately.

## Findings relevant to restoring the node

The recovered October 24 GameAssembly.dll/global-metadata.dat pair was inspected
with Il2CppDumper (metadata version 31), and relevant native code was disassembled.
This verifies the recovered client, not all subsequent client versions.

- MiningObjectViewerWidget inherits ObjectViewerBaseWidget, which inherits
  Widget<NavigationInteractionUIContext>. Its inherited Context is distinct from
  ScanEngageButtonsWidget.Context (BattleTargetData).
- NavigationInteractionUIContext has a public POI reference field named Poi.
  POI exposes Int64 ID and NodeAddress Address.
- NavigationManager.CanSelectPoi(Int64) and SelectPoi(Int64) both return Boolean.
  Native code looks up the live POI with POI.Get(Int64), then SelectPoi forwards
  that POI through native selection. These are not Scan calls.
- MiningObjectViewerWidget.ScanButtonClickedEventHandler jumps to its
  SetPoiToSelect method, which reads inherited Context.Poi.ID for native return
  selection. This is evidence for capturing the original POI identity before
  selecting the fleet; the scan handler itself should not be invoked to cycle.
- NavigationManager.ViewData.ViewingAddress exposes the current NodeAddress;
  Galaxy/System/Instance can distinguish navigation state without retaining
  pooled viewer pointers.

A restoration experiment is being tested separately: capture original node POI
ID and galaxy/system/instance, keep occupant fleet ID separately, and on the third
ActionView press resolve and select the original POI. Confirm the actual native
mining viewer has rebound before clearing inspection state. Clear state on
unrelated selection/section changes and report failed restoration explicitly.
The node is a saved identity, not a saved hidden widget or battle context.

Native Scan, MineClicked and OnEngageButtonClicked routing is unchanged. Saved
inspection IDs should never override native attack targeting. A replacement
occupant must be read afresh on the next node inspection.

Help is requested confirming the preferred selection/restoration entry point
and how this should integrate with the current dev cargo owner. The unresolved
behavior and revision mismatch should not be hidden behind a successful compile.

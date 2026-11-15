# Kir'Shara queue address guard

Moving or recalling another ship in a different system can temporarily make a
deployed ship's native player address point at its starbase. A subsequent native
player-state event then treats that ship's same-system queued targets as foreign
and clears its queue. This failure was also reproduced by a tester with the mod
DLL disabled.

The default-on guard substitutes the ship's current owned deployment address only
inside the native queue address comparison during a player-state event. It requires
a validated nonempty queue belonging to that ship and slot, a local deployment
whose fleet ID matches, no recall or removal, and an active non-warping ship state.
The reported address must be a planet in another system, and the current target
must match the deployment's full galaxy/system/planet/instance address.

The native comparison still checks every target. A missing target, a genuinely
foreign address, or unknown metadata retains native behavior. The guard writes no
fleet fields or queue contents and retains no address between events. Explicit
clearing, recalling the queued ship itself, and genuine system changes remain
native. This is enabled together with Faster Queue Recovery and Thin Queue Protection
through `control.kirshara_fixes`.

`[control] kirshara_fixes = false` disables the combined fixes, including address correction. The separate
default-on `[patches] queueaddressguardhooks` switch controls hook installation;
changing it requires a restart. Disabling the queue also disables correction.

The implementation is shared by Windows and macOS and contains no capture logger,
polling loop, or diagnostic hooks. It owns four native methods: the player-state
event handler, the queue address predicate, the player address getter, and the
deployment lookup. Their Windows x64 client 271 native overwrite windows were
measured against the matching installed client. Current Mac native hook fit and
gameplay validation have not been established; successful builds alone do not
establish those properties.

Run `tests/run-action-queue.ps1` for local regression checks. Gameplay validation
must use the clean build: queue A's same-system hostiles, move/recall B in another
system, and allow A to engage its next target. Then recall A and check its queue
clears normally. The prior instrumented build passed both controls, and reproduced
the raw address fault even with fleet observation polling disabled.

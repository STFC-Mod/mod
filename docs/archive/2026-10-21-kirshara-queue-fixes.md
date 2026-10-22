# Kir'Shara queue fixes: design notes (2026-10-21)

Archived from the per-fix guides that accompanied #287, #313 and the address guard. The live user-facing summary is in [FEATURES.md](../FEATURES.md); the code is `src/mod/features/action_queue.cc` and its tests are `tests/action_queue.cc` (`zig build test-mod`).

Since these notes were written, the fixes moved onto typed bindings: method signatures and member types are checked when they resolve, and no field offsets are pinned. Startup no longer logs `ready=` lines; a missing member is reported by the binding library instead. #339 then enabled recovery and thin protection on macOS; its qualification notes are at the end.

## Faster Queue Recovery

When a queued target disappears while a course request is outstanding, the game can remove that target before its failed response arrives. Native failure handling then attempts the removal again. Since it did not remove the front entry this time, it can leave the next target waiting for the watchdog.

Recovery is enabled by default as part of the combined Kir'Shara fixes:

```toml
[control]
kirshara_fixes = true
```

Following #313, default-enabled `[patches].actionqueuerecoveryhooks` independently controls installation. Changing that installation switch requires a restart. Installed callbacks check the current `control.kirshara_fixes` and `control.queue_enabled` preferences; changing TOML still requires reload/restart.

The feature also respects `control.queue_enabled`. Hooks are supported on Windows x64 when the required method signatures and queue layout are compatible. Other platforms do not install these hooks; incompatible Windows layouts log an unavailable message and retain native behavior.

The previous `faster_queue_recovery`, `thin_queue_protection`, `queue_address_guard`
and `advanced.queue.thin_queue_protection` settings no longer control these features.
Use the single `kirshara_fixes` switch; its default is true. A legacy-only configuration
logs this replacement at startup. No existing config file is rewritten automatically.

Toggling queue entry off invalidates only the mod's pending recovery tracking. It
does not clear native target queues or stop their native processing. Forgetting the
tracking prevents an old course response from authorizing a later recovery after
the queue is toggled back on.

### Behavior

The adapter records the latest engagement attempt for up to eight fleets, using weak queue identities and full 64-bit target IDs. A failed, non-recall course response may request native planning only when:

- The response belongs to the same queue and latest target/attempt, observed within 30 seconds.
- The queue was engaging on entry to the response and native processing cleared that flag.
- A different front target remains, and the failed target is absent from every inspected queue.
- The native retry decision was false. Ordinary retries are unchanged.

The request record is consumed once. The native planner selects and validates the next target. The mod does not force ship state, clear engagement flags, change retry counts, or remove targets. If another fleet still queues the target, the native cross-fleet removal path remains responsible.

Queue storage is bounded and validated. Unknown layouts/content are ineligible. Expired/replaced requests release their weak handles; native session cleanup clears all records. Engage, Retry and Course discard pending recovery records when they observe disabled behavior. Both Toggle Queue transitions also clear them, so an off/on toggle cannot reuse authorization from before the toggle. A native attempt returning skip/stop cancels only its own record, preserving newer reentrant attempts.

There is no watchdog hook, frame scan, timer, background worker, or per-engagement logging. Queue inspection occurs in existing callbacks; all-queue inspection runs only for a potentially eligible failed response. Response matching is not a server-issued request ID, so delayed same-target responses remain an interoperability limitation.

### Native integration

One detour owns each method. Methods are resolved through their complete managed signatures. Installation validates the event and queue field layouts and the engagement result's integer representation. SPUD builds the trampolines using its existing instruction decoder. Addresses and relocation-dependent instruction bytes are not pinned to a client release.

The hooked methods are `TryPlanPathAndEngageTarget`, `ShouldRetryFailedSetCourse`, `OnSetCourseResponseEventHandler` and `StopWatchdogAndClearAllQueues`. These checks establish binding compatibility; changes to the native retry/planner behavior still require update review and runtime testing.

The course event is a 24-byte value type, with fleet ID at 0, success/recall at 8/9 and boxed target at 16. Metadata field offsets include the boxed object header. The native retry handler is called synchronously inside the course handler; returning true selects its existing planner branch.

The older off-screen Kir'Shara combat-completion repair was fixed by Scopely. That workaround and its `kirshara_queue_repair` setting have already been removed; neither is a prerequisite for Faster Queue Recovery. This feature addresses only the separate unavailable-target/course-response race described above.

### Validation

Run `zig build test-mod`.cc` with a C++23 compiler and `-Isrc/mod`. Tests use the production policy and request store with fake weak handles. They cover one-shot recovery, reordered/still-present targets, cross-fleet rejection, stale attempts, replaced/collected queues, expiry, reentrant cancellation, bounded capacity and cleanup. They do not model game ABI or network scheduling.

The prototype produced two observed handoffs with the next target attempted 1–2 ms later and successful responses within 388–532 ms. Those timings are observations, not a latency guarantee. The final adapter requires its own smoke test: normal queued combat; removal of the outstanding target; removal before the first successful course response; queue clear/rebuild; recall; and session restart. Group-wave behavior needs additional coverage when available.

## Thin Queue Protection

Restores the active guard from `v2.1.0-guffa.10` alongside Faster Queue Recovery.
This is separate from the retired Kir'shara combat-completion repair.

```toml
[control]
kirshara_fixes = true
```

Enabled by default on Windows x64 as part of `control.kirshara_fixes`. Also respects
`control.queue_enabled`. The old separate feature settings, including
`advanced.queue.thin_queue_protection`, are retired; use `kirshara_fixes = false`
to disable the combined fixes. Default-enabled `[patches].thinqueueprotectionhooks`
controls installation independently. Installed callbacks check the current feature and queue
preferences. Changing the installation switch requires restart; TOML changes require
reload/restart.

The native planner (`DoPlanPathAndEngageTarget`) and watchdog (`HandleStall`) run
first. If they removed an exact prefix of targets, left a nonempty unchanged
suffix, and left the same fleet idle without an active engagement, the guard
rechecks that state and asks the native `TryPlanPathAndEngageTarget` to resume.
Pending/last-target latches must be absent or refer only to removed targets.
Reordering, replacement, truncation, or a latch naming a surviving target rejects
recovery. An unchanged queue is never enough evidence to retry.

On disposal, a fleet must be explicitly destroyed with removal reason `Destroyed`
(1). If native handling leaves that target at an inactive queue head, the guard
rechecks it and calls native `ProcessQueue(target, false)`. It does not force
immediate selection, remove arbitrary targets, or edit queue/engagement state.

Faster Queue Recovery still owns the failed-course-response path. The restored
guard uses three different detours and calls the existing native engage entry,
so Faster Queue Recovery can observe those requests normally. The nested planner
and watchdog paths cannot replay a successfully engaged target because the outer
guard sees the resulting active engagement. Queue identity is checked again after
native calls. All three guard hooks must install before any guard action is enabled.

Methods, field offsets/types and enum return representations are checked before
installation; incompatible layouts keep native behavior. This port installs on
Windows x64 and, since #339, on macOS; see the macOS notes at the end.

Successful actions log `[ThinQueueProtection] resume` or
`[ThinQueueProtection] process-destroyed-head`; startup logs `ready=true` or an
unavailable warning. There is no per-frame scan or background polling.

Run `zig build test-mod`. The restored
release tests cover destroyed-head filtering, prefix removal, multiple removed
targets, latch safety, queue replacement/reordering and postcondition changes.
They do not establish game ABI or live wave behavior. Runtime verification still
requires a new build to be deployed and a wave test after restart.

## Kir'Shara queue address guard

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

Run `zig build test-mod`. Gameplay validation
must use the clean build: queue A's same-system hostiles, move/recall B in another
system, and allow A to engage its next target. Then recall A and check its queue
clears normally. The prior instrumented build passed both controls, and reproduced
the raw address fault even with fleet observation polling disabled.

## macOS (#339)

Faster Queue Recovery and Thin Queue Protection use their existing implementation
on Apple Silicon and Intel. Queue Address Guard already supported macOS. The
shared `[control] kirshara_fixes` option controls the fixes on every platform;
#339 added no settings and changed no queue policy or defaults.

Historical Mac client 199 (`1.000.52486`) inspection found the same 24-byte course
response layout: fleet at 0, success at 8, recall at 9, target at 16. Its aggregate
argument is indirect on Apple Silicon and stack-passed on Intel, matching the host
C++ ABI. The seven detour targets exceeded the architecture-specific SPUD overwrite
windows in those binaries. That historical evidence does not establish native
hook fit for a newer client. Runtime metadata checks reject incompatible managed
layouts; they do not validate native method extent.

Current Mac build, native-fit and exact-artifact runtime results must be recorded
separately for each tested architecture and game version.

Focused smoke test:

1. Set `[control] queue_enabled = true` and `kirshara_fixes = true`. Leave
   `[patches] actionqueuerecoveryhooks`, `thinqueueprotectionhooks` and
   `queueaddressguardhooks` enabled; restart after changing hook switches.
2. Confirm the startup log reports `+ Patching` for `ActionQueueRecovery`,
   `ThinQueueProtection` and `QueueAddressGuard`. Preserve any `! Not installed`
   line and the binding errors logged before it.
3. Queue same-system hostiles on ship A. Move or recall ship B from another
   system; A's queue should survive and continue normally.
4. Recall A itself, then separately clear its queue manually. Both should clear.
5. Let another player remove A's queued head before engagement. Check progression
   to the next eligible target, then repeat with respawns and two ships. Watch for
   duplicate engagements, stalled courses, unexpected clearing or crashes.
6. Compare `kirshara_fixes` off/on by editing the TOML key; the settings file is
   reloaded when it changes. Record commit, artifact hash, architecture, game version,
   exact steps and logs. Disable an affected hook switch and restart if needed.

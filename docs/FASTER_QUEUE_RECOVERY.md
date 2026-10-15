# Faster Queue Recovery

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

## Behavior

The adapter records the latest engagement attempt for up to eight fleets, using weak queue identities and full 64-bit target IDs. A failed, non-recall course response may request native planning only when:

- The response belongs to the same queue and latest target/attempt, observed within 30 seconds.
- The queue was engaging on entry to the response and native processing cleared that flag.
- A different front target remains, and the failed target is absent from every inspected queue.
- The native retry decision was false. Ordinary retries are unchanged.

The request record is consumed once. The native planner selects and validates the next target. The mod does not force ship state, clear engagement flags, change retry counts, or remove targets. If another fleet still queues the target, the native cross-fleet removal path remains responsible.

Queue storage is bounded and validated. Unknown layouts/content are ineligible. Expired/replaced requests release their weak handles; native session cleanup clears all records. Engage, Retry and Course discard pending recovery records when they observe disabled behavior. Both Toggle Queue transitions also clear them, so an off/on toggle cannot reuse authorization from before the toggle. A native attempt returning skip/stop cancels only its own record, preserving newer reentrant attempts.

There is no watchdog hook, frame scan, timer, background worker, or per-engagement logging. Queue inspection occurs in existing callbacks; all-queue inspection runs only for a potentially eligible failed response. Response matching is not a server-issued request ID, so delayed same-target responses remain an interoperability limitation.

## Native integration

One detour owns each method. Methods are resolved through their complete managed signatures. Installation validates the event and queue field layouts and the engagement result's integer representation. SPUD builds the trampolines using its existing instruction decoder. Addresses and relocation-dependent instruction bytes are not pinned to a client release.

The hooked methods are `TryPlanPathAndEngageTarget`, `ShouldRetryFailedSetCourse`, `OnSetCourseResponseEventHandler` and `StopWatchdogAndClearAllQueues`. These checks establish binding compatibility; changes to the native retry/planner behavior still require update review and runtime testing.

The course event is a 24-byte value type, with fleet ID at 0, success/recall at 8/9 and boxed target at 16. Metadata field offsets include the boxed object header. The native retry handler is called synchronously inside the course handler; returning true selects its existing planner branch.

The older off-screen Kir'Shara combat-completion repair was fixed by Scopely. That workaround and its `kirshara_queue_repair` setting have already been removed; neither is a prerequisite for Faster Queue Recovery. This feature addresses only the separate unavailable-target/course-response race described above.

## Validation

Run `tests/run-action-queue.ps1` on Windows, or compile `tests/action_queue.cc` with a C++23 compiler and `-Isrc/mod`. Tests use the production policy and request store with fake weak handles. They cover one-shot recovery, reordered/still-present targets, cross-fleet rejection, stale attempts, replaced/collected queues, expiry, reentrant cancellation, bounded capacity and cleanup. They do not model game ABI or network scheduling.

The prototype produced two observed handoffs with the next target attempted 1–2 ms later and successful responses within 388–532 ms. Those timings are observations, not a latency guarantee. The final adapter requires its own smoke test: normal queued combat; removal of the outstanding target; removal before the first successful course response; queue clear/rebuild; recall; and session restart. Group-wave behavior needs additional coverage when available.

# Kirshara protections on macOS

Faster Queue Recovery and Thin Queue Protection use their existing implementation
on Apple Silicon and Intel. Queue Address Guard already supports macOS. The
shared `[control] kirshara_fixes` option controls the protections on every platform;
this change adds no settings and changes no queue policy or defaults.

## Qualification

Historical Mac client 199 (`1.000.52486`) inspection found the same 24-byte course
response layout: fleet at 0, success at 8, recall at 9, target at 16. Its aggregate
argument is indirect on Apple Silicon and stack-passed on Intel, matching the host
C++ ABI. The seven detour targets exceeded the architecture-specific SPUD overwrite
windows in those binaries. That historical evidence does not establish native
hook fit for a newer client. Runtime metadata checks reject incompatible managed
layouts; they do not validate native method extent.

Current Mac build, native-fit and exact-artifact runtime results must be recorded
separately for each tested architecture and game version.

## Focused smoke test

1. Set `[control] queue_enabled = true` and `kirshara_fixes = true`. Leave
   `[patches] actionqueuerecoveryhooks`, `thinqueueprotectionhooks` and
   `queueaddressguardhooks` enabled; restart after changing hook switches.
2. Confirm startup logs report `[FasterQueueRecovery] ready=true`,
   `[ThinQueueProtection] ready=true` and `[QueueAddressGuard] ready=true`.
   Preserve any unavailable or hook-install failure messages.
3. Queue same-system hostiles on ship A. Move or recall ship B from another
   system; A's queue should survive and continue normally.
4. Recall A itself, then separately clear its queue manually. Both should clear.
5. Let another player remove A's queued head before engagement. Check progression
   to the next eligible target, then repeat with respawns and two ships. Watch for
   duplicate engagements, stalled courses, unexpected clearing or crashes.
6. Compare `kirshara_fixes` off/on. Without the settings UI, edit the existing TOML
   key and restart. Record commit, artifact hash, architecture, game version,
   exact steps and logs. Disable an affected hook switch and restart if needed.

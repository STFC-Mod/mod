#pragma once

// The mod's entry point for the platform loaders.

// Loads GameAssembly and hooks il2cpp_init, which installs everything else once the runtime is up.
void StartMod();

// Game thread only. UI automation must leave the game's own path alone while an in-process reload settles.
bool ModReloading();

// Reports a failure from before the mod's log exists. Each platform entry point defines it where a user can find
// the message: debugger output on Windows, the system log on macOS.
void ReportStartupFailure(const char* message);

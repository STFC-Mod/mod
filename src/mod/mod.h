#pragma once

// The mod's entry point for the platform loaders.

// Loads GameAssembly and hooks il2cpp_init, which installs everything else once the runtime is up.
void StartMod();

// Reports a failure from before the mod's log exists. Each platform entry point defines it where a user can find
// the message: debugger output on Windows, the system log on macOS.
void ReportStartupFailure(const char* message);

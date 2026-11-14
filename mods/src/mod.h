#pragma once

// The mod's entry point for the platform loaders.

// Loads GameAssembly and hooks il2cpp_init, which installs everything else once the runtime is up.
void StartMod();

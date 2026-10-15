#pragma once

// Where the mod's settings, runtime vars, log, and battle-log cache live, and the command-line flags that change
// them. `-ccm <path>` selects a profile: every file is named after it instead of the defaults.

#include <filesystem>
#include <string>
#include <string_view>

namespace File
{
// Parses the command line. Call once, before anything else here.
void Init();

// File names as configured, relative to Path().
const std::string& Config();
const std::string& Vars();
const std::string& Log();
// The settings file name used without -ccm, for messages that point users at it.
const std::string& DefaultConfig();

// The -ccm profile name ("alt" for -ccm alt.toml), or empty without -ccm.
const std::string& Profile();
bool               Debug();
bool               Trace();

// Full path of a mod file. On macOS this is ~/Library/Preferences/com.stfcmod.startrekpatch unless -ccm points
// elsewhere; on Windows it is the game's working directory.
std::filesystem::path Path(std::string_view name, bool create_dir = false);

// Moves settings left behind by older releases. Logs, so call after the logger is up.
void MigrateLegacyFiles();
} // namespace File

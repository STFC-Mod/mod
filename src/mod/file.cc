#include "file.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <vector>

#if _WIN32
#include <Windows.h>
#include <shellapi.h>
#elif __APPLE__
#include <crt_externs.h>
#endif

#if __APPLE__
std::filesystem::path MacLibraryDirectory(); // file_macos.mm
#endif

namespace
{
constexpr std::string_view kDefaultConfig = "community_patch_settings.toml";
constexpr std::string_view kDefaultVars   = "community_patch_runtime.vars";
constexpr std::string_view kDefaultLog    = "community_patch.log";

// Names used by older releases.
constexpr std::string_view kLegacyVars   = "community_path_runtime.vars";
constexpr std::string_view kLegacyParsed = "community_patch_settings_parsed.toml";

std::string           config_name, vars_name, log_name, profile;
const std::string     default_config{kDefaultConfig};
std::filesystem::path profile_path;
#ifdef _MODDBG
bool debug = true;
#else
bool debug = false;
#endif
bool trace = false;

// std::filesystem::path(std::string) uses the ANSI code page on Windows; the mod's strings are UTF-8.
std::filesystem::path FromUtf8(std::string_view text)
{ return std::u8string(text.begin(), text.end()); }

std::string ToUtf8(const std::filesystem::path& path)
{
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}

std::vector<std::string> Arguments()
{
  std::vector<std::string> args;
#if _WIN32
  int  argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv)
    return args;
  for (int i = 0; i < argc; ++i) {
    const auto  size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
    std::string arg(size > 0 ? size - 1 : 0, '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), size, nullptr, nullptr);
    args.push_back(std::move(arg));
  }
  LocalFree(argv);
#elif __APPLE__
  const int argc = *_NSGetArgc();
  char**    argv = *_NSGetArgv();
  args.assign(argv, argv + argc);
#endif
  return args;
}

std::filesystem::path Directory(bool legacy = false)
{
  if (!profile_path.empty())
    return profile_path.parent_path();
#if __APPLE__
  return MacLibraryDirectory() / "Preferences" / (legacy ? "com.tashcan.startrekpatch" : "com.stfcmod.startrekpatch");
#else
  (void)legacy;
  return {};
#endif
}

// Settings used to live in com.tashcan.startrekpatch. Move that folder over once and leave a symlink and a note
// behind for anyone who still looks there.
void MigrateMacPreferences()
{
#if __APPLE__
  namespace fs         = std::filesystem;
  const auto file_path = File::Path(config_name);
  const auto new_dir   = file_path.parent_path();
  if (fs::exists(file_path) || fs::exists(new_dir))
    return;

  const auto old_dir  = Directory(true);
  const auto old_path = old_dir / config_name;
  if (!fs::is_regular_file(old_path))
    return;

  spdlog::info("mac config migration: moving {} to {}", ToUtf8(old_dir), ToUtf8(new_dir));
  try {
    fs::rename(old_dir, new_dir);
    fs::create_directories(old_dir);
    fs::create_symlink(file_path, old_path);

    std::ofstream info(old_dir / "update-info.txt");
    info << "Your config has been moved!\n\n";
    info << "You can now find your config at " << file_path << "\n\n";
    info << "A symlink has been placed for your convenience, but it is generally recommended, that you use the new "
            "path and delete this directory going forward.";
  } catch (const std::exception& error) {
    spdlog::warn("mac config migration failed: {}", error.what());
  }
#endif
}
} // namespace

namespace File
{
void Init()
{
  const auto args = Arguments();
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "-debug")
      debug = true;
    else if (args[i] == "-trace")
      trace = true;
    else if (args[i] == "-ccm" && i + 1 < args.size())
      profile_path = FromUtf8(args[++i]);
  }

  if (profile_path.empty()) {
    config_name = kDefaultConfig;
    vars_name   = kDefaultVars;
    log_name    = kDefaultLog;
    return;
  }

  profile             = ToUtf8(profile_path.stem());
  auto with_extension = [](const char* extension) {
    return ToUtf8(std::filesystem::path(profile_path).replace_extension(extension));
  };
  config_name = with_extension(".toml");
  vars_name   = with_extension(".vars");
  log_name    = with_extension(".log");
}

const std::string& Config()
{ return config_name; }

const std::string& Vars()
{ return vars_name; }

const std::string& Log()
{ return log_name; }

const std::string& DefaultConfig()
{ return default_config; }

const std::string& Profile()
{ return profile; }

bool Debug()
{ return debug; }

bool Trace()
{ return trace; }

std::filesystem::path Path(std::string_view name, bool create_dir)
{
  const auto dir = Directory();
  if (create_dir && !dir.empty()) {
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
  }
  // A -ccm profile path is already complete; joining an absolute path replaces the directory.
  return dir / FromUtf8(name).filename();
}

void MigrateLegacyFiles()
{
  MigrateMacPreferences();

  std::error_code ignored;
  std::filesystem::remove(Path(kLegacyVars), ignored);
  if (std::filesystem::remove(Path(kLegacyParsed), ignored))
    spdlog::info("Removed {} (old parsed file)", kLegacyParsed);
}
} // namespace File

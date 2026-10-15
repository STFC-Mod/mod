// Launches the game with the community mod dylib injected. The game's install path comes from the Xsolla
// launcher's settings; the dylib sits next to this executable.

#include <inicpp.h>

#include <mach-o/dyld.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

std::filesystem::path LibraryDirectory(); // library_dir.mm

namespace
{
constexpr auto kAppPath = "Star Trek Fleet Command.app/Contents/MacOS/Star Trek Fleet Command";

int Fail(const std::string& message)
{
  fprintf(stderr, "stfc-community-mod-loader: %s\n", message.c_str());
  return 1;
}

// `value` prefixed to the variable's current value, colon-separated.
void Prepend(const char* variable, const std::string& value)
{
  std::string combined = value;
  if (const char* old = getenv(variable)) {
    combined += ":";
    combined += old;
  }
  setenv(variable, combined.c_str(), 1);
}
} // namespace

int main()
{
  const auto settings_path = LibraryDirectory() / "Preferences" / "Star Trek Fleet Command" / "launcher_settings.ini";
  std::ifstream settings_stream(settings_path);
  if (!settings_stream)
    return Fail("cannot read " + settings_path.string() + "; install the game with the Xsolla launcher first");
  ini::IniFile settings(settings_stream);

  const auto install_dir = settings["General"]["152033..GAME_PATH"].as<std::string>();
  if (install_dir.empty())
    return Fail("no game path (152033..GAME_PATH) in " + settings_path.string());
  const auto game = (std::filesystem::path(install_dir) / kAppPath).string();

  // Resolve symlinks so the dylib path is the real one next to this binary.
  char     executable[PATH_MAX];
  uint32_t size = sizeof(executable);
  if (_NSGetExecutablePath(executable, &size) != 0)
    return Fail("executable path does not fit");
  char resolved[PATH_MAX];
  if (!realpath(executable, resolved))
    return Fail(std::string("cannot resolve executable path: ") + strerror(errno));
  const auto loader_dir = std::filesystem::path(resolved).parent_path().string();

  Prepend("DYLD_INSERT_LIBRARIES", loader_dir + "/libstfc-community-mod.dylib");
  // TODO: likely unnecessary since the inserted path is absolute, but it leaks into every lookup the game makes.
  // Remove once someone confirms the game still starts without it.
  Prepend("DYLD_LIBRARY_PATH", loader_dir);

  char* const arguments[] = {const_cast<char*>(game.c_str()), nullptr};
  execv(arguments[0], arguments);
  return Fail("failed to launch " + game + ": " + strerror(errno));
}

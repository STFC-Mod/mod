#include "config_save.h"

#include <cassert>
#include <fstream>
#include <iostream>

#if _WIN32
#include <Windows.h>
#endif

int main(int argc, char** argv)
{
  assert(argc == 2);
  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root);
  const auto        path  = root / "settings.toml";
  const std::string value = "quotes: \"'\\\n[unexpected]\nenabled = true\nUnicode: \xc3\xa9";
  toml::table       config{{"value", value}, {"enabled", false}};
  SaveConfigDocument(config, path, "# generated\n");
  auto parsed = toml::parse_file(path.string());
  assert(parsed["value"].value<std::string>() == value);
  assert(parsed.size() == 2);
  config.insert_or_assign("enabled", true);
  SaveConfigDocument(config, path);
  assert(toml::parse_file(path.string())["enabled"].value<bool>() == true);

  bool failed = false;
  try {
    SaveConfigDocument(config, path, "invalid = [\n");
  } catch (const std::exception&) {
    failed = true;
  }
  assert(failed);
  assert(toml::parse_file(path.string())["value"].value<std::string>() == value);

  const auto directory = root / "occupied";
  std::filesystem::create_directory(directory);
  failed = false;
  try {
    SaveConfigDocument(config, directory);
  } catch (const std::exception&) {
    failed = true;
  }
  assert(failed && std::filesystem::is_directory(directory));
#if _WIN32
  // A real sharing violation must leave the previous readable document intact.
  auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  assert(handle != INVALID_HANDLE_VALUE);
  failed = false;
  config.insert_or_assign("enabled", false);
  try {
    SaveConfigDocument(config, path);
  } catch (const std::exception&) {
    failed = true;
  }
  std::string startupError;
  assert(!SaveStartupConfigDocument(config, path, {}, startupError));
  assert(!startupError.empty());
  CloseHandle(handle);
  assert(failed);
  assert(toml::parse_file(path.string())["enabled"].value<bool>() == true);
  assert(SaveStartupConfigDocument(config, path, {}, startupError) && startupError.empty());
  assert(toml::parse_file(path.string())["enabled"].value<bool>() == false);
#else
  const auto mode = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
  std::filesystem::permissions(path, mode);
  const auto link = root / "linked.toml";
  std::filesystem::create_symlink(path, link);
  config.insert_or_assign("enabled", false);
  SaveConfigDocument(config, link);
  assert(std::filesystem::is_symlink(link));
  assert(toml::parse_file(path.string())["enabled"].value<bool>() == false);
  assert(std::filesystem::status(path).permissions() == mode);
#endif
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    assert(entry.path().filename().string().find(".tmp-") == std::string::npos);
  }
  std::cout << "Config save fixtures passed\n";
}

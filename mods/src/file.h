#include "config.h"
#include "str_utils.h"
#include "version.h"
#include <prime/ui.h>

#include <EASTL/tuple.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#if !_WIN32
#include "folder_manager.h"
#else
#include <shellapi.h>
#include <windows.h>
#endif

// Original output file names
#define FILE_DEF_CONFIG "community_patch_settings.toml"
#define FILE_DEF_LOG "community_patch.log"
#define FILE_DEF_VARS "community_patch_runtime.vars"
#define FILE_DEF_VARS_OLD "community_path_runtime.vars"
#define FILE_DEF_BL "patch_battlelogs_sent.json"
#define FILE_DEF_PARSED "community_patch_settings_parsed.toml"
#define FILE_DEF_TITLE L"Star Trek Fleet Command"

#define FILE_EXT_TOML ".toml"
#define FILE_EXT_VARS ".vars"
#define FILE_EXT_LOG ".log"
#define FILE_EXT_JSON ".json"

class File
{
public:
  static void         Init();
  static std::wstring Title();
  static const char*  Default();
  static const char*  Config();
  static const char*  Vars();
  static const char*  Log();
  static const char*  Battles();
  static bool         hasCustomNames();
  static bool         hasDebug();
  static bool         hasTrace();

#if _WIN32
  static std::string_view MakePath(std::string_view filename, bool create_dir = false, bool old_path = false);
#else
  static std::u8string MakePath(std::string_view filename, bool create_dir = false, bool old_path = false);
#endif

private:
  static std::filesystem::path Path();

  static bool debug;
  static bool trace;
  static bool override;
  static bool initialized;

  static std::wstring cacheNameTitle;
  static std::string  cacheNameBattles;
  static std::string  cacheNameLog;
  static std::string  cacheNameVar;
  static std::string  cacheNameConfig;
  static std::string  cacheNameDefault;

  static std::filesystem::path configPath;
};

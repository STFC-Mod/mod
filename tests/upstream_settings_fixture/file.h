#pragma once
#include <string>
#include <string_view>

// Test-only path boundary. The production catalog still compiles unchanged;
// this fixture never resolves a game installation or an installed config.
class File
{
public:
  static const char* Config();
#if _WIN32
  static std::string_view MakePath(std::string_view, bool = false, bool = false);
#else
  static std::u8string MakePath(std::string_view, bool = false, bool = false);
#endif
};

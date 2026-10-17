#include <cerrno>
#include <cstdio>

static bool failClose = false;

static std::size_t ShortWrite(const void* data, std::size_t size, std::size_t count, std::FILE* file)
{
  if (failClose) {
    return std::fwrite(data, size, count, file);
  }
  const auto written = std::fwrite(data, size, count / 2, file);
  errno              = ENOSPC;
  return written;
}

static int FailedClose(std::FILE* file)
{
  std::fclose(file);
  errno = ENOSPC;
  return EOF;
}

// Exercise the production cleanup path without adding runtime injection controls.
#define CONFIG_SAVE_WRITE ShortWrite
#define CONFIG_SAVE_CLOSE FailedClose
#include "../mods/src/config_save.cc"

#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
  assert(argc == 2);
  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root);
  const auto        path     = root / "settings.toml";
  const std::string original = "# keep this exactly\nenabled = false\n";
  {
    std::ofstream out(path, std::ios::binary);
    out << original;
  }
  for (bool closeFailure : {false, true}) {
    failClose   = closeFailure;
    bool failed = false;
    try {
      SaveConfigDocument(toml::table{{"enabled", true}}, path);
    } catch (const std::system_error&) {
      failed = true;
    }
    assert(failed);
    std::string startupError;
    assert(!SaveStartupConfigDocument(toml::table{{"enabled", true}}, path, {}, startupError));
    assert(!startupError.empty()); // Startup continues with the loaded live value.
    std::ifstream     input(path, std::ios::binary);
    const std::string actual(std::istreambuf_iterator<char>{input}, {});
    assert(actual == original);
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
      assert(entry.path() == path);
    }
  }
  std::cout << "Short-write and failed-close fixtures passed\n";
}

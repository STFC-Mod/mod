#include "config_save.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

#if _WIN32
#include <Windows.h>
#endif

// Compile-time substitutions are used only by the isolated failure fixture.
#ifndef CONFIG_SAVE_WRITE
#define CONFIG_SAVE_WRITE std::fwrite
#endif
#ifndef CONFIG_SAVE_CLOSE
#define CONFIG_SAVE_CLOSE std::fclose
#endif

void SaveConfigDocument(const toml::table& config, const std::filesystem::path& path, std::string_view header)
{
  // Serialize and validate before opening any file. Values are encoded by toml++,
  // never interpolated into TOML source. Validate the header too.
  std::ostringstream output;
  output.exceptions(std::ios::badbit | std::ios::failbit);
  output << header << config;
  const auto bytes = output.str();
  ReplaceConfigText(path, bytes);
}

bool SaveStartupConfigDocument(const toml::table& config, const std::filesystem::path& path, std::string_view header,
                               std::string& error)
{
  try {
    SaveConfigDocument(config, path, header);
    error.clear();
    return true;
  } catch (const std::exception& failure) {
    error = failure.what();
    return false;
  }
}

std::string ReadConfigText(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("could not open config file");
  }
  std::string text;
  char        buffer[8192];
  while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
    text.append(buffer, static_cast<std::size_t>(input.gcount()));
  }
  if (!input.eof() || input.bad()) {
    throw std::runtime_error("could not read config file");
  }
  return text;
}

bool ReplaceConfigText(const std::filesystem::path& path, std::string_view bytes,
                       std::optional<std::string_view> expected)
{
  (void)toml::parse(bytes);

  // Follow existing symlinks as the former ofstream save did. A sibling stays on
  // the same filesystem. Exclusive creation avoids truncating another save's file.
  const auto                             destination = std::filesystem::weakly_canonical(path);
  static std::atomic<unsigned long long> sequence{0};
  auto                                   temporary = destination;
  temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-"
               + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));

  // C11 exclusive creation avoids depending on newer libc++ fstream runtime
  // support on our minimum supported macOS version.
#if _WIN32
  std::FILE* file = nullptr;
  _wfopen_s(&file, temporary.c_str(), L"wbx");
#else
  auto* file = std::fopen(temporary.c_str(), "wbx");
#endif
  if (!file) {
    throw std::system_error(errno, std::generic_category(), "could not create temporary config file");
  }

  bool replacing = false;
  try {
    if (CONFIG_SAVE_WRITE(bytes.data(), 1, bytes.size(), file) != bytes.size()) {
      throw std::system_error(errno, std::generic_category(), "could not write temporary config file");
    }
    const auto closed = CONFIG_SAVE_CLOSE(file); // Includes flushing; failure prevents replacement.
    file              = nullptr;
    if (closed != 0) {
      throw std::system_error(errno, std::generic_category(), "could not close temporary config file");
    }
    // Recheck after staging, immediately before commit. Another editor can still
    // race the native replacement; arbitrary external editors do not share our lock.
    if (expected && ReadConfigText(path) != *expected) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      return false;
    }
#if _WIN32
    // Let Windows retain the existing file's permissions and streams. A backup
    // protects the old contents in ReplaceFile's documented partial-failure cases.
    auto backup = temporary;
    backup += ".bak";
    if (!ReplaceFileW(destination.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr)) {
      auto error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND) {
        // Missing-destination fallback: do not overwrite a file appearing before
        // this move. The caller's earlier existence check is not a create-only transaction.
        error = MoveFileExW(temporary.c_str(), destination.c_str(), 0) ? ERROR_SUCCESS : GetLastError();
      }
      if (error != ERROR_SUCCESS) {
        replacing = error == ERROR_UNABLE_TO_MOVE_REPLACEMENT || error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2;
        throw std::filesystem::filesystem_error(
            replacing ? "config replacement failed; retain temporary/backup for recovery" : "config replacement failed",
            temporary, destination, std::error_code(error, std::system_category()));
      }
    }
    std::error_code ignored;
    std::filesystem::remove(backup, ignored);
#else
    // Preserve ordinary permission bits when replacing an existing config.
    if (std::filesystem::exists(destination)) {
      std::filesystem::permissions(temporary, std::filesystem::status(destination).permissions());
    }
    std::filesystem::rename(temporary, destination);
#endif
  } catch (...) {
    if (file) {
      std::fclose(file);
    }
    std::error_code ignored;
    if (!replacing) {
      std::filesystem::remove(temporary, ignored);
    }
    throw;
  }
  return true;
}

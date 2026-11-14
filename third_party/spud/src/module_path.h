#pragma once

// Module names and, on Windows, the loaded-module list and image headers
// that the import walk and the signature scanner both read.

#include <spud/arch.h>

#include <cstdint>
#include <string>
#include <string_view>

#ifdef SPUD_OS_WIN
#include <windows.h>

#include <psapi.h>

#include <vector>
#endif

namespace spud::detail {

// Windows file names are case-insensitive; everywhere else they are not.
inline bool same_path_text(std::string_view a, std::string_view b) {
#ifdef SPUD_OS_WIN
  if (a.size() != b.size()) {
    return false;
  }
  auto lower = [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  };
  for (size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) {
      return false;
    }
  }
  return true;
#else
  return a == b;
#endif
}

inline std::string_view path_filename(std::string_view path) {
  const size_t slash = path.find_last_of("/\\");
  if (slash == std::string_view::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

// `module` names `path` when it is the same path or file name, the file name
// without its extensions ("libc" matches libc.so.6, "crypt" does not match
// libcrypto.so), or the trailing components of the path. An empty module
// matches everything.
inline bool path_matches_module(std::string_view path,
                                std::string_view module) {
  if (module.empty()) {
    return true;
  }
  if (path.empty()) {
    return false;
  }
  const std::string_view file = path_filename(path);
  const std::string_view needle = path_filename(module);
  if (same_path_text(file, needle) || same_path_text(path, module)) {
    return true;
  }
  if (file.size() > needle.size() && file[needle.size()] == '.' &&
      same_path_text(file.substr(0, needle.size()), needle)) {
    return true;
  }
  if (path.size() > module.size()) {
    const size_t cut = path.size() - module.size();
    const char sep = path[cut - 1];
    return (sep == '/' || sep == '\\') &&
           same_path_text(path.substr(cut), module);
  }
  return false;
}

#ifdef SPUD_OS_WIN
// EnumProcessModules reports the size it needed; a module can load between
// the two calls, so ask again until the list fits.
inline std::vector<HMODULE> process_modules() {
  std::vector<HMODULE> modules(256);
  for (;;) {
    const auto bytes = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules.data(), bytes,
                           &needed) == 0) {
      return {};
    }
    const bool fits = needed <= bytes;
    modules.resize(needed / sizeof(HMODULE));
    if (fits) {
      return modules;
    }
  }
}

// The module's full path, or empty when the call fails. GetModuleFileNameA
// truncates silently and returns the buffer size, so grow up to the 32K
// long-path limit until the name fits.
inline std::string module_file_name(HMODULE module) {
  std::string path(MAX_PATH, '\0');
  for (;;) {
    const DWORD n = GetModuleFileNameA(module, path.data(),
                                       static_cast<DWORD>(path.size()));
    if (n == 0) {
      return {};
    }
    if (n < path.size()) {
      path.resize(n);
      return path;
    }
    if (path.size() >= 32768) {
      return {};
    }
    path.resize(path.size() * 2);
  }
}

// The NT headers of a loaded image, or null when it does not look like one.
inline const IMAGE_NT_HEADERS *image_nt_headers(HMODULE module) {
  const auto *base = reinterpret_cast<const uint8_t *>(module);
  if (base == nullptr) {
    return nullptr;
  }
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    return nullptr;
  }
  const auto *nt =
      reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}
#endif

} // namespace spud::detail

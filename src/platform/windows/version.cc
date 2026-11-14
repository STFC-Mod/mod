// The version.dll exports the game imports, forwarded to the system copy. Loading the mod as version.dll from the
// game folder is what injects it; every other process that picks this file up must still get working calls.

#include <Windows.h>

#include <string>

namespace
{
// Loaded on the first forwarded call rather than in DllMain, which runs under the loader lock.
HMODULE SystemVersionDll()
{
  static const HMODULE module = [] {
    wchar_t    dir[MAX_PATH];
    const auto length = GetSystemDirectoryW(dir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
      return HMODULE{};
    return LoadLibraryW((std::wstring(dir, length) + L"\\version.dll").c_str());
  }();
  return module;
}

FARPROC Resolve(const char* name)
{
  auto* module = SystemVersionDll();
  return module ? GetProcAddress(module, name) : nullptr;
}
} // namespace

// Every export returns DWORD or BOOL, where 0 is failure.
#define FORWARD(name, ...)                                                                                             \
  static const auto original = reinterpret_cast<decltype(&::name)>(Resolve(#name));                                    \
  if (!original) {                                                                                                     \
    SetLastError(ERROR_PROC_NOT_FOUND);                                                                                \
    return 0;                                                                                                          \
  }                                                                                                                    \
  return original(__VA_ARGS__)

DWORD APIENTRY VerFindFileA(DWORD flags, LPCSTR file, LPCSTR win_dir, LPCSTR app_dir, LPSTR cur_dir, PUINT cur_dir_len,
                            LPSTR dest_dir, PUINT dest_dir_len)
{ FORWARD(VerFindFileA, flags, file, win_dir, app_dir, cur_dir, cur_dir_len, dest_dir, dest_dir_len); }

DWORD APIENTRY VerFindFileW(DWORD flags, LPCWSTR file, LPCWSTR win_dir, LPCWSTR app_dir, LPWSTR cur_dir,
                            PUINT cur_dir_len, LPWSTR dest_dir, PUINT dest_dir_len)
{ FORWARD(VerFindFileW, flags, file, win_dir, app_dir, cur_dir, cur_dir_len, dest_dir, dest_dir_len); }

DWORD APIENTRY VerInstallFileA(DWORD flags, LPCSTR src_file, LPCSTR dest_file, LPCSTR src_dir, LPCSTR dest_dir,
                               LPCSTR cur_dir, LPSTR tmp_file, PUINT tmp_file_len)
{ FORWARD(VerInstallFileA, flags, src_file, dest_file, src_dir, dest_dir, cur_dir, tmp_file, tmp_file_len); }

DWORD APIENTRY VerInstallFileW(DWORD flags, LPCWSTR src_file, LPCWSTR dest_file, LPCWSTR src_dir, LPCWSTR dest_dir,
                               LPCWSTR cur_dir, LPWSTR tmp_file, PUINT tmp_file_len)
{ FORWARD(VerInstallFileW, flags, src_file, dest_file, src_dir, dest_dir, cur_dir, tmp_file, tmp_file_len); }

DWORD APIENTRY GetFileVersionInfoSizeA(LPCSTR file, LPDWORD handle)
{ FORWARD(GetFileVersionInfoSizeA, file, handle); }

DWORD APIENTRY GetFileVersionInfoSizeW(LPCWSTR file, LPDWORD handle)
{ FORWARD(GetFileVersionInfoSizeW, file, handle); }

BOOL APIENTRY GetFileVersionInfoA(LPCSTR file, DWORD handle, DWORD length, LPVOID data)
{ FORWARD(GetFileVersionInfoA, file, handle, length, data); }

BOOL APIENTRY GetFileVersionInfoW(LPCWSTR file, DWORD handle, DWORD length, LPVOID data)
{ FORWARD(GetFileVersionInfoW, file, handle, length, data); }

DWORD APIENTRY GetFileVersionInfoSizeExA(DWORD flags, LPCSTR file, LPDWORD handle)
{ FORWARD(GetFileVersionInfoSizeExA, flags, file, handle); }

DWORD APIENTRY GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR file, LPDWORD handle)
{ FORWARD(GetFileVersionInfoSizeExW, flags, file, handle); }

BOOL APIENTRY GetFileVersionInfoExA(DWORD flags, LPCSTR file, DWORD handle, DWORD length, LPVOID data)
{ FORWARD(GetFileVersionInfoExA, flags, file, handle, length, data); }

BOOL APIENTRY GetFileVersionInfoExW(DWORD flags, LPCWSTR file, DWORD handle, DWORD length, LPVOID data)
{ FORWARD(GetFileVersionInfoExW, flags, file, handle, length, data); }

DWORD APIENTRY VerLanguageNameA(DWORD language, LPSTR name, DWORD length)
{ FORWARD(VerLanguageNameA, language, name, length); }

DWORD APIENTRY VerLanguageNameW(DWORD language, LPWSTR name, DWORD length)
{ FORWARD(VerLanguageNameW, language, name, length); }

BOOL APIENTRY VerQueryValueA(LPCVOID block, LPCSTR sub_block, LPVOID* buffer, PUINT length)
{ FORWARD(VerQueryValueA, block, sub_block, buffer, length); }

BOOL APIENTRY VerQueryValueW(LPCVOID block, LPCWSTR sub_block, LPVOID* buffer, PUINT length)
{ FORWARD(VerQueryValueW, block, sub_block, buffer, length); }

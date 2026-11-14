// The Windows entry point: the game loads this as version.dll from its folder.

#include <Windows.h>

#include <string>
#include <string_view>

#include "mod.h"

// Before the mod's log exists, failures go to the debugger output (DebugView shows it without a debugger): the
// game has no console, so its stderr goes nowhere.
void ReportStartupFailure(const char* message)
{ OutputDebugStringA((std::string("stfc-community-mod: ") + message + "\n").c_str()); }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;
  DisableThreadLibraryCalls(instance);

  // Other processes that load version.dll from the game folder only get the forwards in version.cc.
  wchar_t    path[MAX_PATH];
  const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  const auto exe    = std::wstring_view(path, length);
  if (exe.substr(exe.find_last_of(L"\\/") + 1).starts_with(L"prime"))
    StartMod();
  return TRUE;
}

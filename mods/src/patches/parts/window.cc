// The game window on Windows: free resizing, the -ccm profile in the title bar, and the monitor's DPI scale.

#include "patches/patches.h"

#if _WIN32
#include "config.h"
#include "file.h"

#include <il2cpp/il2cpp.h>
#include <spud/detour.h>

#include <Windows.h>

#include <string>

namespace
{
struct AspectRatioConstraintHandler {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Utils", "AspectRatioConstraintHandler">;

  static WNDPROC _unityWndProc()
  { return reinterpret_cast<WNDPROC>(Class::Field<"_unityWndProc", intptr_t>()); }
};

// The first visible, unowned top-level window of this process. Found once it exists, then cached.
HWND GameWindow()
{
  static HWND window = nullptr;
  if (window)
    return window;

  const auto process = GetCurrentProcessId();
  for (auto hwnd = GetTopWindow(nullptr); hwnd; hwnd = GetNextWindow(hwnd, GW_HWNDNEXT)) {
    DWORD owner_process = 0;
    GetWindowThreadProcessId(hwnd, &owner_process);
    if (owner_process == process && !GetWindow(hwnd, GW_OWNER) && IsWindowVisible(hwnd)) {
      window = hwnd;
      break;
    }
  }
  return window;
}

// With several clients running, "-ccm alt.toml" shows up as "[alt] Star Trek Fleet Command".
bool ApplyProfileTitle()
{
  auto* window = GameWindow();
  if (!window)
    return false;

  const auto length = GetWindowTextLengthW(window);
  if (length == 0)
    return false;
  std::wstring title(static_cast<size_t>(length) + 1, L'\0');
  title.resize(GetWindowTextW(window, title.data(), length + 1));

  const auto&  profile = File::Profile();
  std::wstring prefix(MultiByteToWideChar(CP_UTF8, 0, profile.data(), static_cast<int>(profile.size()), nullptr, 0),
                      L'\0');
  MultiByteToWideChar(CP_UTF8, 0, profile.data(), static_cast<int>(profile.size()), prefix.data(),
                      static_cast<int>(prefix.size()));
  return SetWindowTextW(window, (L"[" + prefix + L"] " + title).c_str()) != 0;
}

// TODO: the old hook also stretched a 1024x768 fullscreen fallback to the monitor, but only once a window
// handle captured by a SetWindowLongW detour was set, and that detour was never installed. Bring it back
// with a tested trigger if the fallback is still seen.
// With free_resize the original is skipped: the game's per-frame aspect-ratio enforcement is what would snap the
// window back.
void AspectRatioConstraintHandler_Update(auto original, void* _this)
{
  static bool title_pending = !File::Profile().empty();
  if (title_pending && ApplyProfileTitle())
    title_pending = false;
  if (!Config::Get().graphics.free_resize)
    original(_this);
}

intptr_t AspectRatioConstraintHandler_WndProc(auto original, HWND hWnd, uint32_t msg, intptr_t wParam, intptr_t lParam)
{
  // Skip the game's aspect-ratio clamp and hand the message straight to Unity. The message arrives in the form of
  // the window's current procedure (the game's, installed through its own P/Invoke), and the A and W calls each
  // convert from their own form; the wrong one garbles non-ASCII text such as chat input.
  if (Config::Get().graphics.free_resize) {
    const auto forward = IsWindowUnicode(hWnd) ? CallWindowProcW : CallWindowProcA;
    return forward(AspectRatioConstraintHandler::_unityWndProc(), hWnd, msg, wParam, lParam);
  }
  return original(hWnd, msg, wParam, lParam);
}
} // namespace

float WindowDpiScale(bool refresh)
{
  static HMONITOR last_monitor = nullptr;
  static float    scale        = 1.0f;

  // The game's own window, not GetActiveWindow(): that is null while the game is in the background, which would
  // pick the primary monitor.
  const auto monitor = MonitorFromWindow(GameWindow(), MONITOR_DEFAULTTONEAREST);
  if (!refresh && monitor == last_monitor)
    return scale;

  MONITORINFOEX info{};
  info.cbSize = sizeof(info);
  DEVMODE mode{};
  mode.dmSize = sizeof(mode);
  if (GetMonitorInfo(monitor, &info) && EnumDisplaySettings(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
    const auto logical_width = info.rcMonitor.right - info.rcMonitor.left;
    if (logical_width > 0)
      scale = static_cast<float>(mode.dmPelsWidth) / static_cast<float>(logical_width);
  }
  last_monitor = monitor;
  return scale;
}

void InstallFreeResizeHooks()
{
  auto update  = AspectRatioConstraintHandler::Class::Method("Update");
  auto wndproc = AspectRatioConstraintHandler::Class::Method("WndProc");
  if (!update || !wndproc)
    return;

  SPUD_STATIC_DETOUR(update.Pointer(), AspectRatioConstraintHandler_Update);
  SPUD_STATIC_DETOUR(wndproc.Pointer(), AspectRatioConstraintHandler_WndProc);
}
#else
float WindowDpiScale(bool)
{ return 1.0f; }
#endif

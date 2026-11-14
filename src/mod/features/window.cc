// The game window on Windows: [graphics] free_resize, the -ccm profile in the title bar, [graphics] allow_cursor, and
// the monitor's DPI scale.

#include "features/features.h"

#if _WIN32
#include "config.h"
#include "file.h"

#include "game/app.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <unity/core.h>

#include <string>

#include <Windows.h>

namespace
{
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
  const auto   bytes   = static_cast<int>(profile.size());
  std::wstring prefix(MultiByteToWideChar(CP_UTF8, 0, profile.data(), bytes, nullptr, 0), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, profile.data(), bytes, prefix.data(), static_cast<int>(prefix.size()));
  return SetWindowTextW(window, (L"[" + prefix + L"] " + title).c_str()) != 0;
}

// With free_resize the original is skipped: the game's per-frame aspect-ratio enforcement is what would snap the
// window back.
//
// TODO: the old hook also stretched a 1024x768 fullscreen fallback to the monitor, but only once a window handle
// captured by a SetWindowLongW detour was set, and that detour was never installed. Bring it back with a tested
// trigger if the fallback is still seen.
void AspectRatioConstraintHandler_Update_Hook(auto original, il2cpp::Object* _this)
{
  static bool title_pending = !File::Profile().empty();
  if (title_pending && ApplyProfileTitle())
    title_pending = false;
  if (!Config::Get().graphics.free_resize)
    original(_this);
}

// allow_cursor = false: the plain arrow, never clipped to the window. SetCursor_Injected is static; the trailing
// MethodInfo is passed through unchanged. The UI scale hook applies the same cursor when canvases rescale, since the
// game sets its own cursor again then.
void Cursor_SetCursor_Injected_Hook(auto original, intptr_t texture, const unity::Vector2* hotspot, int32_t mode,
                                    const MethodInfo* method)
{
  if (Config::Get().graphics.allow_cursor)
    return original(texture, hotspot, mode, method);
  SetCursor(LoadCursor(nullptr, IDC_ARROW));
  ClipCursor(nullptr);
}

intptr_t AspectRatioConstraintHandler_WndProc_Hook(auto original, HWND window, uint32_t message, intptr_t w_param,
                                                   intptr_t l_param)
{
  // Skip the game's aspect-ratio clamp and hand the message straight to Unity. The message arrives in the form of
  // the window's current procedure (the game's, installed through its own P/Invoke), and the A and W calls each
  // convert from their own form; the wrong one garbles non-ASCII text such as chat input.
  if (Config::Get().graphics.free_resize) {
    const auto forward = IsWindowUnicode(window) ? CallWindowProcW : CallWindowProcA;
    return forward(reinterpret_cast<WNDPROC>(AspectRatioConstraintHandler::unityWndProc()), window, message, w_param,
                   l_param);
  }
  return original(window, message, w_param, l_param);
}

bool InstallFreeResize()
{
  il2cpp::HookSet hooks("FreeResize");
  IL2CPP_STAGE(hooks, AspectRatioConstraintHandler::Class::Method("Update"), AspectRatioConstraintHandler_Update_Hook);
  IL2CPP_STAGE(hooks, AspectRatioConstraintHandler::Class::Method("WndProc"),
               AspectRatioConstraintHandler_WndProc_Hook);
  return hooks.Commit();
}

bool InstallCursor()
{
  il2cpp::HookSet hooks("Cursor");
  IL2CPP_STAGE(hooks, unity::Cursor::Method("SetCursor_Injected"), Cursor_SetCursor_Injected_Hook);
  return hooks.Commit();
}
} // namespace

// The display scale of the monitor holding the game window, cached until `refresh` or a move to another monitor: the
// display mode's physical width over the width Windows reports to this process, 1.5 when it scales the game to 150%.
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
  DEVMODE mode{.dmSize = sizeof(DEVMODE)};
  if (GetMonitorInfo(monitor, &info) && EnumDisplaySettings(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
    const auto logical_width = info.rcMonitor.right - info.rcMonitor.left;
    if (logical_width > 0)
      scale = static_cast<float>(mode.dmPelsWidth) / static_cast<float>(logical_width);
  }
  last_monitor = monitor;
  return scale;
}

const Feature kFreeResize{"FreeResize", &Config::Patches::freeresizehooks, InstallFreeResize};
// TODO: allow_cursor is not a test patch; it sits in testpatches only because it always has.
const Feature kCursor{"Cursor", &Config::Patches::testpatches, InstallCursor};
#else
float WindowDpiScale(bool)
{ return 1.0f; }
#endif

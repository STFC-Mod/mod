#include "settings/native/page_navigation.h"
#include "settings/native/value_widgets.h"
#include <spdlog/spdlog.h>

void InstallNativeSettings()
{
  // Resolve managed signatures and distinct targets before installing native UI hooks.
#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
  using namespace mod_settings::native;
  try {
    if (!InstallCoreValueWidgets())
      return;
    try {
      InstallPages();
    } catch (...) {
      DisablePages();
      Warn("settings navigation installation unavailable; native confirmation control remains available");
    }
    spdlog::info("[ModSettings] Native settings adapter installed");
  } catch (...) {
    Warn("native settings installation unavailable");
  }
#endif
}

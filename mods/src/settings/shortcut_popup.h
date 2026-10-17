#pragma once
#include <functional>
#include <string>

namespace mod_settings
{
// The feature owns the draft; the native surface only renders and sends commands.
struct ShortcutPopupPresentation {
  std::string title, current, proposed, status, confirmLabel;
  bool        canConfirm = false, canRecord = false;
  bool        operator==(const ShortcutPopupPresentation&) const = default;
};
struct ShortcutPopupCommands {
  std::function<ShortcutPopupPresentation()> read;
  std::function<void()>                      confirm, record, cancel;
};
namespace native
{
#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
  void InstallShortcutPopup();
  bool OpenShortcutPopup(ShortcutPopupCommands commands);
  void CloseShortcutPopup();
#else
  inline void InstallShortcutPopup() {}
  inline bool OpenShortcutPopup(ShortcutPopupCommands)
  { return false; }
  inline void CloseShortcutPopup() {}
#endif
} // namespace native
} // namespace mod_settings

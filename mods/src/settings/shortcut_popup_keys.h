#pragma once

namespace mod_settings
{
enum class ShortcutPopupKey { None, Confirm, Cancel };
// Confirm needs a fresh press after the preview is ready. In particular, an
// Enter captured as the binding must be released before it can confirm itself.
class ShortcutPopupKeys
{
public:
  ShortcutPopupKey Tick(bool canConfirm, bool enterHeld, bool enterDown, bool escapeDown)
  {
    if (escapeDown) {
      armed_ = false;
      return ShortcutPopupKey::Cancel;
    }
    if (!canConfirm) {
      armed_ = false;
      return ShortcutPopupKey::None;
    }
    if (enterDown && armed_) {
      armed_ = false;
      return ShortcutPopupKey::Confirm;
    }
    if (!enterHeld && !enterDown)
      armed_ = true;
    return ShortcutPopupKey::None;
  }

private:
  bool armed_ = false;
};
} // namespace mod_settings

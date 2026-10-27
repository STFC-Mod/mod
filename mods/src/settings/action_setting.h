#pragma once
#include "setting_metadata.h"
#include <charconv>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace mod_settings
{
// A command with a current presentation, not a persisted boolean. The native
// adapter owns each bound view; these definitions live with the page catalog.
struct ActionSetting {
  struct Presentation {
    std::string label, button, value;
    bool        enabled = true, visible = true;
    bool        actionable() const
    { return visible && enabled && !button.empty(); }
  };
  std::string                              identity, label;
  std::function<Presentation(std::size_t)> read;
  std::function<void(std::size_t)>         invoke;
  // Optional repeated rows, evaluated only when building/refreshing settings.
  // Definitions stay stable while their list grows or shrinks.
  std::function<std::size_t()> count;
  // Optional model operations for exact-entry rows. These refer to the same
  // definition as sliders/toggles; commands without a setting leave them empty.
  const SettingMetadata*                 metadata = nullptr;
  std::function<void()>                  reset, retry;
  std::function<std::string()>           status, default_label;
  std::function<SaveStatus()>            save_status;
  std::function<PreparedSettingAction()> prepare_reset;
  std::size_t                            Count() const
  { return count ? count() : 1; }
  Presentation Read(std::size_t index) const
  { return index < Count() ? read(index) : Presentation{"", "", "", false, false}; }
  std::string item_id(std::size_t index) const
  { return identity + ".row." + std::to_string(index); }
  std::optional<std::size_t> item_index(std::string_view text) const
  {
    const auto prefix = identity + ".row.";
    if (!text.starts_with(prefix))
      return {};
    text.remove_prefix(prefix.size());
    std::size_t index  = 0;
    const auto  result = std::from_chars(text.data(), text.data() + text.size(), index);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || text != std::to_string(index))
      return {};
    return index;
  }
  const std::string& id() const
  { return identity; }
};
} // namespace mod_settings

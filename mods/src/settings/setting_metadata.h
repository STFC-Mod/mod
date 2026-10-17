#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace mod_settings
{
enum class ApplyTiming { Live, ReopenView, Restart };
enum class SaveState { None, Pending, Saved, Conflict, Failed, Unavailable, SessionOnly };
struct SaveStatus {
  SaveState     state                               = SaveState::None;
  std::uint64_t revision                            = 0;
  bool          operator==(const SaveStatus&) const = default;
};
struct SettingMetadata {
  std::string section, key, description, unit;
  ApplyTiming timing = ApplyTiming::Live;
  bool        persisted() const
  { return !section.empty() && !key.empty(); }
  std::string TomlKey() const
  { return persisted() ? section + "." + key : std::string{}; }
};
// A confirmation captures its comparison state before opening. A page reset
// validates every plan before applying any of them.
struct PreparedSettingAction {
  std::function<bool()> current, apply;
};

inline const char* SaveStatusText(SaveStatus status)
{
  switch (status.state) {
    case SaveState::SessionOnly:
      return "Applied for this session; not saved";
    case SaveState::Pending:
      return "Saving...";
    case SaveState::Saved:
      return "Saved";
    case SaveState::Conflict:
      return "Applied; file changed externally";
    case SaveState::Failed:
      return "Applied; saving failed";
    case SaveState::Unavailable:
      return "Applied for this session; saving unavailable";
    default:
      return "";
  }
}
} // namespace mod_settings

#pragma once
#include "shortcut_draft.h"

namespace mod_settings
{
// One immediate change per editor. Undo restores the exact list (including order and
// existing duplicates), and cannot overwrite a subsequent binding change.
class ShortcutUndo
{
public:
  explicit ShortcutUndo(ValueSetting<ShortcutList>& owner)
      : owner_(owner)
  {
  }
  Outcome Remove(std::size_t index)
  {
    Clear();
    const auto before = owner_.Observe();
    if (!before.state.known() || index >= before.state.value->size())
      return Outcome::Rejected;
    auto       desired = *before.state.value;
    const auto removed = desired[index];
    desired.erase(desired.begin() + index);
    return Commit(std::move(desired), before, "Removed " + removed);
  }
  Outcome Restore(ShortcutList defaults)
  {
    Clear();
    const auto before = owner_.Observe();
    if (!before.state.known())
      return Outcome::Rejected;
    return Commit(std::move(defaults), before, "Restored defaults");
  }
  Outcome Undo()
  {
    if (!original_)
      return Outcome::Suppressed;
    struct Scope {
      bool& active;
      explicit Scope(bool& value)
          : active(value)
      { active = true; }
      ~Scope()
      { active = false; }
    } scope(restoring_);
    const auto result = owner_.SetFromUser(*original_, after_).outcome;
    Clear();
    return result;
  }
  bool available() const
  { return original_.has_value(); }
  const std::string& message() const
  { return message_; }
  // An oversized player-authored list may be restored only by this exact undo,
  // not used as permission for arbitrary additions beyond the UI's row budget.
  bool Restoring(const ShortcutList& value) const
  { return restoring_ && original_ && value == *original_; }
  void Clear()
  {
    original_.reset();
    after_ = {};
    message_.clear();
  }

private:
  Outcome Commit(ShortcutList desired, const ValueSnapshot<ShortcutList>& before, std::string message)
  {
    const auto result = owner_.SetFromUser(std::move(desired), before);
    if (result.outcome == Outcome::AppliedVerified) {
      original_ = *before.state.value;
      after_    = result.snapshot;
      message_  = std::move(message);
    }
    return result.outcome;
  }
  ValueSetting<ShortcutList>& owner_;
  std::optional<ShortcutList> original_;
  ValueSnapshot<ShortcutList> after_;
  std::string                 message_;
  bool                        restoring_ = false;
};
} // namespace mod_settings

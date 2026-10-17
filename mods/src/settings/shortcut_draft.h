#pragma once
#include "value_settings.h"
#include <algorithm>
#include <vector>

namespace mod_settings
{
using ShortcutList = std::vector<std::string>;
enum class ShortcutStage { Staged, AlreadyBound, Invalid };
// A draft edits one alternative, or restores the action's complete default list,
// against the snapshot the user saw.
// The existing ValueSetting owner provides reentry/stale-read protection.
class ShortcutDraft
{
public:
  using SameBinding = std::function<bool(const std::string&, const std::string&)>;
  explicit ShortcutDraft(ValueSetting<ShortcutList>& owner, SameBinding sameBinding = std::equal_to<std::string>{})
      : owner_(owner)
      , same_binding_(std::move(sameBinding))
  {
  }
  void Begin()
  {
    Cancel();
    observed_ = owner_.Observe();
  }
  ShortcutStage Stage(std::size_t index, std::optional<std::string> replacement)
  {
    desired_.reset();
    if (!observed_.state.known()) {
      Cancel();
      return ShortcutStage::Invalid;
    }
    auto value = *observed_.state.value;
    if (index > value.size() || (!replacement && index == value.size())) {
      Cancel();
      return ShortcutStage::Invalid;
    }
    // Reject an existing alternative without changing the snapshot or publishing
    // a draft. Re-recording the selected binding is also a no-op.
    if (replacement && std::any_of(value.begin(), value.end(), [&](const auto& binding) {
          return same_binding_(binding, *replacement);
        }))
      return ShortcutStage::AlreadyBound;
    if (!replacement)
      value.erase(value.begin() + index);
    else if (index == value.size())
      value.push_back(*replacement);
    else
      value[index] = *replacement;
    desired_ = std::move(value);
    return ShortcutStage::Staged;
  }
  bool pending() const
  { return desired_.has_value(); }
  ShortcutStage Restore(ShortcutList defaults)
  {
    desired_.reset();
    if (!observed_.state.known())
      return ShortcutStage::Invalid;
    if (defaults == *observed_.state.value)
      return ShortcutStage::AlreadyBound;
    desired_ = std::move(defaults);
    return ShortcutStage::Staged;
  }
  const std::optional<ShortcutList>& desired() const
  { return desired_; }
  Outcome Apply()
  {
    if (!desired_)
      return Outcome::Suppressed;
    const auto result = owner_.SetFromUser(*desired_, observed_).outcome;
    Cancel();
    return result;
  }
  void Cancel()
  {
    desired_.reset();
    observed_ = {};
  }

private:
  ValueSetting<ShortcutList>& owner_;
  SameBinding                 same_binding_;
  ValueSnapshot<ShortcutList> observed_;
  std::optional<ShortcutList> desired_;
};
} // namespace mod_settings

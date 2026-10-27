#pragma once

#include "value_settings.h"

namespace mod_settings
{
// One instance per live view, never a persisted copy of the preference. Native
// adapters own rendering and invalidate this view before its context is released.
template <typename T> class ValueView
{
public:
  using Snapshot    = ValueSnapshot<T>;
  using WriteResult = ValueWriteResult<T>;
  explicit ValueView(ValueSetting<T>& setting)
      : setting_(setting)
  {
  }

  void Bind()
  {
    snapshot_   = setting_.Observe();
    unresolved_ = !snapshot_.state.known();
    failed_     = false;
    bound_      = true;
  }
  void Unbind()
  {
    snapshot_   = {};
    bound_      = false;
    unresolved_ = true;
    failed_     = false;
  }
  void Invalidate()
  {
    snapshot_   = {};
    unresolved_ = true;
    failed_     = false;
  }
  WriteResult Request(T desired)
  {
    if (!editable())
      return {Outcome::Rejected, snapshot_};
    auto result = setting_.SetFromUser(desired, snapshot_);
    if (result.outcome == Outcome::Suppressed || result.outcome == Outcome::Busy)
      return result;
    snapshot_   = result.snapshot;
    failed_     = result.outcome != Outcome::AppliedVerified && result.outcome != Outcome::Unchanged;
    unresolved_ = !snapshot_.state.known() || result.outcome == Outcome::Unverified;
    return result;
  }
  WriteResult Reset()
  {
    if (!editable())
      return {Outcome::Rejected, snapshot_};
    return Accept(setting_.Reset(snapshot_));
  }
  WriteResult RetrySave()
  {
    if (!editable())
      return {Outcome::Rejected, snapshot_};
    return Accept(setting_.RetrySave(snapshot_));
  }
  bool editable() const
  { return bound_ && !unresolved_ && snapshot_.state.known(); }
  bool failed() const
  { return failed_; }
  UnavailableReason unavailableReason() const
  { return snapshot_.state.reason; }
  std::optional<T> value() const
  { return editable() ? snapshot_.state.value : std::nullopt; }
  ValueSetting<T>& setting() const
  { return setting_; }

private:
  WriteResult Accept(WriteResult result)
  {
    if (result.outcome != Outcome::Suppressed && result.outcome != Outcome::Busy) {
      snapshot_   = result.snapshot;
      failed_     = result.outcome != Outcome::AppliedVerified && result.outcome != Outcome::Unchanged;
      unresolved_ = !snapshot_.state.known() || result.outcome == Outcome::Unverified;
    }
    return result;
  }
  ValueSetting<T>& setting_;
  Snapshot         snapshot_;
  bool             bound_      = false;
  bool             unresolved_ = true;
  bool             failed_     = false;
};
} // namespace mod_settings

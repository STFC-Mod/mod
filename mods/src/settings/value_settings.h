#pragma once

#include "setting_metadata.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace mod_settings
{
enum class Availability { Known, Unavailable, Unsupported };
enum class UnavailableReason { Retry, OutsideRange, InvalidValue };
template <typename T> struct ValueReadResult {
  Availability           availability = Availability::Unavailable;
  std::optional<T>       value;
  std::uint64_t          generation = 0;
  UnavailableReason      reason     = UnavailableReason::Retry;
  static ValueReadResult Known(T value, std::uint64_t generation)
  { return {Availability::Known, value, generation}; }
  bool known() const
  { return availability == Availability::Known && value.has_value() && generation != 0; }
};
enum class ApplyResult { Applied, Rejected, Unverified };
enum class Outcome { AppliedVerified, Unchanged, Suppressed, Busy, Conflict, Rejected, Unverified };
template <typename T> struct ValueDefinition {
  std::string                         id;
  std::string                         label;
  std::function<ValueReadResult<T>()> read;
  // Adapter must revalidate its own generation immediately before mutation.
  std::function<ApplyResult(T, std::uint64_t)> write;
  SettingMetadata                              metadata;
  std::optional<T>                             default_value;
  // Called only for an explicit, verified user intent, never by Observe/render.
  std::function<void(const T&)> persist;
  std::function<SaveStatus(const T&)> save_status;
  // Restart-only adapters expose their still-active value independently from
  // the requested value returned by read.
  std::function<std::optional<T>()> active_value;
};
template <typename T> struct ValueSnapshot {
  ValueReadResult<T> state;
  std::uint64_t      revision = 0;
  std::uint64_t      epoch    = 0;
  std::uint64_t      owner    = 0;
};
template <typename T> struct ValueWriteResult {
  Outcome          outcome;
  ValueSnapshot<T> snapshot;
};

// All calls, including scope destruction, belong to the constructing UI thread.
// No timers, background work, locks, storage, or IL2CPP dependencies.
template <typename T> class ValueSetting
{
public:
  using ReadResult  = ValueReadResult<T>;
  using Definition  = ValueDefinition<T>;
  using Snapshot    = ValueSnapshot<T>;
  using WriteResult = ValueWriteResult<T>;
  explicit ValueSetting(Definition definition)
      : definition_(std::move(definition))
  {
  }
  ValueSetting(const ValueSetting&)                 = delete;
  ValueSetting&      operator=(const ValueSetting&) = delete;
  const std::string& id() const
  { return definition_.id; }
  const std::string& label() const
  { return definition_.label; }
  const SettingMetadata& metadata() const
  { return definition_.metadata; }
  const std::optional<T>& default_value() const
  { return definition_.default_value; }
  SaveStatus save_status()
  {
    CheckThread();
    try {
      const auto current = Observe();
      if (!current.state.known())
        return {};
      if (persistence_failed_ && *persistence_failed_ == *current.state.value)
        return {SaveState::Unavailable};
      return definition_.save_status ? definition_.save_status(*current.state.value) : SaveStatus{};
    } catch (...) {
      return {SaveState::Unavailable};
    }
  }
  bool restart_pending()
  {
    CheckThread();
    if (metadata().timing != ApplyTiming::Restart || !definition_.active_value)
      return false;
    try {
      const auto requested = Observe();
      const auto active    = definition_.active_value();
      return requested.state.known() && active && requested.state.value != active;
    } catch (...) {
      return false;
    }
  }
  std::string status_text()
  {
    CheckThread();
    // A historical save completion must never imply that an unavailable live
    // setting was just applied. Observation remains read-only.
    if (!Observe().state.known())
      return {};
    const auto  status          = save_status();
    const bool  restart         = metadata().timing == ApplyTiming::Restart;
    const bool  pending_restart = restart_pending();
    std::string text;
    switch (status.state) {
      case SaveState::SessionOnly:
        text = "Applied for this session; not saved";
        break;
      case SaveState::Pending:
        text = restart ? "Saving..." : "Applied; saving...";
        break;
      case SaveState::Saved:
        text = restart ? "Saved" : "Applied and saved";
        break;
      case SaveState::Conflict:
        text = restart ? "Not saved; file changed externally" : "Applied; file changed externally";
        break;
      case SaveState::Failed:
        text = restart ? "Saving failed" : "Applied; saving failed";
        break;
      case SaveState::Unavailable:
        text = restart ? "Saving unavailable" : "Applied for this session; saving unavailable";
        break;
      default:
        break;
    }
    if (pending_restart)
      text += text.empty() ? "Restart required" : "; restart required";
    else if (metadata().timing == ApplyTiming::ReopenView && status.state != SaveState::None)
      text += "; reopen the affected view";
    return text;
  }

  // One process-lifetime presentation adapter. Notifications are synchronous,
  // bounded and run under the write reentry guard; rendering cannot write back.
  bool SetChangeObserver(void (*observer)())
  {
    CheckThread();
    if (change_observer_ && change_observer_ != observer)
      return false;
    change_observer_ = observer;
    return true;
  }

  class RenderScope
  {
  public:
    explicit RenderScope(ValueSetting& setting)
        : setting_(setting)
    {
      setting_.CheckThread();
      ++setting_.render_depth_;
    }
    ~RenderScope()
    { --setting_.render_depth_; }
    RenderScope(const RenderScope&)            = delete;
    RenderScope& operator=(const RenderScope&) = delete;

  private:
    ValueSetting& setting_;
  };

  Snapshot Observe()
  {
    CheckThread();
    if (observing_)
      return {{}, current_.revision, epoch_, identity_};
    struct Observing {
      bool& flag;
      Observing(bool& flag)
          : flag(flag)
      { flag = true; }
      ~Observing()
      { flag = false; }
    } guard(observing_);
    const auto read_epoch = epoch_;
    ReadResult next;
    try {
      next = definition_.read();
    } catch (...) {
      next = {};
    }
    if (read_epoch != epoch_)
      next = {};
    if (!next.known()) {
      next.value.reset();
      next.generation = 0;
      if (next.availability == Availability::Known)
        next.availability = Availability::Unavailable;
    }
    if (next.availability != current_.state.availability || next.value != current_.state.value
        || next.generation != current_.state.generation || next.reason != current_.state.reason)
      ++current_.revision;
    current_.state = next;
    current_.epoch = epoch_;
    current_.owner = identity_;
    return current_;
  }
  bool IsCurrent(const Snapshot& observed)
  {
    CheckThread();
    if (render_depth_ || applying_)
      return false;
    return Matches(observed, Observe());
  }

  void InvalidateSession()
  {
    CheckThread();
    ++epoch_;
    current_ = {{}, current_.revision + 1, epoch_, identity_};
  }

  WriteResult SetFromUser(T desired, const Snapshot& observed)
  {
    CheckThread();
    if (render_depth_)
      return {Outcome::Suppressed, current_};
    if (applying_)
      return {Outcome::Busy, current_};
    // Cover read callbacks as well as the setter: neither can reenter application.
    struct Applying {
      bool& flag;
      Applying(bool& flag)
          : flag(flag)
      { flag = true; }
      ~Applying()
      { flag = false; }
    } guard(applying_);
    const auto before = Observe();
    if (!before.state.known())
      return {Outcome::Rejected, before};
    if (!Matches(observed, before))
      return {Outcome::Conflict, before};
    if (*before.state.value == desired) {
      Persist(desired);
      NotifyChanged();
      if (epoch_ != before.epoch)
        return {Outcome::Unverified, current_};
      return {Outcome::Unchanged, before};
    }
    ApplyResult applied = ApplyResult::Unverified;
    try {
      applied = definition_.write(desired, before.state.generation);
    } catch (...) {
    }
    if (epoch_ != before.epoch)
      return {Outcome::Unverified, current_};
    const auto after = Observe();
    if (!after.state.known() || after.state.generation != before.state.generation)
      return {Outcome::Unverified, after};
    if (applied == ApplyResult::Applied && *after.state.value == desired) {
      Persist(*after.state.value);
      NotifyChanged();
      if (epoch_ != before.epoch)
        return {Outcome::Unverified, current_};
      return {Outcome::AppliedVerified, after};
    }
    // Preserve the authoritative readback even when the adapter reports failure.
    return {applied == ApplyResult::Rejected ? Outcome::Rejected : Outcome::Unverified, after};
  }

  WriteResult Reset(const Snapshot& observed)
  {
    CheckThread();
    return definition_.default_value ? SetFromUser(*definition_.default_value, observed)
                                     : WriteResult{Outcome::Rejected, current_};
  }
  WriteResult RetrySave(const Snapshot& observed)
  {
    CheckThread();
    if (!definition_.persist || !observed.state.known())
      return {Outcome::Rejected, current_};
    return SetFromUser(*observed.state.value, observed);
  }

private:
  bool Matches(const Snapshot& observed, const Snapshot& before) const
  {
    return observed.owner == identity_ && observed.state.known() && before.state.known()
           && observed.epoch == before.epoch && observed.revision == before.revision
           && observed.state.generation == before.state.generation && observed.state.value == before.state.value;
  }
  void Persist(const T& value)
  {
    try {
      if (definition_.persist)
        definition_.persist(value);
      persistence_failed_.reset();
    } catch (...) {
      persistence_failed_ = value;
    }
  }
  void NotifyChanged()
  {
    // Presentation failure does not undo or misreport an authoritative write.
    try {
      if (change_observer_)
        change_observer_();
    } catch (...) {
    }
  }
  void CheckThread() const
  {
    if (std::this_thread::get_id() != thread_)
      throw std::logic_error("setting thread mismatch");
  }
  Definition                               definition_;
  inline static std::atomic<std::uint64_t> next_identity_{1};
  const std::uint64_t                      identity_ = next_identity_.fetch_add(1);
  const std::thread::id                    thread_   = std::this_thread::get_id();
  Snapshot                                 current_;
  std::uint64_t                            epoch_              = 1;
  unsigned                                 render_depth_       = 0;
  bool                                     applying_           = false;
  bool                                     observing_          = false;
  std::optional<T>                         persistence_failed_;
  void (*change_observer_)()                                   = nullptr;
};

template <typename T> PreparedSettingAction PrepareReset(ValueSetting<T>& setting)
{
  const auto snapshot = setting.Observe();
  return {[&setting, snapshot] { return setting.default_value().has_value() && setting.IsCurrent(snapshot); },
          [&setting, snapshot] {
            const auto result = setting.Reset(snapshot).outcome;
            return result == Outcome::AppliedVerified || result == Outcome::Unchanged;
          }};
}

} // namespace mod_settings

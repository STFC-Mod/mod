#include "runtime_config_writer.h"
#include <algorithm>

#if _WIN32
#include <Windows.h>
#endif

#ifndef CONFIG_EDIT_SAVE
#define CONFIG_EDIT_SAVE(editor, path, request) (editor).Save(path, request)
#endif
#ifndef CONFIG_EDIT_START_WORKER
#define CONFIG_EDIT_START_WORKER(...) std::thread(__VA_ARGS__)
#endif

namespace config_edit
{
RuntimeConfigWriter::RuntimeConfigWriter(std::filesystem::path path, Reporter report)
    : path_(std::move(path))
    , report_(report)
{
}

RuntimeConfigWriter::~RuntimeConfigWriter()
{
  Stop(false);
  if (worker_.joinable())
    worker_.join();
}

bool RuntimeConfigWriter::Register(std::string section, std::string key, std::optional<Value> initial)
{
  std::lock_guard lock(mutex_);
  if (stopping_ || cancel_pending_.load() || section.empty() || key.empty())
    return false;
  saved_.try_emplace(Key{std::move(section), std::move(key)},
                     Saved{std::move(initial), false, {0, Outcome::AlreadySaved, false, true}});
  return true;
}

std::uint64_t RuntimeConfigWriter::Submit(std::string section, std::string key, Value desired,
                                          std::chrono::milliseconds delay)
{
  std::lock_guard lock(mutex_);
  const Key       identity{section, key};
  const auto      saved = saved_.find(identity);
  if (stopping_ || cancel_pending_.load() || saved == saved_.end())
    return 0;
  pending_.insert_or_assign(identity,
                            Pending{++revision_,
                                    {std::move(section), std::move(key), saved->second.value, std::move(desired)},
                                    std::chrono::steady_clock::now() + delay});
  saved->second.status = {revision_, saved->second.status.outcome, true, true, pending_.at(identity).edit.desired};
  ++status_sequence_;
  has_work_.store(true);
  if (!worker_.joinable()) {
    try {
      worker_ = CONFIG_EDIT_START_WORKER(&RuntimeConfigWriter::Run, this);
    } catch (...) {
      pending_.clear();
      has_work_.store(false);
      completion_          = {revision_, Outcome::IoError};
      saved->second.failed = true;
      saved->second.status = {revision_, Outcome::IoError, false, true, saved->second.status.subject};
      ++status_sequence_;
      has_failures_.store(true);
      return 0;
    }
  }
  wake_.notify_one();
  return revision_;
}

void RuntimeConfigWriter::Stop(bool cancel_pending)
{
  if (cancel_pending)
    RequestCancelPending();
  std::lock_guard lock(mutex_);
  stopping_ = true;
  if (cancel_pending && !pending_.empty()) {
    completion_ = {revision_, Outcome::Cancelled};
    for (const auto& [identity, pending] : pending_) {
      auto& saved = saved_.at(identity);
      if (saved.status.revision == pending.revision) {
        saved.status = {pending.revision, Outcome::Cancelled, false, true, pending.edit.desired};
        saved.failed = true;
      }
    }
    has_failures_.store(true);
    ++status_sequence_;
    pending_.clear();
  }
  wake_.notify_one();
}

void RuntimeConfigWriter::RequestCancelPending()
{
  cancel_pending_.store(true);
  wake_.notify_one();
}

RuntimeConfigWriter::Completion RuntimeConfigWriter::LastCompletion()
{
  std::lock_guard lock(mutex_);
  return completion_;
}
bool RuntimeConfigWriter::HasFailure(std::string_view section, std::string_view key)
{
  std::lock_guard lock(mutex_);
  const auto      found = saved_.find(Key{std::string(section), std::string(key)});
  return found != saved_.end() && found->second.failed;
}
RuntimeConfigWriter::Status RuntimeConfigWriter::GetStatus(std::string_view section, std::string_view key)
{
  std::lock_guard lock(mutex_);
  const auto      found = saved_.find(Key{std::string(section), std::string(key)});
  return found == saved_.end() ? Status{} : found->second.status;
}

void RuntimeConfigWriter::Run()
{
  for (;;) {
    Pending work;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] { return stopping_ || cancel_pending_.load() || !pending_.empty(); });
      if (cancel_pending_.load()) {
        stopping_ = true;
        if (!pending_.empty()) {
          completion_ = {revision_, Outcome::Cancelled};
          for (const auto& [identity, pending] : pending_) {
            auto& saved = saved_.at(identity);
            if (saved.status.revision == pending.revision) {
              saved.status = {pending.revision, Outcome::Cancelled, false, true, pending.edit.desired};
              saved.failed = true;
            }
          }
          has_failures_.store(true);
          ++status_sequence_;
        }
        pending_.clear();
      }
      if (pending_.empty())
        break;
      auto next = std::min_element(pending_.begin(), pending_.end(),
                                   [](const auto& a, const auto& b) { return a.second.ready < b.second.ready; });
      if (!stopping_ && next->second.ready > std::chrono::steady_clock::now()) {
        const auto deadline = next->second.ready;
        wake_.wait_until(lock, deadline);
        continue; // New submissions may move a deadline; orderly stop flushes it.
      }
      work = std::move(next->second);
      pending_.erase(next);
    }
    Outcome outcome;
    try {
      outcome = CONFIG_EDIT_SAVE(editor_, path_, work.edit);
    } catch (...) {
      outcome = Outcome::IoError;
    }
    {
      std::lock_guard lock(mutex_);
      if (outcome == Outcome::Saved || outcome == Outcome::AlreadySaved) {
        // Rebase our queued intent over our own successful write, never over a
        // conflicting external edit. Failed saves leave the acknowledged value alone.
        const Key identity{work.edit.section, work.edit.key};
        auto&     saved = saved_.at(identity).value;
        if (auto pending = pending_.find(identity); pending != pending_.end() && pending->second.edit.expected == saved)
          pending->second.edit.expected = work.edit.desired;
        saved = work.edit.desired;
      }
      // A successful B save must not hide an unsaved A change.
      auto& saved_state = saved_.at(Key{work.edit.section, work.edit.key});
      auto& status      = saved_state.status;
      // A queued newer cancellation owns the final failure state; an older
      // in-flight success must not make its unsaved value appear acknowledged.
      if (status.revision == work.revision || status.pending)
        saved_state.failed = outcome != Outcome::Saved && outcome != Outcome::AlreadySaved;
      if (status.revision == work.revision)
        status = {work.revision, outcome, false, true, work.edit.desired};
      ++status_sequence_;
      has_failures_.store(
          std::any_of(saved_.begin(), saved_.end(), [](const auto& entry) { return entry.second.failed; }));
      if (work.revision >= completion_.revision)
        completion_ = {work.revision, outcome};
    }
    if (report_ && outcome != Outcome::Saved && outcome != Outcome::AlreadySaved) {
      try {
        report_(work.edit.section, work.edit.key, outcome);
      } catch (...) { /* Diagnostics cannot kill the worker. */
      }
    }
    {
      std::lock_guard lock(mutex_);
      // A diagnostic callback is still active worker work, even after disk I/O.
      has_work_.store(!pending_.empty());
    }
  }
  has_work_.store(false);
  finished_.store(true);
}

bool RuntimeConfigWriter::PollStopped()
{
  if (!worker_.joinable()) {
    std::lock_guard lock(mutex_);
    return stopping_;
  }
#if _WIN32
  if (WaitForSingleObject(worker_.native_handle(), 0) != WAIT_OBJECT_0)
    return false;
#else
  // The worker has completed its disk work and published its final state.
  if (!finished_.load())
    return false;
#endif
  worker_.join();
  return true;
}

bool RuntimeConfigWriter::Resume()
{
  std::lock_guard lock(mutex_);
  if (worker_.joinable() || !stopping_ || cancel_pending_.load() || !pending_.empty())
    return false;
  stopping_ = false;
  finished_.store(false);
  return true;
}

#if _WIN32
void* RuntimeConfigWriter::NativeHandle()
{ return worker_.joinable() ? worker_.native_handle() : nullptr; }
#endif
} // namespace config_edit

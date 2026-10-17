#pragma once
#if _WIN32
#include <Windows.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace spdlog
{
template <class... Args> void warn(const char*, Args&&...) {}
} // namespace spdlog
namespace config_edit
{
using Value = std::variant<bool, std::string, double, std::int64_t>;
enum class Outcome { Saved, AlreadySaved, Conflict, InvalidDocument, Unsupported, IoError };
// Controllable boundary; the fixture below includes the actual adapter bodies.
struct RuntimeConfigWriter {
  struct Status {
    std::uint64_t revision = 0;
    Outcome       outcome  = Outcome::AlreadySaved;
    bool          pending = false, registered = true;
    std::optional<Value> subject;
  };
  std::uint64_t StatusSequence() const
  { return submissions; }
  bool Register(std::string, std::string, std::optional<Value>)
  { return !stopped; }
  Status GetStatus(std::string_view, std::string_view) const
  { return {submissions, failures ? Outcome::IoError : Outcome::Saved, work, true, desired}; }
  std::optional<Value> desired;
  bool failures = false;
  bool HasFailures() const
  { return failures; }
  bool HasFailure(std::string_view, std::string_view) const
  { return failures; }
  bool     work = false, stopped = false, finished = false, cancelled = false;
  bool     block_cancel = false;
  unsigned submissions  = 0;
  void*    handle       = nullptr;
  bool     HasWork() const
  { return work; }
  void RequestCancelPending()
  { cancelled = true; }
  void Stop(bool cancel)
  {
    stopped   = true;
    cancelled = cancel;
    if (cancel && block_cancel) {
      std::puts("pending cancellation requested");
      std::fflush(stdout);
#if _WIN32
      Sleep(INFINITE); // Deadline must be independent of this stalled caller.
#endif
    }
  }
  bool PollStopped() const
  { return finished; }
  bool Resume()
  {
    if (!stopped || !finished || cancelled)
      return false;
    stopped = finished = false;
    return true;
  }
  void* NativeHandle() const
  { return handle; }
  unsigned Submit(const char*)
  { return stopped || failures ? 0 : ++submissions; }
  unsigned Submit(const char*, const char*, Value value, std::chrono::milliseconds)
  {
    desired = std::move(value);
    return Submit("");
  }
};
} // namespace config_edit

// Observe actual callback registration; tests must not manually call an Update
// that the adapter failed to arrange for the no-persistence case.
inline void (*fixture_update_callback)() = nullptr;
inline bool install_screen_manager_update_hook()
{ return true; }
inline bool register_screen_manager_update_callback(void (*callback)())
{
  fixture_update_callback = callback;
  return true;
}

#include "action_queue.h"
#include "features/features.h"
#include <config.h>

// Use the host ABI for the 24-byte value-type callback on Windows and both Mac architectures.
// Validate the managed layout before installing.
#include "game/queues.h"
#include <array>
#include <atomic>
#include <cstring>
#include <il2cpp/hook.h>
#include <mutex>
#include <spdlog/spdlog.h>
#include <utility>

using namespace queue_bindings::recovery;

namespace
{
using action_queue::Clock;
using action_queue::QueueState;
action_queue::Requests<WeakQueues> requests;
std::mutex                         requestsMutex;
std::atomic_bool                   ready{false};

bool Enabled()
{ return ready.load() && Config::Get().control.kirshara_fixes && Config::Get().control.queue_enabled; }
void ClearRequests()
{
  std::lock_guard lock(requestsMutex);
  requests.Clear();
}
struct CourseContext {
  std::int64_t  fleet{}, target{};
  Il2CppObject* queue{}; // Borrowed only inside the synchronous native
                         // ActionQueueManager_OnSetCourseResponseEventHandler_Hook call.
  bool outstandingAtEntry{};
};
thread_local CourseContext* currentCourse{};

int ActionQueueManager_TryPlanPathAndEngageTarget_Hook(auto original, Il2CppObject* manager, Il2CppObject* player,
                                                       Il2CppObject* queue)
{
  std::uint64_t serial{};
  if (Enabled()) {
    try {
      const auto      s = Inspect(queue);
      std::lock_guard lock(requestsMutex);
      serial = requests.Remember(queue, s, Clock::now());
    } catch (...) {
      ClearRequests();
    }
  } else {
    ClearRequests();
  }
  const auto result = original(manager, player, queue);
  // Result zero means native dispatch succeeded. Do not cancel a newer reentrant request.
  if (serial && result != 0) {
    std::lock_guard lock(requestsMutex);
    requests.Cancel(serial);
  }
  return result;
}
bool ActionQueueManager_ShouldRetryFailedSetCourse_Hook(auto original, Il2CppObject* manager, std::int64_t target,
                                                        Il2CppObject* queue)
{
  if (!Enabled())
    ClearRequests();
  const bool retry = original(manager, target, queue);
  if (retry || !Enabled() || !currentCourse)
    return retry;
  try {
    const auto& c = *currentCourse;
    const auto  s = Inspect(queue, target);
    if (c.target != target
        || !action_queue::CanAdvance(s, c.fleet, target, c.outstandingAtEntry, c.queue == queue, true))
      return retry;
    // The native false branch processes this target across all fleets. Never suppress that work
    // while any other queue still contains it or has contents we cannot verify.
    if (!AbsentFromAllQueues(manager, target))
      return retry;
    std::lock_guard lock(requestsMutex);
    if (!requests.Consume(queue, s, target, Clock::now()))
      return retry;
    // The enclosing ActionQueueManager_OnSetCourseResponseEventHandler_Hook handler's true branch calls its normal
    // planner for this same fleet. That planner retains all eligibility checks. No flags, targets or retry counters are
    // changed here.
    return true;
  } catch (...) {
    return retry;
  }
}
void ActionQueueManager_OnSetCourseResponseEventHandler_Hook(auto original, Il2CppObject* manager, CourseResponse args)
{
  if (!Enabled())
    ClearRequests();
  CourseContext context{args.fleet};
  if (Enabled() && !args.success && !args.recall) {
    try {
      if (ReadCourseTarget(args.target, context.target)) {
        context.queue              = FindQueue(manager, args.fleet);
        context.outstandingAtEntry = context.queue && Read<bool>(context.queue, 0x10);
      }
    } catch (...) {
      context = {};
    }
  }
  struct Scope {
    CourseContext* previous{currentCourse};
    explicit Scope(CourseContext* value)
    { currentCourse = value; }
    ~Scope()
    { currentCourse = previous; }
  } scope(&context);
  original(manager, args);
}
void ActionQueueManager_StopWatchdogAndClearAllQueues_Hook(auto original, Il2CppObject* manager)
{
  // Native session end/invalidation/quit share this seam; release weak handles while IL2CPP is alive.
  ClearRequests();
  original(manager);
}

} // namespace

void ResetQueueRecoveryTracking()
{ ClearRequests(); }

namespace
{
void InstallActionQueueRecovery()
{
  if (!ResolveBindings())
    return;
  const bool a = IL2CPP_DETOUR(engage, ActionQueueManager_TryPlanPathAndEngageTarget_Hook,
                               "ActionQueueManager_TryPlanPathAndEngageTarget_Hook")
                 != nullptr;
  const bool b = IL2CPP_DETOUR(retry, ActionQueueManager_ShouldRetryFailedSetCourse_Hook,
                               "ActionQueueManager_ShouldRetryFailedSetCourse_Hook")
                 != nullptr;
  const bool c = IL2CPP_DETOUR(course, ActionQueueManager_OnSetCourseResponseEventHandler_Hook,
                               "ActionQueueManager_OnSetCourseResponseEventHandler_Hook")
                 != nullptr;
  const bool d = IL2CPP_DETOUR(clear, ActionQueueManager_StopWatchdogAndClearAllQueues_Hook,
                               "ActionQueueManager_StopWatchdogAndClearAllQueues_Hook")
                 != nullptr;
  for (const auto& [installed, name] : std::array{std::pair{a, "ActionQueueManager.TryPlanPathAndEngageTarget"},
                                                  std::pair{b, "ActionQueueManager.ShouldRetryFailedSetCourse"},
                                                  std::pair{c, "ActionQueueManager.OnSetCourseResponseEventHandler"},
                                                  std::pair{d, "ActionQueueManager.StopWatchdogAndClearAllQueues"}})
    if (!installed)
      spdlog::warn("[FasterQueueRecovery] hook installation failed: {}; feature unavailable", name);
  ready.store(a && b && c && d);
  spdlog::info("[FasterQueueRecovery] ready={}", ready.load());
}
} // namespace

const Feature kActionQueueRecovery{"ActionQueueRecovery", &Config::Patches::actionqueuerecoveryhooks,
                                   InstallActionQueueRecovery};

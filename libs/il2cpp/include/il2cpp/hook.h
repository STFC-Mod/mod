#pragma once

// Detours on managed methods and runtime functions. A missing method is already reported by its lookup; these
// report a detour SPUD could not install, which SPUD_STATIC_DETOUR alone would drop without a word. Like it, they
// install permanently: nothing removes them, so their stubs skip SPUD's per-call entry counting.
//
// Hooks take the original as their first parameter:
//   void ScreenManager_Update_Hook(auto original, ScreenManager* _this) { ...; original(_this); }
//   IL2CPP_HOOK(ScreenManager::Class::Method("Update"), ScreenManager_Update_Hook);
// Each expansion installs one detour on its first call, so expand it once per target: a second call from the same
// place returns the first call's trampoline and installs nothing.

#include "il2cpp.h"

#include <spud/detour.h>
#include <spud/diagnostics.h>

#include <string>
#include <string_view>

namespace il2cpp::detail
{
void ReportDetourFailure(std::string_view target, const char* error);

constexpr const char* DetourErrorName(spud::detour_error error)
{
  using E = spud::detour_error;
  switch (error) {
    case E::kOk:
      return "ok";
    case E::kUnsupportedArch:
      return "unsupported architecture";
    case E::kDecodeFailed:
      return "prologue could not be decoded";
    case E::kInsufficientBytes:
      return "function too short for a detour";
    case E::kProtectionFailed:
      return "page protection could not be changed";
    case E::kSuspensionUnavailable:
      return "other threads could not be suspended";
    case E::kTargetChanged:
      return "target changed while installing";
    case E::kBusy:
      return "detour busy";
    case E::kChainFull:
      return "too many detours on one function";
    case E::kControlFlowEnforced:
      return "control-flow protection";
    case E::kNotFound:
      return "not found";
    case E::kUnwindFailed:
      return "unwind metadata could not be registered";
    case E::kRollbackFailed:
      return "rollback failed";
  }
  return "unknown error";
}

inline bool Installed(spud::detour_error error, std::string_view target)
{
  if (error == spud::detour_error::kOk)
    return true;
  const auto  detail = spud::last_failure_detail();
  std::string message = DetourErrorName(error);
  if (detail.reason)
    message += std::string(": ") + detail.reason;
  ReportDetourFailure(target, message.c_str());
  return false;
}

inline bool Installed(spud::detour_error error, const Method& target)
{ return error == spud::detour_error::kOk || Installed(error, target.Name()); }
} // namespace il2cpp::detail

// Detours `address` with `hook`; `name` (a string or an il2cpp::Method) labels a failure. Evaluates to the
// trampoline that calls the original, or null when nothing was installed.
#define IL2CPP_DETOUR(address, hook, name)                                                                             \
  ([&](void* target_) {                                                                                                \
    using Trampoline_                    = decltype(SPUD_AUTO_HOOK(target_, hook).trampoline());                       \
    static const Trampoline_ trampoline_ = [&]() -> Trampoline_ {                                                      \
      if (!target_)                                                                                                    \
        return nullptr;                                                                                                \
      auto detour_ = SPUD_AUTO_HOOK(target_, hook);                                                                    \
      if (!::il2cpp::detail::Installed(detour_.try_install_permanent(), name))                                         \
        return nullptr;                                                                                                \
      return detour_.trampoline();                                                                                     \
    }();                                                                                                               \
    return trampoline_;                                                                                                \
  }(reinterpret_cast<void*>(address)))

// Detours the native code of a managed method from Class::Method. A missing method installs nothing.
#define IL2CPP_HOOK(method, hook)                                                                                      \
  ([&](const ::il2cpp::Method& method_) { return IL2CPP_DETOUR(method_.Pointer(), hook, method_); }(method))

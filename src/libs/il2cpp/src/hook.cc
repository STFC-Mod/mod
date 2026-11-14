// Owns hook set commits and their failure reports.

#include <il2cpp/hook.h>

#include <spud/diagnostics.h>

#include <format>
#include <string>

namespace il2cpp
{
namespace
{
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

  void ReportNotInstalled(std::string_view set, std::string_view why)
  { Report(std::format("[IL2CPP] {} not installed: {}", set, why)); }
} // namespace

bool HookSet::Commit()
{
  if (missing_) {
    // The lookup has named the method; this names what it costs.
    ReportNotInstalled(name_, "a method it hooks is missing");
    return false;
  }
  if (detach_.empty())
    return false;
  if (const auto error = transaction_.commit(); error != spud::detour_error::kOk) {
    // SPUD records per thread why the failing step was refused, as a short token.
    const auto detail = spud::last_failure_detail();
    ReportNotInstalled(name_, detail.reason ? std::format("{}: {}", DetourErrorName(error), detail.reason)
                                            : DetourErrorName(error));
    detach_.clear();
    return false;
  }
  for (const auto& detach : detach_)
    detach();
  detach_.clear();
  return true;
}

void* DetourTarget(const Method& method)
{
  void* entry = method.Pointer();
  // The plan resolves the jump without writing anything. It names no target where SPUD cannot hook at all.
  const auto target = entry ? spud::inspect_detour(entry).resolved_target : 0;
  return target ? reinterpret_cast<void*>(target) : entry;
}
} // namespace il2cpp

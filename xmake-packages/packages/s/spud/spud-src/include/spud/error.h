#pragma once

// The result codes detours, pointer hooks and signature scans report.

#include <string_view>
#include <system_error>

namespace spud {

// Outcome of a checked install or explicit uninstall. `kOk` is the only
// success. Detailed OS codes stay in diagnostics; this is the caller-facing
// contract.
enum class detour_error {
  kOk = 0,
  kUnsupportedArch,
  kDecodeFailed,
  kInsufficientBytes,
  kProtectionFailed,
  kSuspensionUnavailable,
  kTargetChanged,
  kBusy,
  kChainFull,
  kControlFlowEnforced,
  kNotFound,
  kRollbackFailed,
  // The copied code's unwind or exception-handler records could not be
  // built or registered. last_failure_detail() names the refusal.
  kUnwindFailed,
};

constexpr std::string_view to_string(detour_error error) noexcept {
  switch (error) {
  case detour_error::kOk:
    return "ok";
  case detour_error::kUnsupportedArch:
    return "unsupported_arch";
  case detour_error::kDecodeFailed:
    return "decode_failed";
  case detour_error::kInsufficientBytes:
    return "insufficient_bytes";
  case detour_error::kProtectionFailed:
    return "protection_failed";
  case detour_error::kSuspensionUnavailable:
    return "suspension_unavailable";
  case detour_error::kTargetChanged:
    return "target_changed";
  case detour_error::kBusy:
    return "busy";
  case detour_error::kChainFull:
    return "chain_full";
  case detour_error::kControlFlowEnforced:
    return "control_flow_enforced";
  case detour_error::kNotFound:
    return "not_found";
  case detour_error::kRollbackFailed:
    return "rollback_failed";
  case detour_error::kUnwindFailed:
    return "unwind_failed";
  }
  return "unknown";
}

// detour_error converts to std::error_code, so `if (std::error_code ec =
// hook.try_install())` and `ec.message()` work. `kOk` is the empty code.
const std::error_category &detour_category() noexcept;

inline std::error_code make_error_code(detour_error error) noexcept {
  return {static_cast<int>(error), detour_category()};
}

} // namespace spud

template <>
struct std::is_error_code_enum<spud::detour_error> : std::true_type {};

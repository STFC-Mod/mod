#include <spud/arch.h>
#include <spud/detour.h>
#include <spud/diagnostics.h>

#include "memory/safe_patch.h"

#ifdef SPUD_OS_WIN
#include <windows.h>
#elif SPUD_OS_LINUX
#include <sys/auxv.h>
#if SPUD_ARCH_X86_FAMILY
#include <sys/syscall.h>
#include <unistd.h>
#ifndef SYS_arch_prctl
#define SYS_arch_prctl 158
#endif
#ifndef ARCH_SHSTK_STATUS
#define ARCH_SHSTK_STATUS 0x5005
#endif
#endif
#ifndef AT_HWCAP2
#define AT_HWCAP2 26
#endif
#ifndef HWCAP2_BTI
#define HWCAP2_BTI (1 << 17)
#endif
#endif

namespace spud {
namespace detail {

bool register_call_target(void *ptr, size_t size) {
  if (ptr == nullptr || size == 0) {
    return true;
  }
#ifdef SPUD_OS_WIN
  PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg{};
  if (!GetProcessMitigationPolicy(GetCurrentProcess(),
                                  ProcessControlFlowGuardPolicy, &cfg,
                                  sizeof(cfg)) ||
      !cfg.EnableControlFlowGuard) {
    return true;
  }
  // SetProcessValidCallTargets is only in umbrella import libraries such as
  // mincore.lib, not kernel32.lib. Linking one of those would also move the
  // user's other kernel32 imports to API-set names, so resolve it from
  // kernelbase.dll, which exports it on every Windows with CFG.
  using set_valid_call_targets_fn = decltype(&SetProcessValidCallTargets);
  static const auto set_valid_call_targets =
      reinterpret_cast<set_valid_call_targets_fn>(reinterpret_cast<void *>(
          GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),
                         "SetProcessValidCallTargets")));
  if (set_valid_call_targets == nullptr) {
    note_current_os_error();
    return false;
  }
  CFG_CALL_TARGET_INFO info{};
  MEMORY_BASIC_INFORMATION region{};
  if (VirtualQuery(ptr, &region, sizeof(region)) == 0 ||
      size > region.RegionSize - (static_cast<uint8_t *>(ptr) -
                                  static_cast<uint8_t *>(region.BaseAddress))) {
    note_current_os_error();
    return false;
  }
  info.Offset = reinterpret_cast<uintptr_t>(ptr) -
                reinterpret_cast<uintptr_t>(region.BaseAddress);
  info.Flags = CFG_CALL_TARGET_VALID;
  if (!set_valid_call_targets(GetCurrentProcess(), region.BaseAddress,
                              region.RegionSize, 1, &info)) {
    note_current_os_error();
    return false;
  }
  return true;
#else
  (void)ptr;
  (void)size;
  return true;
#endif
}

} // namespace detail

control_flow query_control_flow() {
  control_flow state;
#ifdef SPUD_OS_WIN
  PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg{};
  if (GetProcessMitigationPolicy(GetCurrentProcess(),
                                 ProcessControlFlowGuardPolicy, &cfg,
                                 sizeof(cfg))) {
    state.cfg = cfg.EnableControlFlowGuard != 0;
  }
  PROCESS_MITIGATION_USER_SHADOW_STACK_POLICY ss{};
  if (GetProcessMitigationPolicy(
          GetCurrentProcess(), ProcessUserShadowStackPolicy, &ss, sizeof(ss))) {
    state.shadow_stack = ss.EnableUserShadowStack != 0;
  }
#elif SPUD_OS_LINUX
#if SPUD_ARCH_X86_FAMILY
  unsigned long shstk = 0;
  if (syscall(SYS_arch_prctl, ARCH_SHSTK_STATUS, &shstk) == 0) {
    state.shadow_stack = (shstk & 1u) != 0;
  }
#endif
#if SPUD_ARCH_ARM_FAMILY
  const unsigned long hwcap2 = getauxval(AT_HWCAP2);
  state.bti = (hwcap2 & HWCAP2_BTI) != 0;
#endif
#endif
#if SPUD_ARCH_ARM64E
  state.pac = true;
#endif
  return state;
}

} // namespace spud

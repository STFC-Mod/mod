#pragma once

#include "detour/fwd.h"

#include "detour/detour_stub.h"

#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

namespace spud::detail::arm64 {
// A PC-relative instruction inside the copied prologue.
struct relocation_entry {
  // Offset from the hooked function.
  uintptr_t address = 0;
  uint32_t word = 0;
};

std::tuple<relocation_info, size_t>
collect_relocations(uintptr_t address, size_t jump_size,
                    std::span<const uint8_t> original_prefix = {},
                    size_t function_size = 0);
// `at` is the address the trampoline will run at, or 0 for a
// position-independent build. sizing_only reserves worst-case space without
// claiming the buffer is executable; a real far form can still be refused.
trampoline_buffer create_trampoline(uintptr_t at, uintptr_t return_address,
                                    std::span<const uint8_t> target,
                                    const relocation_info &relocation_infos,
                                    bool sizing_only = false,
                                    bool unwind = bool(SPUD_UNWIND_SUPPORT));
uintptr_t maybe_resolve_jump(uintptr_t);
} // namespace spud::detail::arm64

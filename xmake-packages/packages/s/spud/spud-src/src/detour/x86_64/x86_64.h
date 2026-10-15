#pragma once

#include "detour/fwd.h"

#include "detour/detour_stub.h"

#include <Zydis/Utils.h>
#include <Zydis/Zydis.h>

#include <array>
#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

namespace spud::detail::x64 {

// A decoded instruction of the prologue. collect_relocations keeps the
// PC-relative ones that land in the copy.
struct relocation_entry {
  // Offset from the hooked function.
  uintptr_t address;
  ZydisDecodedInstruction instruction;
  std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> operands;
};

std::tuple<relocation_info, size_t>
collect_relocations(uintptr_t address, size_t jump_size,
                    std::span<const uint8_t> original_prefix = {},
                    size_t function_size = 0);
// `at` is the address the trampoline will run at, or 0 for a
// position-independent build. sizing_only reserves worst-case space without
// claiming the buffer is executable; a real far form can still be refused.
// A far `jmp [rip + d]` allocates a relay near its slot, which the caller
// writes with trampoline_buffer::publish_relays() before running the code.
trampoline_buffer create_trampoline(uintptr_t at, uintptr_t return_address,
                                    std::span<const uint8_t> target,
                                    const relocation_info &relocation_infos,
                                    bool sizing_only = false,
                                    bool unwind = bool(SPUD_UNWIND_SUPPORT));
// Unknown or unreadable instructions keep a pad conservatively.
bool needs_ibt_landing(uintptr_t address,
                       std::span<const uint8_t> original_prefix = {});
uintptr_t maybe_resolve_jump(uintptr_t);

} // namespace spud::detail::x64

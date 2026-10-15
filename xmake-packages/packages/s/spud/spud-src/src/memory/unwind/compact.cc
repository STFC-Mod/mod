// Rebuilds DWARF rows from Apple compact unwind encodings by decoding the
// source function's prologue and epilogue paths.
#include "memory/unwind/dwarf.h"

#include "memory/readable.h"

#include <spud/arch.h>

#if SPUD_UNWIND_SUPPORT && SPUD_OS_APPLE
#include <Zydis/Zydis.h>
#include <dlfcn.h>
#include <mach-o/compact_unwind_encoding.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#if SPUD_AARCH64_SUPPORT
#include <capstone/capstone.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <vector>

namespace spud::detail::dwarf {

bool snapshot_compact(uintptr_t source, uint32_t encoding, frame &f) {
  Dl_info image{};
  if (dladdr(reinterpret_cast<void *>(source), &image) == 0)
    return false;
  unsigned long length = 0;
  const auto *section =
      getsectiondata(static_cast<const mach_header_64 *>(image.dli_fbase),
                     "__TEXT", "__unwind_info", &length);
  if (section == nullptr)
    return false;
  // Each table is read once and snapshotted whole, and the binary searches
  // run on the copies. The section is read-only, so a larger snapshot costs
  // a longer compare at commit rather than more reads.
  auto read = [&](size_t offset, size_t size, std::vector<uint8_t> &bytes) {
    if (offset > length || size > length - offset)
      return false;
    bytes.resize(size);
    const auto address = reinterpret_cast<uintptr_t>(section) + offset;
    if (!read_code_bytes(address, bytes))
      return false;
    f.metadata.emplace_back(address, bytes);
    return true;
  };
  auto field = [](const std::vector<uint8_t> &bytes, size_t at, auto &value) {
    if (at > bytes.size() || sizeof(value) > bytes.size() - at)
      return false;
    std::memcpy(&value, bytes.data() + at, sizeof(value));
    return true;
  };
  std::vector<uint8_t> bytes;
  unwind_info_section_header header{};
  if (!read(0, sizeof(header), bytes) || !field(bytes, 0, header) ||
      header.version != UNWIND_SECTION_VERSION || header.indexCount < 2 ||
      header.indexCount >
          length / sizeof(unwind_info_section_header_index_entry))
    return false;
  const uintptr_t image_base = reinterpret_cast<uintptr_t>(image.dli_fbase);
  const uintptr_t relative = source - image_base;
  // The last index entry for which `before(entry)` holds; entry 0 when none.
  auto search = [&](size_t count, auto before) {
    size_t low = 0, high = count - 1;
    while (low < high) {
      const size_t middle = low + (high - low + 1) / 2;
      if (before(middle))
        low = middle;
      else
        high = middle - 1;
    }
    return low;
  };
  unwind_info_section_header_index_entry index{};
  if (!read(header.indexSectionOffset, header.indexCount * sizeof(index),
            bytes))
    return false;
  const size_t slot = search(header.indexCount, [&](size_t i) {
    unwind_info_section_header_index_entry entry{};
    std::memcpy(&entry, bytes.data() + i * sizeof(entry), sizeof(entry));
    return entry.functionOffset <= relative;
  });
  std::memcpy(&index, bytes.data() + slot * sizeof(index), sizeof(index));
  if (index.functionOffset > relative ||
      index.secondLevelPagesSectionOffset == 0)
    return false;
  const size_t page = index.secondLevelPagesSectionOffset;
  unwind_info_compressed_second_level_page_header entries{};
  // Both page headers start with kind, entryPageOffset and entryCount; the
  // compressed one adds its local encodings.
  if (!read(page, std::min<size_t>(sizeof(entries), length - page), bytes) ||
      !field(bytes, 0, entries.kind) ||
      !field(bytes, 4, entries.entryPageOffset) ||
      !field(bytes, 6, entries.entryCount) || entries.entryCount == 0)
    return false;
  uint32_t actual = 0;
  if (entries.kind == UNWIND_SECOND_LEVEL_REGULAR) {
    unwind_info_regular_second_level_entry entry{};
    if (!read(page + entries.entryPageOffset,
              size_t(entries.entryCount) * sizeof(entry), bytes))
      return false;
    const size_t at = search(entries.entryCount, [&](size_t i) {
      unwind_info_regular_second_level_entry probe{};
      std::memcpy(&probe, bytes.data() + i * sizeof(probe), sizeof(probe));
      return probe.functionOffset <= relative;
    });
    std::memcpy(&entry, bytes.data() + at * sizeof(entry), sizeof(entry));
    if (entry.functionOffset > relative)
      return false;
    actual = entry.encoding;
  } else if (entries.kind == UNWIND_SECOND_LEVEL_COMPRESSED) {
    if (!field(bytes, 8, entries.encodingsPageOffset) ||
        !field(bytes, 10, entries.encodingsCount))
      return false;
    if (!read(page + entries.entryPageOffset, size_t(entries.entryCount) * 4,
              bytes))
      return false;
    auto entry_at = [&](size_t i) {
      uint32_t entry;
      std::memcpy(&entry, bytes.data() + i * 4, 4);
      return entry;
    };
    const uint32_t entry = entry_at(search(entries.entryCount, [&](size_t i) {
      return uint64_t(index.functionOffset) + (entry_at(i) & 0xFFFFFF) <=
             relative;
    }));
    if (uint64_t(index.functionOffset) + (entry & 0xFFFFFF) > relative)
      return false;
    const size_t encoding_slot = entry >> 24;
    if (encoding_slot < header.commonEncodingsArrayCount) {
      if (!read(size_t(header.commonEncodingsArraySectionOffset) +
                    encoding_slot * 4,
                4, bytes) ||
          !field(bytes, 0, actual))
        return false;
    } else {
      const size_t local = encoding_slot - header.commonEncodingsArrayCount;
      if (local >= entries.encodingsCount ||
          !read(page + entries.encodingsPageOffset + local * 4, 4, bytes) ||
          !field(bytes, 0, actual))
        return false;
    }
  } else {
    return false;
  }
  const unsigned personality = (actual >> 28) & 3;
  if (personality != 0 && (personality > header.personalityArrayCount ||
                           !read(size_t(header.personalityArraySectionOffset) +
                                     (personality - 1) * 4,
                                 4, bytes)))
    return false;
  return actual == encoding;
}

namespace {

struct compact_state {
  std::optional<int64_t> sp = 0;
  std::optional<int64_t> fp;
  std::array<std::optional<int64_t>, 64> saved{};
  std::array<bool, 64> original{};

  compact_state() {
    original.fill(true);
  }
  static bool nonvolatile(unsigned reg) {
    return kArm64 ? (reg >= 19 && reg <= 30) || (reg >= 40 && reg <= 47)
                  : reg == 3 || reg == 5 || reg >= 12;
  }
  bool operator==(const compact_state &other) const {
    if (sp != other.sp || fp != other.fp || saved != other.saved)
      return false;
    for (unsigned reg = 0; reg < (kArm64 ? 64u : 16u); ++reg)
      if (nonvolatile(reg) && !saved[reg] &&
          original[reg] != other.original[reg])
        return false;
    return true;
  }
};

unsigned compact_dwarf_register(unsigned reg) {
  constexpr unsigned kX64[] = {0, 2, 1,  3,  7,  6,  4,  5,
                               8, 9, 10, 11, 12, 13, 14, 15};
  return kArm64 ? (reg < 32 ? reg : reg + 32) : kX64[reg];
}

bool compact_expected(uint32_t encoding, frame &f, compact_state &expected) {
  const unsigned mode = encoding & 0x0F000000;
  if (kArm64) {
    // X19..X28 pairs, then D8..D15, stored downward from `slot`.
    auto save_pairs = [&](int64_t slot) {
      for (unsigned pair = 0; pair < 9; ++pair) {
        const unsigned bit = pair < 5 ? pair : pair + 3;
        const unsigned first = pair < 5 ? 19 + pair * 2 : 40 + (pair - 5) * 2;
        if ((encoding & (1u << bit)) != 0) {
          expected.saved[first] = slot;
          expected.saved[first + 1] = slot - 8;
          slot -= 16;
        }
      }
    };
    if (mode == UNWIND_ARM64_MODE_FRAMELESS) {
      const int64_t stack = int64_t((encoding >> 12) & 0xFFF) * 16;
      const unsigned pairs = std::popcount(encoding & 0xF1Fu);
      const int64_t saved = int64_t(pairs) * 16;
      if (stack < saved)
        return false;
      expected.sp = saved - stack;
      // Darwin's frameless walker starts at SP + the encoded stack size,
      // restores pairs downward, then uses the resulting address as CFA.
      // Its first saved register can therefore lie above CFA.
      save_pairs(saved);
      return true;
    }
    if (mode != UNWIND_ARM64_MODE_FRAME)
      return false;
    expected.fp = -16;
    expected.saved[29] = -16;
    expected.saved[30] = -8;
    save_pairs(-24);
    return true;
  }
  constexpr unsigned kRegisters[] = {0, 3, 12, 13, 14, 15, 5};
  if (mode == UNWIND_X86_64_MODE_RBP_FRAME) {
    expected.fp = -8;
    expected.saved[5] = -16;
    int64_t slot = -16 - int64_t((encoding >> 16) & 255) * 8;
    unsigned locations = encoding & 0x7FFF;
    for (unsigned i = 0; i < 5; ++i, slot += 8, locations >>= 3) {
      const unsigned reg = locations & 7;
      if (reg > 5)
        return false;
      if (reg != 0) {
        if (expected.saved[kRegisters[reg]])
          return false;
        expected.saved[kRegisters[reg]] = slot;
      }
    }
    return true;
  }
  if (mode != UNWIND_X86_64_MODE_STACK_IMMD &&
      mode != UNWIND_X86_64_MODE_STACK_IND)
    return false;
  uint64_t stack_size = ((encoding >> 16) & 255) * 8;
  if (mode == UNWIND_X86_64_MODE_STACK_IND) {
    const auto address = f.begin + ((encoding >> 16) & 255);
    std::vector<uint8_t> bytes(4);
    if (f.code.read(address, bytes) != bytes.size())
      return false;
    uint32_t immediate = 0;
    std::memcpy(&immediate, bytes.data(), 4);
    stack_size = uint64_t(immediate) + ((encoding >> 13) & 7) * 8;
    f.metadata.emplace_back(address, std::move(bytes));
  }
  const unsigned count = (encoding >> 10) & 7;
  if (count > 6 || stack_size < 8 + count * 8 || stack_size > INT32_MAX)
    return false;
  expected.sp = 8 - int64_t(stack_size);
  unsigned permutation = encoding & 1023;
  std::array<unsigned, 6> available{1, 2, 3, 4, 5, 6};
  unsigned remaining = 6;
  for (unsigned i = 0; i < count; ++i) {
    unsigned divisor = 1;
    for (unsigned j = 0; j < count - i - 1; ++j)
      divisor *= remaining - 1 - j;
    const unsigned digit = permutation / divisor;
    permutation %= divisor;
    if (digit >= remaining)
      return false;
    const auto reg = kRegisters[available[digit]];
    expected.saved[reg] = -16 - int64_t(count - i - 1) * 8;
    std::move(available.begin() + digit + 1, available.begin() + remaining,
              available.begin() + digit);
    --remaining;
  }
  return permutation == 0;
}

bool compact_matches(const compact_state &state,
                     const compact_state &expected) {
  if (expected.fp ? state.fp != expected.fp : state.sp != expected.sp)
    return false;
  for (unsigned reg = 0; reg < (kArm64 ? 64u : 16u); ++reg) {
    if (expected.saved[reg]) {
      if (state.saved[reg] != expected.saved[reg])
        return false;
    } else if (compact_state::nonvolatile(reg) && !state.original[reg]) {
      return false;
    }
  }
  return true;
}

} // namespace

// Compact records describe completed frames. Decode reachable instructions
// to recover the rows before and after each prologue or epilogue operation.
// Calls must agree with the compact record, and branch joins must agree with
// one another. Unrecognized writes to SP or indirect paths are refused.
bool compact_to_dwarf(uintptr_t source, size_t copied, uint32_t encoding,
                      frame &f) {
  if (source < f.begin || source - f.begin > kMetadataLimit ||
      copied > kMetadataLimit - (source - f.begin) ||
      (encoding & UNWIND_IS_NOT_FUNCTION_START) != 0)
    return false;
  const size_t length = source - f.begin + copied;
  std::vector<uint8_t> bytes(length);
  if (length == 0 || f.code.read(f.begin, bytes) != length)
    return false;
  f.metadata.emplace_back(f.begin, bytes);
  compact_state expected;
  if (!compact_expected(encoding, f, expected))
    return false;
  std::vector<std::optional<compact_state>> rows(length + 1);
  std::vector<size_t> decoded(length, 0);
  rows[0].emplace();
  std::vector<size_t> pending{0};
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  struct arm_decoder {
#if SPUD_AARCH64_SUPPORT
    csh handle = 0;
    cs_insn *instruction = nullptr;
    ~arm_decoder() {
      if (instruction != nullptr)
        cs_free(instruction, 1);
      if (handle != 0)
        cs_close(&handle);
    }
#endif
  } arm;
#if SPUD_AARCH64_SUPPORT
  if (kArm64) {
    if (cs_open(CS_ARCH_AARCH64, CS_MODE_ARM, &arm.handle) != CS_ERR_OK ||
        cs_option(arm.handle, CS_OPT_DETAIL, CS_OPT_ON) != CS_ERR_OK)
      return false;
    arm.instruction = cs_malloc(arm.handle);
    if (arm.instruction == nullptr)
      return false;
  }
#endif
  auto enqueue = [&](size_t pc, const compact_state &state) {
    if (pc > length)
      return true;
    if (pc < length && decoded[pc] != 0 && decoded[pc] != pc + 1)
      return false;
    if (rows[pc])
      return *rows[pc] == state;
    rows[pc] = state;
    if (pc < length)
      pending.push_back(pc);
    return true;
  };
  auto memory = [&](compact_state &state, unsigned reg, int64_t address,
                    bool load) {
    if (!compact_state::nonvolatile(reg))
      return;
    const int64_t offset = address - (kArm64 ? 0 : 8);
    if (load) {
      if (state.saved[reg] == offset) {
        state.saved[reg].reset();
        state.original[reg] = true;
      } else {
        state.original[reg] = false;
      }
      if (reg == (kArm64 ? 29u : 5u))
        state.fp.reset();
    } else if (state.original[reg]) {
      state.saved[reg] = offset;
    }
  };
  while (!pending.empty()) {
    const size_t pc = pending.back();
    pending.pop_back();
    compact_state state = *rows[pc];
    size_t next = pc;
    std::optional<int64_t> branch;
    bool terminal = false, call = false;
    if (kArm64) {
      if (length - pc < 4)
        return false;
      uint32_t word;
      std::memcpy(&word, bytes.data() + pc, 4);
      next += 4;
      const unsigned rd = word & 31, rn = (word >> 5) & 31;
      // SP and FP are the only bases with a known frame offset.
      auto base_of = [&](unsigned reg) {
        return reg == 31   ? state.sp
               : reg == 29 ? state.fp
                           : std::optional<int64_t>{};
      };
      // Pre- and post-indexed forms (modes 3 and 1) update their base.
      auto writeback = [&](unsigned reg, unsigned mode,
                           std::optional<int64_t> base, int64_t displacement) {
        if ((reg != 31 && reg != 29) || (mode != 1 && mode != 3))
          return true;
        if (!base)
          return false;
        (reg == 31 ? state.sp : state.fp) = *base + displacement;
        if (reg == 29)
          state.original[29] = false;
        return true;
      };
      if ((word & 0xFC000000u) == 0x94000000u) {
        call = true;
      } else if (const auto direct = decode_arm64_branch(word)) {
        branch = int64_t(pc) + direct->words * 4;
        terminal = direct->unconditional;
      } else if ((word & 0xFFFFFC1Fu) == 0xD65F0000u) {
        terminal = true;
      } else if ((word & 0xFE000000u) == 0xD6000000u) {
        const unsigned operation = (word >> 21) & 15;
        if (operation == 1)
          call = true;
        else
          return false;
      } else if ((word & 0x1F000000u) == 0x11000000u &&
                 (word & 0x80000000u) != 0 && (word & 0x20000000u) == 0) {
        const int64_t amount = int64_t((word >> 10) & 4095)
                               << (((word >> 22) & 1) * 12);
        const auto base = base_of(rn);
        const auto value =
            base ? std::optional<int64_t>{*base +
                                          ((word >> 30) & 1 ? -amount : amount)}
                 : std::nullopt;
        if (rd == 31) {
          if (!value && !state.fp)
            return false;
          state.sp = value;
        } else if (rd == 29) {
          state.fp = value;
          state.original[29] = false;
        } else {
          state.original[rd] = false;
        }
      } else if ((word & 0x3A000000u) == 0x28000000u) {
        const bool simd = ((word >> 26) & 1) != 0;
        const unsigned opc = word >> 30;
        const unsigned width = simd ? 4u << opc : opc == 2 ? 8u : 4u;
        const int64_t displacement = int64_t(int32_t(word << 10) >> 25) * width;
        const unsigned mode = (word >> 23) & 3;
        const bool load = ((word >> 22) & 1) != 0;
        const auto base = base_of(rn);
        if (!writeback(rn, mode, base, displacement))
          return false;
        if (base && (width == 8 || (simd && width == 16))) {
          const int64_t address = *base + (mode == 1 ? 0 : displacement);
          const unsigned second = (word >> 10) & 31;
          if (rd != 31 || simd)
            memory(state, rd + (simd ? 32 : 0), address, load);
          if (second != 31 || simd)
            memory(state, second + (simd ? 32 : 0), address + width, load);
        } else if (load) {
          for (unsigned reg : {rd, (word >> 10) & 31}) {
            const unsigned saved_reg = reg + (simd ? 32 : 0);
            state.original[saved_reg] = false;
            if (!simd && reg == 29)
              state.fp.reset();
          }
        }
      } else if ((word & 0x3B000000u) == 0x39000000u ||
                 (word & 0x3B200000u) == 0x38000000u) {
        const bool simd = ((word >> 26) & 1) != 0;
        const unsigned operation = (word >> 22) & 3;
        if (operation > 1)
          return false;
        const unsigned width = 1u << (word >> 30);
        const bool scaled = (word & 0x01000000u) != 0;
        const int64_t displacement = scaled
                                         ? int64_t((word >> 10) & 4095) * width
                                         : int64_t(int32_t(word << 11) >> 23);
        const unsigned mode = scaled ? 0 : (word >> 10) & 3;
        const bool load = ((word >> 22) & 1) != 0;
        const auto base = base_of(rn);
        if (!writeback(rn, mode, base, displacement))
          return false;
        if (base && width == 8 && (rd != 31 || simd))
          memory(state, rd + (simd ? 32 : 0),
                 *base + (mode == 1 ? 0 : displacement), load);
        else if (load) {
          state.original[rd + (simd ? 32 : 0)] = false;
          if (!simd && rd == 29)
            state.fp.reset();
        }
      } else {
#if SPUD_AARCH64_SUPPORT
        const uint8_t *code = bytes.data() + pc;
        size_t available = 4;
        uint64_t address = f.begin + pc;
        cs_regs reads{}, writes{};
        uint8_t read_count = 0, write_count = 0;
        if (!cs_disasm_iter(arm.handle, &code, &available, &address,
                            arm.instruction) ||
            cs_regs_access(arm.handle, arm.instruction, reads, &read_count,
                           writes, &write_count) != CS_ERR_OK)
          return false;
        for (unsigned i = 0; i < write_count; ++i) {
          const unsigned written = writes[i];
          if (written == AARCH64_REG_SP || written == AARCH64_REG_WSP) {
            if (!state.fp)
              return false;
            state.sp.reset();
          } else if (written == AARCH64_REG_FP || written == AARCH64_REG_W29) {
            state.fp.reset();
            state.original[29] = false;
          } else if (written == AARCH64_REG_LR || written == AARCH64_REG_W30) {
            state.original[30] = false;
          } else if (written >= AARCH64_REG_X19 && written <= AARCH64_REG_X28) {
            state.original[19 + written - AARCH64_REG_X19] = false;
          } else if (written >= AARCH64_REG_W19 && written <= AARCH64_REG_W28) {
            state.original[19 + written - AARCH64_REG_W19] = false;
          } else if (written >= AARCH64_REG_D8 && written <= AARCH64_REG_D15) {
            state.original[40 + written - AARCH64_REG_D8] = false;
          } else if (written >= AARCH64_REG_Q8 && written <= AARCH64_REG_Q15) {
            state.original[40 + written - AARCH64_REG_Q8] = false;
          } else if (written >= AARCH64_REG_S8 && written <= AARCH64_REG_S15) {
            state.original[40 + written - AARCH64_REG_S8] = false;
          } else if (written >= AARCH64_REG_H8 && written <= AARCH64_REG_H15) {
            state.original[40 + written - AARCH64_REG_H8] = false;
          } else if (written >= AARCH64_REG_B8 && written <= AARCH64_REG_B15) {
            state.original[40 + written - AARCH64_REG_B8] = false;
          }
        }
#else
        return false;
#endif
      }
    } else {
      ZydisDecodedInstruction instruction;
      ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
      if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, bytes.data() + pc,
                                               length - pc, &instruction,
                                               operands)))
        return false;
      next += instruction.length;
      auto reg = [&](ZydisRegister value) -> std::optional<unsigned> {
        const auto full =
            ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, value);
        if (ZydisRegisterGetClass(full) != ZYDIS_REGCLASS_GPR64)
          return std::nullopt;
        return unsigned(ZydisRegisterGetId(full));
      };
      auto address = [&](const ZydisDecodedOperand &operand) {
        if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY ||
            operand.mem.index != ZYDIS_REGISTER_NONE)
          return std::optional<int64_t>{};
        const auto base = operand.mem.base == ZYDIS_REGISTER_RSP ? state.sp
                          : operand.mem.base == ZYDIS_REGISTER_RBP
                              ? state.fp
                              : std::nullopt;
        return base ? std::optional<int64_t>{*base + operand.mem.disp.value}
                    : std::nullopt;
      };
      call = instruction.meta.category == ZYDIS_CATEGORY_CALL;
      terminal = instruction.meta.category == ZYDIS_CATEGORY_RET ||
                 instruction.meta.category == ZYDIS_CATEGORY_UNCOND_BR;
      if (instruction.meta.category == ZYDIS_CATEGORY_COND_BR ||
          instruction.meta.category == ZYDIS_CATEGORY_UNCOND_BR) {
        for (unsigned i = 0; i < instruction.operand_count_visible; ++i)
          if (operands[i].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
              operands[i].imm.is_relative)
            branch = int64_t(next) + operands[i].imm.value.s;
        if (!branch)
          return false;
      }
      const auto first = instruction.operand_count_visible != 0 &&
                                 operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER
                             ? reg(operands[0].reg.value)
                             : std::nullopt;
      if (instruction.mnemonic == ZYDIS_MNEMONIC_PUSH) {
        if (!state.sp || instruction.operand_width != 64 ||
            (first && *first == 4))
          return false;
        state.sp = *state.sp - 8;
        if (first)
          memory(state, *first, *state.sp, false);
      } else if (instruction.mnemonic == ZYDIS_MNEMONIC_POP) {
        if (!state.sp || !first || *first == 4 || operands[0].size != 64)
          return false;
        memory(state, *first, *state.sp, true);
        state.sp = *state.sp + 8;
      } else if (instruction.mnemonic == ZYDIS_MNEMONIC_LEAVE) {
        if (!state.fp)
          return false;
        state.sp = *state.fp + 8;
        memory(state, 5, *state.fp, true);
      } else if ((instruction.mnemonic == ZYDIS_MNEMONIC_ADD ||
                  instruction.mnemonic == ZYDIS_MNEMONIC_SUB) &&
                 first == 4 &&
                 operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE &&
                 operands[0].size == 64) {
        if (state.sp)
          state.sp = *state.sp + (instruction.mnemonic == ZYDIS_MNEMONIC_SUB
                                      ? -operands[1].imm.value.s
                                      : operands[1].imm.value.s);
      } else if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV && first == 5 &&
                 operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                 operands[1].reg.value == ZYDIS_REGISTER_RSP &&
                 operands[0].size == 64) {
        state.fp = state.sp;
        state.original[5] = false;
      } else if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV && first == 4 &&
                 operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                 operands[1].reg.value == ZYDIS_REGISTER_RBP &&
                 operands[0].size == 64) {
        if (!state.fp)
          return false;
        state.sp = state.fp;
      } else if (instruction.mnemonic == ZYDIS_MNEMONIC_LEA && first == 4 &&
                 operands[0].size == 64) {
        state.sp = address(operands[1]);
        if (!state.sp && !state.fp)
          return false;
      } else if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV &&
                 operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                 operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                 operands[0].size == 64) {
        const auto location = address(operands[0]);
        const auto source_reg = reg(operands[1].reg.value);
        if (location && source_reg && *source_reg != 4)
          memory(state, *source_reg, *location, false);
      } else if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV && first &&
                 operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                 operands[0].size == 64) {
        const auto location = address(operands[1]);
        if (*first == 4)
          return false;
        if (location)
          memory(state, *first, *location, true);
        else
          state.original[*first] = false;
      } else if (!call && !terminal) {
        for (unsigned i = 0; i < instruction.operand_count; ++i) {
          if (operands[i].type != ZYDIS_OPERAND_TYPE_REGISTER ||
              (operands[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) == 0)
            continue;
          const auto written = reg(operands[i].reg.value);
          if (written == 4) {
            if (!state.fp)
              return false;
            state.sp.reset();
          } else if (written) {
            state.original[*written] = false;
            if (*written == 5)
              state.fp.reset();
          }
        }
      }
    }
    for (size_t byte = pc; byte < next; ++byte) {
      if (decoded[byte] != 0 && decoded[byte] != pc + 1)
        return false;
      decoded[byte] = pc + 1;
    }
    if (call && !compact_matches(*rows[pc], expected))
      return false;
    if (call && kArm64)
      state.original[30] = false;
    if (branch && *branch >= 0 && size_t(*branch) <= length &&
        !enqueue(size_t(*branch), state))
      return false;
    if (!terminal && !enqueue(next, state))
      return false;
  }
  if (!rows[source - f.begin])
    return false;
  f.initial = entry_rules();
  writer instructions;
  size_t previous = 0;
  const compact_state *previous_state = nullptr;
  for (size_t pc = 0; pc <= length; ++pc) {
    if (!rows[pc])
      continue;
    const auto &state = *rows[pc];
    for (unsigned reg = 0; reg < (kArm64 ? 64u : 16u); ++reg)
      if (compact_state::nonvolatile(reg) && !state.saved[reg] &&
          !state.original[reg])
        return false;
    if (previous_state != nullptr && state.fp == previous_state->fp &&
        (state.fp || state.sp == previous_state->sp) &&
        state.saved == previous_state->saved)
      continue;
    previous_state = &state;
    if (!state.fp && !state.sp)
      return false;
    instructions.fixed(4, 1);
    instructions.fixed(pc - previous, 4);
    previous = pc;
    instructions.fixed(12, 1);
    instructions.uleb(state.fp ? (kArm64 ? 29 : 6) : (kArm64 ? 31 : 7));
    const int64_t cfa = (kArm64 ? 0 : 8) - (state.fp ? *state.fp : *state.sp);
    if (cfa < 0)
      return false;
    instructions.uleb(cfa);
    for (unsigned reg = 0; reg < (kArm64 ? 64u : 16u); ++reg) {
      if (!compact_state::nonvolatile(reg))
        continue;
      if (state.saved[reg]) {
        if (*state.saved[reg] % 8 != 0)
          return false;
        const int64_t offset = *state.saved[reg];
        instructions.fixed(offset > 0 ? 17 : 5, 1);
        instructions.uleb(compact_dwarf_register(reg));
        if (offset > 0)
          instructions.sleb(-offset / 8);
        else
          instructions.uleb(-offset / 8);
      } else {
        instructions.fixed(8, 1);
        instructions.uleb(compact_dwarf_register(reg));
      }
    }
  }
  f.instructions = std::move(instructions.bytes);
  return true;
}

bool copied_calls(const frame &f, uintptr_t source, size_t copied,
                  bool &calls) {
  calls = false;
  std::vector<uint8_t> code(copied);
  code.resize(f.code.read(source, code));
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  for (size_t offset = 0; offset < copied;) {
    const size_t available =
        offset < code.size() ? code.size() - offset : size_t{0};
    if (kArm64) {
      if (available < 4)
        return false;
      uint32_t word;
      std::memcpy(&word, code.data() + offset, 4);
      const auto register_opcode = (word >> 21) & 15u;
      calls |= (word & 0xFC000000u) == 0x94000000u ||
               ((word & 0xFE000000u) == 0xD6000000u &&
                (register_opcode == 1 || register_opcode == 9));
      offset += 4;
    } else {
      ZydisDecodedInstruction instruction;
      if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(
              &decoder, nullptr, code.data() + offset,
              std::min<size_t>(available, 15), &instruction)))
        return false;
      calls |= instruction.meta.category == ZYDIS_CATEGORY_CALL;
      offset += instruction.length;
    }
  }
  return true;
}

} // namespace spud::detail::dwarf
#endif

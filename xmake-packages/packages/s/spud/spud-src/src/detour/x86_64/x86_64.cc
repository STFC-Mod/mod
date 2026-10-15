// Owns the x86-64 jump stub and the relocation of an overwritten prologue
// into a trampoline.
#include "x86_64.h"

#include "detour/chain.h"
#include "detour/detour_impl.h"
#include "memory/executable_pool.h"
#include "memory/readable.h"

#include "memory/safe_patch.h"
#include <spud/detour.h>
#include <spud/memory.h>

#include <Zydis/Encoder.h>
#include <Zydis/Utils.h>
#include <Zydis/Zydis.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

namespace spud {
namespace detail {
namespace x64 {

namespace {

constexpr unsigned kDwarfRegisters[] = {0, 2, 1,  3,  7,  6,  4,  5,
                                        8, 9, 10, 11, 12, 13, 14, 15};

// What a relative instruction is, by opcode rather than mnemonic so new
// mnemonics fall into `unsupported` instead of being copied.
enum class rel_form : uint8_t {
  none,
  jmp,    // jmp rel8/rel32
  call,   // call rel32
  jcc,    // jcc rel8/rel32
  loop,   // loop, loope, loopne, jrcxz, jecxz: rel8 only
  xbegin, // the relative abort path of an RTM transaction
  memory, // a [rip + disp32] operand
  unsupported,
};

// Index of the RIP/EIP-relative operand, or -1.
int rip_operand(const relocation_entry &e) {
  for (int i = 0; i < e.instruction.operand_count_visible; ++i) {
    if (e.operands[i].type == ZYDIS_OPERAND_TYPE_MEMORY &&
        (e.operands[i].mem.base == ZYDIS_REGISTER_RIP ||
         e.operands[i].mem.base == ZYDIS_REGISTER_EIP)) {
      return i;
    }
  }
  return -1;
}

// Jcc rel8/rel32. LOOP and JRCXZ share its Zydis category.
bool is_jcc(const ZydisDecodedInstruction &insn) {
  return (insn.opcode_map == ZYDIS_OPCODE_MAP_DEFAULT &&
          (insn.opcode & 0xF0) == 0x70) ||
         (insn.opcode_map == ZYDIS_OPCODE_MAP_0F &&
          (insn.opcode & 0xF0) == 0x80);
}

rel_form classify(const relocation_entry &e) {
  const auto &insn = e.instruction;
  if ((insn.attributes & ZYDIS_ATTRIB_IS_RELATIVE) == 0) {
    return rel_form::none;
  }
  if (insn.raw.disp.size == 32 && rip_operand(e) >= 0) {
    return rel_form::memory;
  }
  if (insn.raw.imm[0].size == 0) {
    return rel_form::unsupported;
  }
  const bool map0 = insn.opcode_map == ZYDIS_OPCODE_MAP_DEFAULT;
  if (map0 && (insn.opcode == 0xE9 || insn.opcode == 0xEB)) {
    return rel_form::jmp;
  }
  if (map0 && insn.opcode == 0xE8) {
    return rel_form::call;
  }
  if (is_jcc(insn)) {
    return rel_form::jcc;
  }
  if (map0 && insn.opcode >= 0xE0 && insn.opcode <= 0xE3) {
    return rel_form::loop;
  }
  if (map0 && insn.opcode == 0xC7 && insn.raw.modrm.mod == 3 &&
      insn.raw.modrm.reg == 7 && insn.raw.modrm.rm == 0) {
    return rel_form::xbegin;
  }
  return rel_form::unsupported;
}

// Absolute address a relative operand references when the instruction runs
// at `base + e.address`.
std::optional<uintptr_t> operand_target(const relocation_entry &e, int index,
                                        uintptr_t base) {
  ZyanU64 result = 0;
  if (index < 0 ||
      !ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&e.instruction, &e.operands[index],
                                             base + e.address, &result))) {
    return std::nullopt;
  }
  return static_cast<uintptr_t>(result);
}

// Branch destination as an offset from the decoded function.
std::optional<intptr_t> branch_offset(const ZydisDecodedInstruction &insn,
                                      size_t offset) {
  if (!insn.raw.imm[0].is_relative) {
    return std::nullopt;
  }
  return static_cast<intptr_t>(offset + insn.length) + insn.raw.imm[0].value.s;
}

bool is_register_load(const relocation_entry &e) {
  return e.instruction.mnemonic == ZYDIS_MNEMONIC_MOV &&
         e.instruction.operand_count_visible == 2 &&
         e.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
         e.operands[0].size == 64 && rip_operand(e) == 1;
}

// UD0-2 are the traps of inline checks (trapping UBSan, Zig safety, Rust
// and __builtin_trap), often placed after the return. INT3 is left out:
// a debugger breakpoint resumes at the next instruction.
bool ends_flow(const ZydisDecodedInstruction &instruction) {
  switch (instruction.mnemonic) {
  case ZYDIS_MNEMONIC_RET:
  case ZYDIS_MNEMONIC_JMP:
  case ZYDIS_MNEMONIC_UD0:
  case ZYDIS_MNEMONIC_UD1:
  case ZYDIS_MNEMONIC_UD2:
    return true;
  default:
    return false;
  }
}

// These store the x87 FPU instruction pointer (FIP): the address of the last
// x87 instruction that ran. SYSCALL is not here. It leaves the return PC in
// RCX and R11, which the ABI clobbers and compiled code does not read.
bool observes_pc(ZydisMnemonic mnemonic) {
  switch (mnemonic) {
  case ZYDIS_MNEMONIC_FNSTENV:
  case ZYDIS_MNEMONIC_FNSAVE:
  case ZYDIS_MNEMONIC_FXSAVE:
  case ZYDIS_MNEMONIC_FXSAVE64:
  case ZYDIS_MNEMONIC_XSAVE:
  case ZYDIS_MNEMONIC_XSAVE64:
  case ZYDIS_MNEMONIC_XSAVEC:
  case ZYDIS_MNEMONIC_XSAVEC64:
  case ZYDIS_MNEMONIC_XSAVEOPT:
  case ZYDIS_MNEMONIC_XSAVEOPT64:
  case ZYDIS_MNEMONIC_XSAVES:
  case ZYDIS_MNEMONIC_XSAVES64:
    return true;
  default:
    return false;
  }
}

// Whether running this instruction from the trampoline leaves a trampoline
// address in FIP.
bool sets_fip(const ZydisDecodedInstruction &insn) {
  return (insn.meta.isa_ext == ZYDIS_ISA_EXT_X87 ||
          insn.meta.isa_set == ZYDIS_ISA_SET_SSE3X87) &&
         !observes_pc(insn.mnemonic);
}

bool is_data_read(const relocation_entry &e) {
  const auto mnemonic = e.instruction.mnemonic;
  return mnemonic != ZYDIS_MNEMONIC_CLFLUSH &&
         mnemonic != ZYDIS_MNEMONIC_CLFLUSHOPT &&
         mnemonic != ZYDIS_MNEMONIC_CLWB && mnemonic != ZYDIS_MNEMONIC_NOP;
}

bool segmented_memory(const ZydisDecodedOperand &op) {
  return op.mem.segment == ZYDIS_REGISTER_FS ||
         op.mem.segment == ZYDIS_REGISTER_GS;
}

bool known_memory_extent(const relocation_entry &e, int m) {
  const auto &op = e.operands[m];
  const auto mnemonic = e.instruction.mnemonic;
  if (op.size == 0 || mnemonic == ZYDIS_MNEMONIC_XRSTOR ||
      mnemonic == ZYDIS_MNEMONIC_XRSTOR64 ||
      mnemonic == ZYDIS_MNEMONIC_XRSTORS ||
      mnemonic == ZYDIS_MNEMONIC_XRSTORS64 || observes_pc(mnemonic)) {
    return false;
  }
  return !((mnemonic == ZYDIS_MNEMONIC_BT || mnemonic == ZYDIS_MNEMONIC_BTS ||
            mnemonic == ZYDIS_MNEMONIC_BTR || mnemonic == ZYDIS_MNEMONIC_BTC) &&
           e.operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER);
}

size_t padding_size(const ZydisDecoder &decoder, uintptr_t address,
                    size_t available, size_t required) {
  size_t size = 0;
  while (size < required) {
    ZydisDecodedInstruction insn{};
    if (ZYAN_FAILED(ZydisDecoderDecodeInstruction(
            &decoder, nullptr, reinterpret_cast<const void *>(address + size),
            available - size, &insn)) ||
        (insn.mnemonic != ZYDIS_MNEMONIC_NOP &&
         insn.mnemonic != ZYDIS_MNEMONIC_INT3)) {
      return 0;
    }
    size += insn.length;
  }
  return size;
}

// Whether the bytes after a call cannot be its fallthrough. Clang places
// noreturn calls (__stack_chk_fail, _Unwind_Resume) after the final ret, so
// a function can end in a call. A trap after the call proves it; otherwise
// require padding to a 16-byte boundary followed by a function entry, since
// loop headers are padded the same way but do not start with endbr64 or push.
bool ends_in_noreturn_call(const ZydisDecoder &decoder, const uint8_t *bytes,
                           size_t available, uintptr_t pc) {
  size_t size = 0;
  ZydisDecodedInstruction insn{};
  while (size < available &&
         ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(
             &decoder, nullptr, bytes + size, available - size, &insn))) {
    if (insn.mnemonic == ZYDIS_MNEMONIC_INT3 ||
        insn.mnemonic == ZYDIS_MNEMONIC_UD2)
      return true;
    if (insn.mnemonic != ZYDIS_MNEMONIC_NOP)
      break;
    size += insn.length;
  }
  if (size == 0 || size >= available || (pc + size) % 16 != 0)
    return false;
  return insn.mnemonic == ZYDIS_MNEMONIC_ENDBR64 ||
         (insn.mnemonic == ZYDIS_MNEMONIC_PUSH &&
          insn.opcode_map == ZYDIS_OPCODE_MAP_DEFAULT &&
          (insn.opcode & 0xF8) == 0x50);
}

bool uses_register(const relocation_entry &e, ZydisRegister reg64) {
  auto same = [&](ZydisRegister r) {
    return r != ZYDIS_REGISTER_NONE &&
           ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, r) ==
               reg64;
  };
  for (int i = 0; i < e.instruction.operand_count; ++i) {
    const auto &op = e.operands[i];
    if ((op.type == ZYDIS_OPERAND_TYPE_REGISTER && same(op.reg.value)) ||
        (op.type == ZYDIS_OPERAND_TYPE_MEMORY &&
         (same(op.mem.base) || same(op.mem.index)))) {
      return true;
    }
  }
  return false;
}

// The general-purpose register `reg` belongs to, 0-15, or -1. Vector,
// flags and RIP registers have ids too, which must not alias RAX..R15.
int register_id(ZydisRegister reg) {
  const auto enclosing =
      ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, reg);
  if (reg == ZYDIS_REGISTER_NONE ||
      ZydisRegisterGetClass(enclosing) != ZYDIS_REGCLASS_GPR64)
    return -1;
  return ZydisRegisterGetId(enclosing);
}

bool writes_flags(const ZydisDecodedInstruction &insn) {
  return insn.cpu_flags != nullptr &&
         (insn.cpu_flags->modified | insn.cpu_flags->set_0 |
          insn.cpu_flags->set_1 | insn.cpu_flags->undefined) != 0;
}

bool ends_block(const ZydisDecodedInstruction &insn) {
  const auto category = insn.meta.category;
  return category == ZYDIS_CATEGORY_UNCOND_BR ||
         category == ZYDIS_CATEGORY_RET || category == ZYDIS_CATEGORY_CALL;
}

// Condition codes of the unsigned bound checks: JA and JAE leave the
// dispatch for the default, JBE and JB enter it.
constexpr unsigned kBelow = 2;
constexpr unsigned kAboveOrEqual = 3;
constexpr unsigned kBelowOrEqual = 6;
constexpr unsigned kAbove = 7;

// A switch dispatch through an indexed table: the absolute case targets, or
// none when the shape matched but the table could not be bounded or read.
struct jump_table {
  bool matched = false;
  std::vector<uintptr_t> targets;
  // Offset of the first instruction the match evaluated. A branch landing
  // after it and up to the JMP joins with registers the match did not see,
  // except at `entry`, where another bound check of the same register may
  // branch in.
  size_t block = 0;
  size_t jcc = 0;
  size_t entry = 0;
  ZydisRegister bound = ZYDIS_REGISTER_NONE;
  uint64_t count = 0;
  uintptr_t table = 0;
  size_t table_size = 0;
};

// Matches the dispatch blocks GCC, Clang and MSVC emit for a dense switch:
//   cmp idx, N; ja default            (jae: N entries, ja: N + 1)
//   lea base, [rip + T]               (MSVC: the image base)
//   movsxd/mov r, [base + idx*4 (+ rva)]; add r, base; jmp r
// or an absolute table through `jmp [T + idx*8]`, `mov r, [base + idx*8]`.
// Instructions that leave the flags alone may sit between the CMP and the
// Jcc, and the dispatch may instead be the target of `jbe` or `jb` after
// the default code. Registers are evaluated forward from the CMP, or when
// that does not give the table, from the start of the straight-line run
// holding it, where the base is often formed. A base hoisted out of a loop
// is matched but unread, and the caller refuses it like any other local
// dispatch it cannot enumerate.
class jump_table_reader {
public:
  // `offsets` are the scanned instructions, ending with the indirect JMP.
  jump_table_reader(const ZydisDecoder &decoder, std::span<const uint8_t> bytes,
                    uintptr_t address, std::span<const size_t> offsets)
      : decoder_(decoder), bytes_(bytes), address_(address), offsets_(offsets),
        jmp_(offsets.size() - 1) {}

  jump_table read() {
    find_branch();
    find_check();
    if (!cmp_at_)
      return table_from(falls_in() ? entry_ : first_);
    auto result = table_from(*cmp_at_);
    if (!result.targets.empty())
      return result;
    size_t start = *cmp_at_;
    while (start > 0 && *cmp_at_ - start < kMaxBlock) {
      relocation_entry e;
      if (!decode(start - 1, e) || ends_block(e.instruction))
        break;
      --start;
    }
    // Padding before a loop header is not part of the run: a back edge to
    // the header enters at its first instruction.
    for (relocation_entry e; start < *cmp_at_ && decode(start, e) &&
                             e.instruction.mnemonic == ZYDIS_MNEMONIC_NOP;)
      ++start;
    if (start != *cmp_at_) {
      if (auto wider = table_from(start); !wider.targets.empty())
        return wider;
    }
    return result;
  }

private:
  // Compiled dispatch blocks are a handful of instructions, but spills and
  // argument moves get scheduled into them. Each walk is bounded by this
  // many instructions.
  static constexpr size_t kMaxBlock = 16;
  // 4096 cases covers byte-code interpreters; larger tables are refused.
  static constexpr uint64_t kMaxEntries = 4096;

  // What the evaluation knows about a register.
  struct value {
    enum class kind : uint8_t { unknown, constant, entry } k = kind::unknown;
    uintptr_t constant = 0; // the address, or the addend of an entry
    // An entry loaded from [table + index * width].
    std::optional<uintptr_t> table;
    // The index is the bound-checked register or a copy of it.
    bool bounded = false;
    uint8_t width = 0;
    bool sign = false;
  };
  struct registers {
    std::array<value, 16> values{};
    // Registers holding the bounded index.
    uint16_t aliases = 0;
  };

  static value constant(uintptr_t address) {
    value v;
    v.k = value::kind::constant;
    v.constant = address;
    return v;
  }

  bool decode(size_t i, relocation_entry &e) const {
    e.address = offsets_[i];
    return ZYAN_SUCCESS(ZydisDecoderDecodeFull(
        &decoder_, bytes_.data() + offsets_[i], bytes_.size() - offsets_[i],
        &e.instruction, e.operands.data()));
  }

  bool falls_in() const {
    return jcc_ && entry_ == *jcc_ + 1;
  }

  // Walks back to the conditional branch that bounds the index. Without one
  // in the run before the JMP, looks for a JBE or JB into the run's start,
  // past NOP padding.
  void find_branch() {
    first_ = jmp_;
    entry_ = jmp_;
    while (first_ > 0 && jmp_ - first_ < kMaxBlock) {
      relocation_entry e;
      if (!decode(first_ - 1, e) || ends_block(e.instruction))
        break;
      if (e.instruction.meta.category == ZYDIS_CATEGORY_COND_BR) {
        jcc_ = first_ - 1;
        entry_ = first_;
        break;
      }
      --first_;
    }
    relocation_entry branch;
    if (jcc_) {
      if (decode(*jcc_, branch) && is_jcc(branch.instruction))
        cc_ = branch.instruction.opcode & 0x0F;
      return;
    }
    entry_ = first_;
    for (relocation_entry e; entry_ < jmp_ && decode(entry_, e) &&
                             e.instruction.mnemonic == ZYDIS_MNEMONIC_NOP;)
      ++entry_;
    for (size_t i = first_; i > 0 && first_ - i < kMaxBlock; --i) {
      if (decode(i - 1, branch) && is_jcc(branch.instruction) &&
          branch_offset(branch.instruction, offsets_[i - 1]) ==
              static_cast<intptr_t>(offsets_[entry_])) {
        jcc_ = i - 1;
        cc_ = branch.instruction.opcode & 0x0F;
        return;
      }
    }
  }

  // Walks back from the Jcc to the instruction whose flags it tests, and
  // takes the bound from it when it compares a register or a memory operand
  // with an immediate.
  void find_check() {
    if (!jcc_ || !(falls_in() ? cc_ == kAbove || cc_ == kAboveOrEqual
                              : cc_ == kBelowOrEqual || cc_ == kBelow))
      return;
    bool stores = false;
    for (size_t i = *jcc_; i > 0 && *jcc_ - i < kMaxBlock; --i) {
      relocation_entry cmp;
      if (!decode(i - 1, cmp) || ends_block(cmp.instruction))
        return;
      // Another Jcc on the same flags only leaves earlier, as in
      // `cmp; jg; ja`.
      if (!writes_flags(cmp.instruction)) {
        for (unsigned o = 0; o < cmp.instruction.operand_count; ++o)
          stores |=
              cmp.operands[o].type == ZYDIS_OPERAND_TYPE_MEMORY &&
              (cmp.operands[o].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0;
        continue;
      }
      const auto &lhs = cmp.operands[0];
      const auto &rhs = cmp.operands[1];
      if (cmp.instruction.mnemonic != ZYDIS_MNEMONIC_CMP ||
          rhs.type != ZYDIS_OPERAND_TYPE_IMMEDIATE || rhs.imm.value.s < 0 ||
          rhs.imm.value.u >= kMaxEntries)
        return;
      if (lhs.type == ZYDIS_OPERAND_TYPE_REGISTER) {
        bound_ = register_id(lhs.reg.value);
        bound_reg_ = lhs.reg.value;
      } else if (lhs.type == ZYDIS_OPERAND_TYPE_MEMORY && !stores &&
                 falls_in()) {
        // A store before the branch may change a bound held in memory.
        bound_memory_ = lhs;
      }
      if (bound_ >= 0 || bound_memory_) {
        cmp_at_ = i - 1;
        count_ = rhs.imm.value.u + (cc_ == kAbove || cc_ == kBelowOrEqual);
      }
      return;
    }
  }

  // An entry loaded from `op` as [base + index * width].
  value load(const registers &regs, const ZydisDecodedOperand &op,
             uint8_t width, bool sign) const {
    value v;
    const int base = register_id(op.mem.base);
    const int index = register_id(op.mem.index);
    if (op.type != ZYDIS_OPERAND_TYPE_MEMORY || index < 0 || base == 4 ||
        op.mem.scale != width || op.mem.segment == ZYDIS_REGISTER_FS ||
        op.mem.segment == ZYDIS_REGISTER_GS)
      return v;
    v.k = value::kind::entry;
    v.bounded = (regs.aliases & (1u << index)) != 0;
    v.width = width;
    v.sign = sign;
    if (op.mem.base == ZYDIS_REGISTER_NONE)
      v.table = static_cast<uintptr_t>(op.mem.disp.value);
    else if (base >= 0 && regs.values[base].k == value::kind::constant)
      v.table = regs.values[base].constant + op.mem.disp.value;
    return v;
  }

  // Applies the instruction at index `i` to `regs`.
  void transfer(registers &regs, size_t i, const relocation_entry &e) const {
    const auto &insn = e.instruction;
    const auto &dst = e.operands[0];
    const auto &src = e.operands[1];
    const int d = dst.type == ZYDIS_OPERAND_TYPE_REGISTER
                      ? register_id(dst.reg.value)
                      : -1;
    std::optional<value> next;
    bool alias = false;
    if (i == entry_ && d >= 0 && bound_memory_ &&
        (insn.mnemonic == ZYDIS_MNEMONIC_MOVZX ||
         insn.mnemonic == ZYDIS_MNEMONIC_MOV) &&
        src.type == ZYDIS_OPERAND_TYPE_MEMORY &&
        src.size == bound_memory_->size &&
        src.mem.segment == bound_memory_->mem.segment &&
        src.mem.base == bound_memory_->mem.base &&
        src.mem.index == bound_memory_->mem.index &&
        src.mem.scale == bound_memory_->mem.scale &&
        src.mem.disp.value == bound_memory_->mem.disp.value) {
      alias = true;
    } else if (d >= 0 && insn.mnemonic == ZYDIS_MNEMONIC_LEA &&
               dst.size == 64) {
      if (const auto abs = operand_target(e, rip_operand(e), address_)) {
        next = constant(*abs);
      } else if (const int b = register_id(src.mem.base);
                 b >= 0 && src.mem.index == ZYDIS_REGISTER_NONE &&
                 regs.values[b].k == value::kind::constant) {
        next = constant(regs.values[b].constant +
                        static_cast<uintptr_t>(src.mem.disp.value));
      }
    } else if (d >= 0 && insn.mnemonic == ZYDIS_MNEMONIC_MOV &&
               src.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
      next = constant(dst.size == 64 ? src.imm.value.u
                                     : static_cast<uint32_t>(src.imm.value.u));
    } else if (d >= 0 &&
               (insn.mnemonic == ZYDIS_MNEMONIC_MOV ||
                insn.mnemonic == ZYDIS_MNEMONIC_MOVSXD ||
                insn.mnemonic == ZYDIS_MNEMONIC_MOVZX) &&
               src.type == ZYDIS_OPERAND_TYPE_REGISTER) {
      // Moving the bounded index, often to extend it.
      const int s = register_id(src.reg.value);
      alias = s >= 0 && (regs.aliases & (1u << s)) != 0;
      if (s >= 0 && insn.mnemonic == ZYDIS_MNEMONIC_MOV && dst.size == 64)
        next = regs.values[s];
    } else if (d >= 0 && dst.size >= 32 &&
               (insn.mnemonic == ZYDIS_MNEMONIC_MOV ||
                insn.mnemonic == ZYDIS_MNEMONIC_MOVSXD)) {
      const bool sign = insn.mnemonic == ZYDIS_MNEMONIC_MOVSXD;
      if (src.size == 32 && (sign || dst.size == 32))
        next = load(regs, src, 4, sign);
      else if (src.size == 64 && dst.size == 64)
        next = load(regs, src, 8, false);
    } else if (d >= 0 && insn.mnemonic == ZYDIS_MNEMONIC_ADD &&
               dst.size == 64 && src.type == ZYDIS_OPERAND_TYPE_REGISTER) {
      const int s = register_id(src.reg.value);
      value a = regs.values[d], b = s >= 0 ? regs.values[s] : value{};
      if (a.k == value::kind::constant && b.k == value::kind::entry)
        std::swap(a, b);
      if (b.k == value::kind::constant && a.k != value::kind::unknown) {
        a.constant += b.constant;
        next = a;
      } else if (a.k == value::kind::entry || b.k == value::kind::entry) {
        // Still a table entry, relative to a base this block did not form.
        if (a.k != value::kind::entry)
          std::swap(a, b);
        a.table.reset();
        next = a;
      }
    }
    // Everything else, including the forms above that did not resolve,
    // clobbers what it writes.
    for (unsigned o = 0; o < insn.operand_count; ++o) {
      const auto &op = e.operands[o];
      if (op.type != ZYDIS_OPERAND_TYPE_REGISTER ||
          (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) == 0)
        continue;
      if (const int w = register_id(op.reg.value); w >= 0) {
        regs.values[w] = {};
        regs.aliases &= ~(1u << w);
      }
    }
    if (d >= 0) {
      if (next)
        regs.values[d] = *next;
      if (alias)
        regs.aliases |= 1u << d;
    }
  }

  // Evaluates the registers along the path from `start` to the JMP, which
  // jumps from the Jcc to the entry when the dispatch is its target, and
  // returns what the JMP goes through.
  value evaluate(size_t start) const {
    registers regs;
    for (size_t i = start; i < jmp_; ++i) {
      // The bound holds from the CMP on; the CMP and Jcc write no register.
      if (i == cmp_at_) {
        if (bound_ >= 0)
          regs.aliases |= 1u << bound_;
        continue;
      }
      if (i == jcc_) {
        i = entry_ - 1;
        continue;
      }
      relocation_entry e;
      if (!decode(i, e))
        return {};
      transfer(regs, i, e);
    }
    relocation_entry jump;
    if (!decode(jmp_, jump))
      return {};
    const auto &op = jump.operands[0];
    if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) {
      if (const int r = register_id(op.reg.value); r >= 0)
        return regs.values[r];
      return {};
    }
    return op.size == 64 ? load(regs, op, 8, false) : value{};
  }

  // The dispatch evaluated from `start`, with its table read.
  jump_table table_from(size_t start) const {
    jump_table result;
    result.block = offsets_[start];
    if (jcc_) {
      result.jcc = offsets_[*jcc_];
      result.entry = offsets_[entry_];
    }
    result.bound = bound_reg_;
    const value v = evaluate(start);
    if (v.k != value::kind::entry)
      return result;
    result.matched = true;
    if (!v.table || !v.bounded || !count_ || *count_ == 0)
      return result;
    result.count = *count_;
    std::vector<uint8_t> table(*count_ * v.width);
    const uintptr_t begin = *v.table;
    result.table = begin;
    result.table_size = table.size();
    // A table placed after the code (MSVC) is read from the snapshot.
    if (begin >= address_ && begin - address_ <= bytes_.size() &&
        table.size() <= bytes_.size() - (begin - address_)) {
      std::memcpy(table.data(), bytes_.data() + (begin - address_),
                  table.size());
    } else if (!read_code_bytes(begin, table)) {
      return result;
    }
    for (size_t i = 0; i < *count_; ++i) {
      uint64_t slot = 0;
      std::memcpy(&slot, table.data() + i * v.width, v.width);
      if (v.width == 4)
        slot = v.sign ? static_cast<uint64_t>(static_cast<int32_t>(slot))
                      : static_cast<uint32_t>(slot);
      result.targets.push_back(static_cast<uintptr_t>(slot + v.constant));
    }
    return result;
  }

  const ZydisDecoder &decoder_;
  std::span<const uint8_t> bytes_;
  uintptr_t address_;
  std::span<const size_t> offsets_;
  // Indexes into `offsets_`: the JMP, the start of the run before it, the
  // bounding Jcc and the first instruction of the bounded path.
  size_t jmp_;
  size_t first_ = 0;
  std::optional<size_t> jcc_;
  size_t entry_ = 0;
  unsigned cc_ = 0;
  std::optional<size_t> cmp_at_;
  std::optional<uint64_t> count_;
  int bound_ = -1;
  ZydisRegister bound_reg_ = ZYDIS_REGISTER_NONE;
  // GCC also bounds a byte in memory, then loads it as the index.
  std::optional<ZydisDecodedOperand> bound_memory_;
};

// Whether the branch at `from` into a dispatch's entry bounds the same
// register as the dispatch's own check, at most as loosely: GCC merges the
// dispatch of a switch reached from several bound checks.
bool bounds_index(const ZydisDecoder &decoder, std::span<const uint8_t> bytes,
                  std::span<const size_t> offsets, size_t from,
                  ZydisRegister bound, uint64_t count) {
  const auto at = std::lower_bound(offsets.begin(), offsets.end(), from);
  if (bound == ZYDIS_REGISTER_NONE || at == offsets.begin() ||
      at == offsets.end() || *at != from)
    return false;
  relocation_entry branch, cmp;
  auto decode = [&](size_t off, relocation_entry &e) {
    return ZYAN_SUCCESS(
        ZydisDecoderDecodeFull(&decoder, bytes.data() + off, bytes.size() - off,
                               &e.instruction, e.operands.data()));
  };
  if (!decode(from, branch) || !is_jcc(branch.instruction) ||
      !decode(*std::prev(at), cmp))
    return false;
  const unsigned cc = branch.instruction.opcode & 0x0F;
  const auto &lhs = cmp.operands[0];
  const auto &rhs = cmp.operands[1];
  return (cc == kBelowOrEqual || cc == kBelow) &&
         cmp.instruction.mnemonic == ZYDIS_MNEMONIC_CMP &&
         lhs.type == ZYDIS_OPERAND_TYPE_REGISTER && lhs.reg.value == bound &&
         rhs.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && rhs.imm.value.s >= 0 &&
         rhs.imm.value.u + (cc == kBelowOrEqual) <= count;
}

// One pass over the target's code that decides how much of it to copy. It
// owns what the scan learns on the way: branch destinations, registers that
// hold known addresses, decoded switch tables, and the reads that extend or
// refuse the copy. Exhausting the bounded scan is a refusal, not proof that
// no back edge exists; known forward destinations keep an early return from
// ending it. Operands are decoded only where the scan needs them. The few
// PC-relative instructions that end up in the copy are decoded again by
// select(), which is cheaper than keeping a full decode of every branch.
class prologue_scan {
public:
  prologue_scan(relocation_info &info, uintptr_t address, size_t jump_size,
                size_t function_size, const relocation_info::scan_bounds &scan)
      : info_(info), bytes_(info.source_bytes.data()), address_(address),
        jump_size_(jump_size), function_size_(function_size),
        window_(scan.window), limit_(scan.limit), bounded_(scan.bounded) {
    ZydisDecoderInit(&decoder_, ZYDIS_MACHINE_MODE_LONG_64,
                     ZYDIS_STACK_WIDTH_64);
  }

  // Scans to the end of the function. False refuses the target.
  bool run() {
    for (size_t off = 0; off < limit_;) {
      size_t end = 0;
      const outcome next = step(off, end);
      if (next == outcome::refuse)
        return false;
      if (next == outcome::stop)
        break;
      off = end;
    }
    if ((function_size_ != 0 && scanned_ == function_size_) ||
        (bounded_ && scanned_ == limit_))
      ended_ = true;
    // No terminator that every earlier branch stays before, within the
    // window: the end of the function is unknown, not short.
    if (!ended_)
      return refuse("function_end_not_found", scanned_);
    return size_ >= jump_size_ || refuse("function_too_short", scanned_,
                                         detour_error::kInsufficientBytes);
  }

  // Whether the rest of the scanned code leaves the chosen copy safe to
  // move: nothing can still enter or observe the original bytes.
  bool check() const {
    // A copied x87 instruction leaves the trampoline in FIP, where a later
    // FNSTENV or XSAVE in this body would see it. Nothing outside the scan
    // is considered: signal frames and debuggers also record FIP, but
    // compiled code does not act on it.
    if (fip_observed_ && first_fip_ < size_)
      return refuse("unsupported_instruction", first_fip_);
    const auto &offsets = info_.instruction_offsets;
    // Every scanned branch, including those in the copy, lands on an
    // instruction boundary.
    for (const auto &[from, t] : branch_targets_) {
      if (t >= 0 && t < static_cast<intptr_t>(scanned_) &&
          !std::binary_search(offsets.begin(), offsets.end(), t))
        return refuse("branch_into_instruction", from);
      // A join inside a dispatch block brings registers the table match did
      // not evaluate, and an index the bound check did not see unless it
      // comes from another check of the same index.
      for (const auto &d : dispatch_blocks_) {
        const auto u = static_cast<size_t>(t);
        if (t < 0 ||
            (u == d.entry && bounds_index(decoder_, {bytes_, window_}, offsets,
                                          from, d.bound, d.count)))
          continue;
        if ((u > d.block && u <= d.jcc) || (u >= d.entry && u <= d.jmp))
          return refuse("indirect_branch_unproven", from);
      }
    }
    for (const size_t at : indexed_reads_) {
      if (std::none_of(dispatch_blocks_.begin(), dispatch_blocks_.end(),
                       [&](const auto &d) {
                         return at >= d.block && at <= d.jmp &&
                                !touches_patch(
                                    static_cast<intptr_t>(d.table - address_),
                                    static_cast<intptr_t>(d.table_size));
                       }))
        return refuse("indirect_branch_unproven", at);
    }
    // A call to the entry is a new invocation through the hook. One from
    // outside the copy to a later copied offset would run patched bytes.
    for (const auto &[off, t] : interior_calls_) {
      if (off >= size_ && t < static_cast<intptr_t>(size_))
        return refuse("branch_into_patch", off);
    }
    // The address-producing instruction or table keeps pointer identity, so
    // an indirect jump into the copy would still enter the original bytes.
    for (const auto &[off, destination] : indirect_targets_) {
      if (destination >= address_ && destination < address_ + size_)
        return refuse("branch_into_patch", off);
    }
    return true;
  }

  // Decodes the PC-relative instructions of the copy into the relocations,
  // and trims the snapshot to the bytes the trampoline and the commit check
  // read. Returns the copied size, or 0 to refuse.
  size_t select() {
    intptr_t original_begin = 0;
    size_t original_end = 0;
    for (const size_t at : relative_) {
      if (at >= size_)
        break;
      relocation_entry r;
      r.address = at;
      ZydisDecoderContext ctx;
      if (ZYAN_FAILED(ZydisDecoderDecodeInstruction(
              &decoder_, &ctx, bytes_ + at, size_ - at, &r.instruction)) ||
          ZYAN_FAILED(ZydisDecoderDecodeOperands(
              &decoder_, &ctx, &r.instruction, r.operands.data(),
              r.instruction.operand_count)))
        return refuse("undecodable_instruction", at);
      if (r.instruction.mnemonic == ZYDIS_MNEMONIC_CALL &&
          branch_offset(r.instruction, r.address) ==
              static_cast<intptr_t>(r.address + r.instruction.length))
        return refuse("unsupported_instruction", at);
      const rel_form form = classify(r);
      if (form == rel_form::unsupported)
        return refuse("unsupported_relocation", at);
      if (form == rel_form::memory) {
        // The scan already refused unknown extents. RSP operands can retain
        // their encoding when reachable; the emitter rejects only far shapes
        // that would observe its temporary spill.
        const int m = rip_operand(r);
        const auto &op = r.operands[m];
        const auto t = operand_target(r, m, address_);
        if (!t)
          return refuse("unsupported_relocation", at);
        const auto offset = static_cast<intptr_t>(*t - address_);
        const auto width =
            static_cast<intptr_t>(std::max<size_t>(op.size / 8, 1));
        if (op.mem.type != ZYDIS_MEMOP_TYPE_AGEN &&
            touches_patch(offset, width)) {
          // Base zero could read or overwrite the patched bytes.
          if (segmented_memory(op) ||
              (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0)
            return refuse("patched_bytes_referenced", at);
          if (is_data_read(r)) {
            original_begin = std::min(original_begin, offset);
            original_end =
                std::max(original_end, static_cast<size_t>(offset + width));
          }
        }
      }
      info_.relocations.emplace_back(std::move(r));
    }
    const size_t end = std::max({size_, scanned_, original_end});
    if (end > window_)
      return refuse("preserved_bytes_unreadable", window_);
    info_.source_bytes.resize(end);
    if (original_begin < 0 &&
        !info_.read_before(static_cast<size_t>(-original_begin)))
      return refuse("preserved_bytes_unreadable", 0);
    return size_;
  }

private:
  enum class outcome : uint8_t { next, stop, refuse };

  // Records why the target is refused at `off` from the entry. False and
  // zero both refuse, so every caller can return it.
  bool refuse(const char *reason, size_t off,
              detour_error error = detour_error::kDecodeFailed) const {
    info_.refuse(reason, address_ + off, error);
    return false;
  }

  // Decoded switch dispatches, as matched by jump_table_reader, and the JMP.
  struct dispatch_block {
    size_t block;
    size_t jcc;
    size_t entry;
    size_t jmp;
    ZydisRegister bound;
    uint64_t count;
    uintptr_t table;
    size_t table_size;
  };

  // Bytes [0, jump_size) are overwritten by the patch.
  bool touches_patch(intptr_t offset, intptr_t width) const {
    return offset < static_cast<intptr_t>(jump_size_) && offset + width > 0;
  }

  // Scans the instruction at `off`, ending at `end`.
  outcome step(size_t off, size_t &end) {
    relocation_entry e;
    const auto &insn = e.instruction;
    ZydisDecoderContext ctx;
    // Every byte before the end of the function is an instruction, so one
    // that does not decode refuses rather than ending the scan.
    if (ZYAN_FAILED(ZydisDecoderDecodeInstruction(
            &decoder_, &ctx, bytes_ + off, limit_ - off, &e.instruction))) {
      refuse("undecodable_instruction", off);
      return outcome::refuse;
    }
    e.address = off;
    end = off + insn.length;
    const uint16_t prior = known_;
    if (insn.mnemonic != ZYDIS_MNEMONIC_LEA &&
        insn.mnemonic != ZYDIS_MNEMONIC_MOV &&
        insn.mnemonic != ZYDIS_MNEMONIC_JMP &&
        insn.mnemonic != ZYDIS_MNEMONIC_NOP &&
        insn.mnemonic != ZYDIS_MNEMONIC_CMP &&
        insn.mnemonic != ZYDIS_MNEMONIC_TEST)
      known_ = 0;
    fip_observed_ |= observes_pc(insn.mnemonic);
    if (sets_fip(insn))
      first_fip_ = std::min(first_fip_, off);
    scanned_ = end;
    info_.instruction_offsets.push_back(off);
    if (size_ < jump_size_)
      size_ = end;
    if (const auto t = branch_offset(insn, off)) {
      branch_targets_.emplace_back(off, *t);
      if (insn.mnemonic == ZYDIS_MNEMONIC_CALL && *t > 0 &&
          *t < static_cast<intptr_t>(window_))
        interior_calls_.emplace_back(off, *t);
      known_ = 0;
      if (*t >= 0 && insn.mnemonic != ZYDIS_MNEMONIC_CALL &&
          (*t < static_cast<intptr_t>(limit_) ||
           (!bounded_ && insn.mnemonic != ZYDIS_MNEMONIC_JMP)))
        reached_ = std::max(reached_, *t);
      // A local loop, including one to offset 0, stays in the copy. CALL
      // to the entry is a new invocation and still enters the hook.
      if (insn.mnemonic != ZYDIS_MNEMONIC_CALL && off >= size_ && *t >= 0 &&
          *t < static_cast<intptr_t>(size_))
        size_ = end;
    }
    if ((insn.attributes & ZYDIS_ATTRIB_IS_RELATIVE) != 0)
      relative_.push_back(off);
    // While no register holds a known address, only RIP operands, immediate
    // MOVs and indirect JMPs have operands the scan reads.
    if ((prior != 0 || insn.raw.disp.size == 32 ||
         (insn.mnemonic == ZYDIS_MNEMONIC_MOV && insn.raw.imm[0].size != 0) ||
         (insn.mnemonic == ZYDIS_MNEMONIC_JMP &&
          !insn.raw.imm[0].is_relative)) &&
        !track_operands(ctx, e, end, prior))
      return outcome::refuse;
    // An explicit extent shorter than the patch ends like an inferred one:
    // in a terminator followed by padding.
    const bool short_extent = function_size_ != 0 && end == function_size_ &&
                              end < jump_size_ && ends_flow(insn);
    if (!short_extent &&
        !(function_size_ == 0 && reached_ < static_cast<intptr_t>(end) &&
          (ends_flow(insn) ||
           (!bounded_ && insn.mnemonic == ZYDIS_MNEMONIC_CALL &&
            ends_in_noreturn_call(decoder_, bytes_ + end, window_ - end,
                                  address_ + end)))))
      return outcome::next;
    if (end < jump_size_) {
      const size_t padding =
          padding_size(decoder_, reinterpret_cast<uintptr_t>(bytes_ + end),
                       window_ - end, jump_size_ - end);
      if (padding == 0) {
        refuse("function_too_short", end, detour_error::kInsufficientBytes);
        return outcome::refuse;
      }
      size_ = std::max(size_, end + padding);
      scanned_ = std::max(scanned_, size_);
    }
    ended_ = true;
    return outcome::stop;
  }

  // Tracks addresses formed by LEA and MOV so a register or RIP-slot jump
  // can be resolved, checks memory operands against the patch, and matches
  // jump tables at an indirect JMP. False refuses.
  bool track_operands(ZydisDecoderContext &ctx, relocation_entry &e, size_t end,
                      uint16_t prior) {
    const auto &insn = e.instruction;
    const size_t off = e.address;
    if (ZYAN_FAILED(ZydisDecoderDecodeOperands(&decoder_, &ctx, &e.instruction,
                                               e.operands.data(),
                                               insn.operand_count)))
      return refuse("undecodable_instruction", off);
    const int m = rip_operand(e);
    const auto abs = operand_target(e, m, address_);
    if (m >= 0) {
      const auto &op = e.operands[m];
      // A thread-specific segment base hides the target.
      if (segmented_memory(op) && insn.mnemonic == ZYDIS_MNEMONIC_JMP)
        return refuse("indirect_branch_unproven", off);
      if (op.mem.type != ZYDIS_MEMOP_TYPE_AGEN) {
        // XRSTOR's full read size depends on the enabled state components.
        if (!known_memory_extent(e, m))
          return refuse("unsupported_instruction", off);
        // A later self-read joins the copy so it reads preserved bytes.
        if (abs && is_data_read(e) &&
            touches_patch(static_cast<intptr_t>(*abs - address_),
                          std::max<intptr_t>(op.size / 8, 1)))
          size_ = std::max(size_, end);
      }
    }
    for (unsigned i = 0; i < insn.operand_count_visible; ++i) {
      const auto &op = e.operands[i];
      if (op.type != ZYDIS_OPERAND_TYPE_MEMORY ||
          op.mem.type == ZYDIS_MEMOP_TYPE_AGEN ||
          op.mem.base == ZYDIS_REGISTER_RIP ||
          op.mem.base == ZYDIS_REGISTER_EIP)
        continue;
      const auto enclosing = ZydisRegisterGetLargestEnclosing(
          ZYDIS_MACHINE_MODE_LONG_64, op.mem.base);
      const int id = ZydisRegisterGetId(enclosing);
      if (id < 0 || id >= 16 || (prior & (1u << id)) == 0)
        continue;
      const uintptr_t base = values_[id];
      if (op.mem.index != ZYDIS_REGISTER_NONE && base >= address_ &&
          base < address_ + window_) {
        // Only a decoded jump table bounds such an access; check() looks.
        indexed_reads_.push_back(off);
      } else if (touches_patch(
                     static_cast<intptr_t>(base + op.mem.disp.value - address_),
                     std::max<intptr_t>(op.size / 8, 1))) {
        return refuse("patched_bytes_referenced", off);
      }
    }
    if (e.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
        e.operands[0].size == 64 &&
        (insn.mnemonic == ZYDIS_MNEMONIC_LEA ||
         insn.mnemonic == ZYDIS_MNEMONIC_MOV)) {
      const auto id = ZydisRegisterGetId(e.operands[0].reg.value);
      if (id >= 0 && id < 16) {
        known_ &= ~(1u << id);
        if (insn.mnemonic == ZYDIS_MNEMONIC_LEA && abs) {
          values_[id] = *abs;
          known_ |= 1u << id;
        } else if (e.operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
          values_[id] = e.operands[1].imm.value.u;
          known_ |= 1u << id;
        }
      }
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV ||
               insn.mnemonic == ZYDIS_MNEMONIC_LEA) {
      known_ = 0;
    }
    if (insn.mnemonic != ZYDIS_MNEMONIC_JMP || insn.raw.imm[0].is_relative)
      return true;
    std::optional<uintptr_t> destination;
    if (e.operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER) {
      const auto id = ZydisRegisterGetId(e.operands[0].reg.value);
      if (id >= 0 && id < 16 && (known_ & (1u << id)) != 0)
        destination = values_[id];
    } else if (uintptr_t value = 0;
               abs && e.operands[m].size == 64 &&
               read_code_bytes(*abs,
                               {reinterpret_cast<uint8_t *>(&value), 8})) {
      destination = value;
    }
    if (destination) {
      indirect_targets_.emplace_back(off, *destination);
      if (*destination >= address_ && *destination < address_ + limit_)
        reached_ =
            std::max(reached_, static_cast<intptr_t>(*destination - address_));
      return true;
    }
    const auto table = jump_table_reader(decoder_, {bytes_, window_}, address_,
                                         info_.instruction_offsets)
                           .read();
    // Any other unproven destination is taken as a tail call through a
    // register, vtable or GOT slot, and ends the flow like RET.
    if (!table.matched)
      return true;
    // Case labels are local branch targets. One in the copy would still be
    // entered at its original address.
    if (table.targets.empty())
      return refuse("indirect_branch_unproven", off);
    dispatch_blocks_.push_back({table.block, table.jcc, table.entry, off,
                                table.bound, table.count, table.table,
                                table.table_size});
    for (const uintptr_t target : table.targets) {
      indirect_targets_.emplace_back(off, target);
      if (target >= address_ && target < address_ + limit_) {
        const auto t = static_cast<intptr_t>(target - address_);
        branch_targets_.emplace_back(off, t);
        reached_ = std::max(reached_, t);
      }
    }
    return true;
  }

  relocation_info &info_;
  ZydisDecoder decoder_;
  const uint8_t *bytes_;
  uintptr_t address_;
  size_t jump_size_;
  size_t function_size_;
  size_t window_;
  size_t limit_;
  bool bounded_;

  size_t size_ = 0;
  size_t scanned_ = 0;
  bool ended_ = false;
  intptr_t reached_ = -1;
  // Registers holding a known absolute address, as a bitmask over `values_`.
  uint16_t known_ = 0;
  std::array<uintptr_t, 16> values_{};
  size_t first_fip_ = SIZE_MAX;
  bool fip_observed_ = false;
  std::vector<size_t> relative_;
  std::vector<std::pair<size_t, uintptr_t>> indirect_targets_;
  // Local branch destinations, as (branch offset, destination).
  std::vector<std::pair<size_t, intptr_t>> branch_targets_;
  std::vector<dispatch_block> dispatch_blocks_;
  // Indexed accesses through a known address in the window.
  std::vector<size_t> indexed_reads_;
  // Calls into the function after its entry, as (call offset, target).
  std::vector<std::pair<size_t, intptr_t>> interior_calls_;
};

// Lays out one trampoline at `at`: the copy with each PC-relative
// instruction rewritten, the exit, and the preserved original bytes that
// self-reads use. Owns the output, its fixups and the frame notes unwinding
// reads.
//
// When `at` is known, a relative branch or [rip] operand that still reaches
// its target from there keeps a rel32 or disp32 form. Otherwise it is
// rewritten through an absolute address. With `at == 0` it uses the largest
// reachable or absolute form, bounding the final size. EIP-relative operands
// remain reachable at every placement.
class trampoline_writer {
public:
  trampoline_writer(uintptr_t at, uintptr_t fn, std::span<const uint8_t> target,
                    const relocation_info &relocations, bool sizing_only,
                    bool unwind)
      : at_(at), fn_(fn), target_(target), relocations_(relocations),
        sizing_only_(sizing_only), unwind_(unwind) {}

  // Copies up to `relo` and rewrites it. False when it has no form here.
  bool relocate(const relocation_entry &relo) {
    const auto &insn = relo.instruction;
    copy_to(relo.address);
    offsets_.emplace_back(relo.address, out_.size() - relo.address);
    cursor_ = relo.address + insn.length;
#if SPUD_OS_WIN && SPUD_UNWIND_SUPPORT
    if (unwind_ && (insn.meta.category == ZYDIS_CATEGORY_UNCOND_BR ||
                    insn.meta.category == ZYDIS_CATEGORY_COND_BR))
      emit({0x90}); // A preceding copied call can return before this branch.
#endif
    const rel_form form = classify(relo);
    const auto abs = operand_target(
        relo, form == rel_form::memory ? rip_operand(relo) : 0, fn_);
    if (!abs || form == rel_form::none || form == rel_form::unsupported)
      return false;
    if (form == rel_form::memory) {
      if (!relocate_memory(relo, *abs))
        return false;
    } else {
      relocate_branch(relo, form, *abs);
    }
    offsets_.emplace_back(cursor_, out_.size() - cursor_);
    return true;
  }

  // Copies the rest, jumps to `return_address`, appends the preserved
  // original bytes and resolves the fixups. Empty on failure.
  trampoline_buffer finish(uintptr_t return_address) {
    copy_to(target_.size());
    offsets_.emplace_back(target_.size(), out_.size() - target_.size());
#if SPUD_OS_WIN && SPUD_UNWIND_SUPPORT
    // A copied call can return here. Keep its dispatch PC out of the jump
    // back to the source body, which needs prologue-style unwind treatment.
    if (unwind_)
      emit({0x90});
#endif
    // A direct jmp needs no endbr64 at the return address under IBT.
    const auto exit =
        static_cast<int64_t>(return_address - (at_ + out_.size() + 5));
    if (at_ != 0 && exit == static_cast<int32_t>(exit)) {
      emit({0xE9});
      emit32(static_cast<int32_t>(exit));
    } else {
      emit_absolute_jmp(return_address);
    }

    size_t original_start = 0;
    const size_t code_end = out_.size();
    if (preserve_original_) {
      const uintptr_t begin = fn_ + original_begin_;
      const size_t length =
          static_cast<size_t>(original_end_ - original_begin_);
      // Keep aligned vector reads aligned too. The bound reserves the
      // maximum padding, since the final trampoline address can need a
      // different amount.
      const size_t padding =
          (begin - (at_ + out_.size())) & (kPreservedAlignment - 1);
      out_.resize(out_.size() + padding, 0x90);
      original_start = out_.size();
      const auto original = relocations_.snapshot(begin, length);
      if (original.empty())
        return {0, {}, {}};
      emit_bytes(original.data(), length);
      if (at_ == 0)
        out_.resize(out_.size() + kPreservedAlignment - 1 - padding, 0x90);
    }
    for (const auto &f : fixups_) {
      const size_t dest = f.kind == dest_kind::code
                              ? relocated_location_at(offsets_, f.offset)
                              : original_start + (f.offset - original_begin_);
      const auto rel = static_cast<int32_t>(dest - f.next);
      std::memcpy(out_.data() + f.field, &rel, sizeof(rel));
    }
    std::vector<unwind_range> ranges;
    if (SPUD_UNWIND_SUPPORT && unwind_)
      ranges.push_back({0, code_end, 0});
    return {0,
            std::move(out_),
            std::move(offsets_),
            std::move(ranges),
            std::move(stack_),
            std::move(relays_)};
  }

private:
  using register_slot = stack_adjustment::register_slot;
  // Code and preserved data have different offsets after relocation. Both
  // destinations are local, so these fields can be filled without `at`.
  enum class dest_kind : uint8_t { code, original };
  struct fixup {
    size_t field;
    size_t next;
    intptr_t offset;
    dest_kind kind;
  };
  // The instruction with its RIP operand replaced by [base]. A zero length
  // means it has no encoding.
  struct encoded {
    std::array<uint8_t, ZYDIS_MAX_INSTRUCTION_LENGTH> bytes;
    ZyanUSize length = 0;
  };

  void emit(std::initializer_list<uint8_t> bytes) {
    out_.insert(out_.end(), bytes);
  }
  void emit_bytes(const void *data, size_t size) {
    const auto *p = static_cast<const uint8_t *>(data);
    out_.insert(out_.end(), p, p + size);
  }
  void emit32(int32_t v) {
    emit_bytes(&v, sizeof(v));
  }
  void emit64(uint64_t v) {
    emit_bytes(&v, sizeof(v));
  }
  void emit_push(uint8_t id) {
    if (id >= 8)
      emit({0x41});
    emit({static_cast<uint8_t>(0x50 | (id & 7))});
  }
  void emit_pop(uint8_t id) {
    if (id >= 8)
      emit({0x41});
    emit({static_cast<uint8_t>(0x58 | (id & 7))});
  }
  // The frame state from the end of the bytes emitted so far: SP below its
  // source value and where borrowed registers keep their source values.
  void note(int64_t displacement,
            decltype(stack_adjustment::saved_registers) saved = {}) {
    if (SPUD_UNWIND_SUPPORT && unwind_)
      stack_.push_back({out_.size(), displacement, saved});
  }
  // lea rsp,[rsp-128]; push reg. Spills below the source red zone.
  void emit_spill(uint8_t id) {
    emit({0x48, 0x8D, 0x64, 0x24, 0x80});
    note(128);
    emit_push(id);
    note(136, register_slot{kDwarfRegisters[id], 0});
  }
  bool is_internal(uintptr_t abs) const {
    return abs >= fn_ && abs <= fn_ + target_.size();
  }
  // Whether an instruction of `length` bytes starting here, ending in a
  // rel32 field, reaches `abs`.
  bool reaches(uintptr_t abs, size_t length) const {
    if (is_internal(abs))
      return true;
    const auto d = static_cast<int64_t>(abs - (at_ + out_.size() + length));
    return at_ != 0 && d == static_cast<int32_t>(d);
  }
  // The rel32 field that ends the instruction being emitted.
  void emit_rel32(uintptr_t abs) {
    if (is_internal(abs)) {
      fixups_.push_back({out_.size(), out_.size() + 4,
                         static_cast<intptr_t>(abs - fn_), dest_kind::code});
      emit32(0);
    } else {
      emit32(static_cast<int32_t>(abs - (at_ + out_.size() + 4)));
    }
  }
  // jmp [rip + 0]; .quad abs
  void emit_absolute_jmp(uint64_t abs) {
    emit({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});
    emit64(abs);
  }
  // The skip jump keeps the absolute target literal out of the return path.
  void emit_absolute_call(uint64_t abs) {
#if SPUD_OS_WIN && SPUD_UNWIND_SUPPORT
    // The return PC must precede the skip jump. Windows otherwise treats
    // that jump as an epilogue and bypasses this frame's exception handler.
    if (unwind_)
      emit({0xFF, 0x15, 0x03, 0x00, 0x00, 0x00, 0x90, 0xEB, 0x08});
    else
#endif
      emit({0xFF, 0x15, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x08});
    emit64(abs);
  }
  void copy_to(size_t end) {
    emit_bytes(target_.data() + cursor_, end - cursor_);
    cursor_ = end;
  }
  void emit_segment(const ZydisDecodedOperand &op) {
    if (op.mem.segment == ZYDIS_REGISTER_FS)
      emit({0x64});
    else if (op.mem.segment == ZYDIS_REGISTER_GS)
      emit({0x65});
  }

  // Rewritten branches drop a BND prefix. MPX is gone from current CPUs and
  // kernels; without it the prefix does nothing.
  void relocate_branch(const relocation_entry &relo, rel_form form,
                       uintptr_t abs) {
    const auto &insn = relo.instruction;
    switch (form) {
    case rel_form::jmp:
      if (reaches(abs, 5)) {
        emit({0xE9});
        emit_rel32(abs);
      } else {
        emit_absolute_jmp(abs);
      }
      break;
    case rel_form::call:
      if (abs == fn_) {
        // A call to the entry goes through the hook, never into the copy.
        const auto disp = static_cast<int64_t>(abs - (at_ + out_.size() + 5));
        if (at_ != 0 && disp == static_cast<int32_t>(disp)) {
          emit({0xE8});
          emit32(static_cast<int32_t>(disp));
        } else {
          emit_absolute_call(abs);
        }
      } else if (reaches(abs, 5)) {
        emit({0xE8});
        emit_rel32(abs);
      } else {
        emit_absolute_call(abs);
      }
      break;
    case rel_form::jcc: {
      const auto cc = static_cast<uint8_t>(insn.opcode & 0x0F);
      if (reaches(abs, 6)) {
        emit({0x0F, static_cast<uint8_t>(0x80 | cc)});
        emit_rel32(abs);
      } else {
        // The inverted condition skips the absolute jump.
        emit({static_cast<uint8_t>(0x70 | (cc ^ 1)), 0x0E});
        emit_absolute_jmp(abs);
      }
      break;
    }
    case rel_form::loop: {
      // These only have rel8. Keep the instruction, prefixes included, and
      // aim it at a jump to the real target: insn +2; jmp short past; jmp.
      std::array<uint8_t, ZYDIS_MAX_INSTRUCTION_LENGTH> bytes{};
      std::memcpy(bytes.data(), target_.data() + relo.address, insn.length);
      bytes[insn.raw.imm[0].offset] = 2;
      emit_bytes(bytes.data(), insn.length);
      if (reaches(abs, 2 + 5)) {
        emit({0xEB, 0x05, 0xE9});
        emit_rel32(abs);
      } else {
        emit({0xEB, 0x0E});
        emit_absolute_jmp(abs);
      }
      break;
    }
    case rel_form::xbegin:
      // rel16 also sign-extends to the full RIP, so both input forms can
      // use rel32. An abort takes the veneer; normal execution skips it.
      emit({0xC7, 0xF8});
      if (reaches(abs, 4)) {
        emit_rel32(abs);
      } else {
        emit32(2);
        emit({0xEB, 0x0E});
        emit_absolute_jmp(abs);
      }
      break;
    default:
      break;
    }
  }

  // A [rip] operand: a read of the patched bytes goes to the preserved
  // copy, a reachable one keeps its encoding with a new disp32, and the
  // rest go through an absolute address in a borrowed register or slot.
  bool relocate_memory(const relocation_entry &relo, uintptr_t abs) {
    const auto &insn = relo.instruction;
    const uint8_t *original = target_.data() + relo.address;
    const size_t start = out_.size();
    const int m = rip_operand(relo);
    const auto &op = relo.operands[m];
    const auto data_offset = static_cast<intptr_t>(abs - fn_);
    const auto width = static_cast<intptr_t>(std::max<size_t>(op.size / 8, 1));
    if (is_data_read(relo) && op.mem.type != ZYDIS_MEMOP_TYPE_AGEN &&
        (op.actions & ZYDIS_OPERAND_ACTION_MASK_READ) != 0 &&
        (op.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) == 0 &&
        data_offset + width > 0 &&
        data_offset < static_cast<intptr_t>(relocations_.overwritten_size)) {
      preserve_original_ = true;
      original_begin_ = std::min(original_begin_, data_offset);
      original_end_ = std::max(original_end_, data_offset + width);
      // A preserved copy need not be below 4GB. Remove the address-size
      // override so this particular self-read can reach it through RIP.
      size_t removed = 0;
      for (size_t i = 0; i < insn.length; ++i) {
        if (op.mem.base == ZYDIS_REGISTER_EIP && i < insn.raw.prefix_count &&
            original[i] == 0x67) {
          ++removed;
        } else {
          out_.push_back(original[i]);
        }
      }
      fixups_.push_back({start + insn.raw.disp.offset - removed, out_.size(),
                         data_offset, dest_kind::original});
      return true;
    }
    const auto disp =
        static_cast<int64_t>(abs - (at_ + out_.size() + insn.length));
    if (at_ != 0 && (op.mem.base == ZYDIS_REGISTER_EIP ||
                     disp == static_cast<int32_t>(disp))) {
      const auto field = static_cast<int32_t>(disp);
      emit_bytes(original, insn.length);
      std::memcpy(out_.data() + start + insn.raw.disp.offset, &field,
                  sizeof(field));
      return true;
    }
    const bool qword = op.size == 64;
    const bool word = op.size == 16;
    if (insn.mnemonic == ZYDIS_MNEMONIC_JMP && qword)
      return jump_through_slot(relo, op, abs);
    if (insn.mnemonic == ZYDIS_MNEMONIC_LEA) {
      move_address(relo, abs);
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_MOV && m == 0 &&
               relo.operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
               ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,
                                                relo.operands[1].reg.value) ==
                   ZYDIS_REGISTER_RSP) {
      store_stack_pointer(op, abs);
    } else if (is_register_load(relo) &&
               relo.operands[0].reg.value == ZYDIS_REGISTER_RSP) {
      load_stack_pointer(op, abs);
    } else if (is_register_load(relo)
#if SPUD_OS_WIN && SPUD_UNWIND_SUPPORT
               && !unwind_
#endif
    ) {
      if (!load_through_destination(relo, m, abs))
        return false;
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_CALL && qword) {
      call_through_slot(insn, op, abs);
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_PUSH && word) {
      push_word(op, abs);
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_POP && word) {
      pop_word(op, abs);
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_PUSH && qword) {
      // The push slot is the one the original writes, so this stays out of
      // the red zone.
      load_into_slot(op, abs, 0);
    } else if (insn.mnemonic == ZYDIS_MNEMONIC_POP && qword) {
      pop_qword(op, abs);
    } else if (!access_through_scratch(relo, m, abs)) {
      return false;
    }
    // Keep the rewrite at least as long as the in-range form, so the
    // `at == 0` build stays an upper bound.
    if (out_.size() - start < insn.length)
      out_.resize(start + insn.length, 0x90);
    return true;
  }

  // lea r, [rip + d] is mov r, imm (B8+r) at the width of r. Both
  // zero-extend a 32-bit destination and keep the top of a 16-bit one.
  void move_address(const relocation_entry &relo, uintptr_t abs) {
    const auto bits = relo.operands[0].size;
    const auto id =
        static_cast<uint8_t>(ZydisRegisterGetId(relo.operands[0].reg.value));
    uint8_t rex = id >= 8 ? 0x41 : 0x00;
    if (bits == 16) {
      emit({0x66});
    } else if (bits == 64) {
      rex |= 0x48;
    }
    if (rex != 0) {
      emit({rex});
    }
    emit({static_cast<uint8_t>(0xB8 | (id & 7))});
    emit_bytes(&abs, bits / 8);
  }

  // mov [rip + d], rsp. Materialize the source SP before storing through
  // MOFFS. The scratch slot stays below the red zone and inside the current
  // stack.
  void store_stack_pointer(const ZydisDecodedOperand &op, uintptr_t abs) {
    emit_spill(0);
    emit({0x48, 0x8D, 0x84, 0x24, 0x88, 0, 0, 0}); // lea rax,[rsp+136]
    emit_segment(op);
    if (op.size == 16)
      emit({0x66});
    else if (op.size == 64)
      emit({0x48});
    emit({static_cast<uint8_t>(op.size == 8 ? 0xA2 : 0xA3)});
    emit64(abs);
    emit_pop(0);
    note(128);
    emit({0x48, 0x8D, 0xA4, 0x24, 0x80, 0, 0, 0});
    note(0);
  }

  // mov rsp, [rip + d]. Holding the address in RSP would make a load fault
  // lose the stack needed to deliver its signal. Keep source SP until the
  // final MOV.
  void load_stack_pointer(const ZydisDecodedOperand &op, uintptr_t abs) {
    emit_spill(0);
    emit_segment(op);
    emit({0x48, 0xA1}); // mov rax,[moffs64]
    emit64(abs);
    emit({0x48, 0x87, 0x04, 0x24}); // Restore RAX and retain the loaded SP.
    note(136);
    emit({0x48, 0x8B, 0x24, 0x24}); // mov rsp,[rsp]
    note(0);
  }

  // mov reg, [rip + d] through the destination itself. A source CFA may read
  // the destination until this instruction ends, so with unwind records its
  // old value is spilled while it holds the address and the loaded value.
  bool load_through_destination(const relocation_entry &relo, int m,
                                uintptr_t abs) {
    const auto reg = relo.operands[0].reg.value;
    const auto load = encode_through(relo, m, reg);
    if (load.length == 0)
      return false;
    const auto id = static_cast<uint8_t>(ZydisRegisterGetId(reg));
    if (SPUD_UNWIND_SUPPORT && unwind_)
      emit_spill(id);
    emit({static_cast<uint8_t>(id >= 8 ? 0x49 : 0x48),
          static_cast<uint8_t>(0xB8 | (id & 7))}); // mov destination, imm64
    emit64(abs);
    emit_bytes(load.bytes.data(), load.length);
    if (SPUD_UNWIND_SUPPORT && unwind_) {
      emit({0x48, 0x8D, 0xA4, 0x24, 0x88, 0, 0, 0});
      note(0);
    }
    return true;
  }

  // jmp [rip + d]. JMP cannot borrow its caller's red zone. A near relay
  // can read the live pointer without borrowing either a register or the
  // red zone; its landing pad covers the new indirect transfer introduced
  // by the absolute jump. Like the in-range forms, it is not padded.
  bool jump_through_slot(const relocation_entry &relo,
                         const ZydisDecodedOperand &op, uintptr_t abs) {
    const auto &insn = relo.instruction;
    if (sizing_only_) {
      emit_absolute_jmp(0);
      return true;
    }
    const size_t length = 4 + insn.length;
    void *memory = executable_pool::instance().allocate_near(
        length, abs,
        unwind_ ? executable_pool::unwind_metadata::kTrampoline
                : executable_pool::unwind_metadata::kNone);
    if (memory == nullptr)
      return false;
    if (relays_ == nullptr)
      relays_ = std::make_unique<executable_relays>();
    auto &relay = relays_->entries.emplace_back(memory, length);
    const auto entry = reinterpret_cast<uintptr_t>(memory);
    const auto delta = static_cast<int64_t>(abs - (entry + length));
    if (op.mem.base != ZYDIS_REGISTER_EIP &&
        delta != static_cast<int32_t>(delta))
      return false;
    // endbr64, then the original JMP with its disp32 aimed at the slot.
    // The caller writes it with publish_relays().
    const uint8_t *original = target_.data() + relo.address;
    relay.code = {0xF3, 0x0F, 0x1E, 0xFA};
    relay.code.insert(relay.code.end(), original, original + insn.length);
    const auto displacement = static_cast<int32_t>(delta);
    std::memcpy(relay.code.data() + 4 + insn.raw.disp.offset, &displacement, 4);
    relay.source_offset = relo.address;
    emit_absolute_jmp(entry);
    return true;
  }

  // push r11; r11 = [abs]; then swap it into the pushed slot, which restores
  // R11. `displacement` is SP below its source value once the swap is done.
  void load_into_slot(const ZydisDecodedOperand &op, uintptr_t abs,
                      int64_t displacement) {
    emit({0x41, 0x53}); // push r11
    note(8, register_slot{11, 0});
    emit({0x49, 0xBB}); // mov r11, imm64
    emit64(abs);
    emit_segment(op);
    emit({0x4D, 0x8B, 0x1B});       // mov r11, [r11]
    emit({0x4C, 0x87, 0x1C, 0x24}); // xchg [rsp], r11
    note(displacement);
  }

  // call [rip + d]. CALL already consumes a return slot: use that slot for
  // its target, then call through it with the original SP and R11 restored.
  void call_through_slot(const ZydisDecodedInstruction &insn,
                         const ZydisDecodedOperand &op, uintptr_t abs) {
    load_into_slot(op, abs, 8);
    emit({0x48, 0x8D, 0x64, 0x24, 0x08}); // lea rsp, [rsp + 8]
    note(0);
    if ((insn.attributes & ZYDIS_ATTRIB_HAS_NOTRACK) != 0)
      emit({0x3E});
    emit({0xFF, 0x54, 0x24, 0xF8}); // call [rsp - 8]
  }

  // push word [rip + d]. Borrow exactly the slot PUSH writes. AX is restored
  // before the next original instruction; no other red-zone bytes are
  // touched.
  void push_word(const ZydisDecodedOperand &op, uintptr_t abs) {
    emit({0x48, 0x8D, 0x64, 0x24, 0xFE}); // lea rsp,[rsp-2]
    note(2);
    emit({0x66, 0x87, 0x04, 0x24}); // xchg ax,[rsp]
    note(2, register_slot{0, 0, register_slot::width::kWord});
    emit_segment(op);
    emit({0x66, 0xA1}); // mov ax,[moffs64]
    emit64(abs);
    emit({0x66, 0x87, 0x04, 0x24});
    note(0);
  }

  void pop_word(const ZydisDecodedOperand &op, uintptr_t abs) {
    emit({0x66, 0x87, 0x04, 0x24});
    note(0, register_slot{0, 0, register_slot::width::kWord});
    emit_segment(op);
    emit({0x66, 0xA3}); // mov [moffs64],ax
    emit64(abs);
    emit({0x66, 0x87, 0x04, 0x24});
    note(0);
    emit({0x48, 0x8D, 0x64, 0x24, 0x02}); // lea rsp,[rsp+2]
  }

  // pop qword [rip + d]. Restore the consumed slot too: it becomes part of
  // the red zone after POP and the original value may still be read there.
  void pop_qword(const ZydisDecodedOperand &op, uintptr_t abs) {
    emit({0x48, 0x87, 0x04, 0x24}); // xchg [rsp], rax
    note(0, register_slot{0, 0});
    emit_segment(op);
    emit({0x48, 0xA3}); // mov [abs], rax
    emit64(abs);
    emit({0x48, 0x87, 0x04, 0x24}); // xchg [rsp], rax
    note(0);
    emit({0x48, 0x8D, 0x64, 0x24, 0x08}); // lea rsp, [rsp + 8]
  }

  // The instruction with its RIP operand replaced by [base]. With
  // `source_sp`, explicit RSP operands name that register instead.
  encoded encode_through(const relocation_entry &relo, int m,
                         ZydisRegister base,
                         ZydisRegister source_sp = ZYDIS_REGISTER_NONE) const {
    const auto &insn = relo.instruction;
    const auto &op = relo.operands[m];
    encoded result;
    ZydisEncoderRequest req{};
    if (!ZYAN_SUCCESS(ZydisEncoderDecodedInstructionToEncoderRequest(
            &insn, relo.operands.data(), insn.operand_count_visible, &req)))
      return result;
    req.operands[m].mem.base = base;
    req.operands[m].mem.index = ZYDIS_REGISTER_NONE;
    req.operands[m].mem.scale = 0;
    req.operands[m].mem.displacement = 0;
    if (source_sp != ZYDIS_REGISTER_NONE) {
      for (unsigned i = 0; i < insn.operand_count; ++i) {
        const auto &operand = relo.operands[i];
        if (operand.type != ZYDIS_OPERAND_TYPE_REGISTER ||
            ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,
                                             operand.reg.value) !=
                ZYDIS_REGISTER_RSP)
          continue;
        if (i >= insn.operand_count_visible)
          return result;
        const auto cls = ZydisRegisterGetClass(operand.reg.value);
        auto id = ZydisRegisterGetId(source_sp);
        // GPR8 IDs include AH..BH before SPL..R15B.
        if (cls == ZYDIS_REGCLASS_GPR8 && id >= 4)
          id += 4;
        req.operands[i].reg.value = ZydisRegisterEncode(cls, id);
      }
    }
    // The original EIP calculation already wrapped to a 32-bit offset. A
    // register now holds that offset, independent of the code's PC.
    if (op.mem.base == ZYDIS_REGISTER_EIP)
      req.address_size_hint = ZYDIS_ADDRESS_SIZE_HINT_64;
    result.length = result.bytes.size();
    if (!ZYAN_SUCCESS(ZydisEncoderEncodeInstruction(&req, result.bytes.data(),
                                                    &result.length)))
      result.length = 0;
    return result;
  }

  // Any other access, through a spilled scratch register holding the
  // address. Explicit SP operands use a second register holding source SP;
  // any updated SP is committed last. Windows SET_FPREG needs its live frame
  // register, so far register loads use a volatile address scratch instead
  // of their destination.
  bool access_through_scratch(const relocation_entry &relo, int m,
                              uintptr_t abs) {
    const auto &insn = relo.instruction;
    static constexpr ZydisRegister kScratch[] = {
        ZYDIS_REGISTER_R11, ZYDIS_REGISTER_R10, ZYDIS_REGISTER_R9,
        ZYDIS_REGISTER_R8,  ZYDIS_REGISTER_RAX, ZYDIS_REGISTER_RCX,
        ZYDIS_REGISTER_RDX,
#if !SPUD_OS_WIN
        ZYDIS_REGISTER_RSI, ZYDIS_REGISTER_RDI,
#endif
    };
    const bool reads_sp = uses_register(relo, ZYDIS_REGISTER_RSP);
    const auto *source_sp = std::end(kScratch);
    if (reads_sp)
      source_sp = std::find_if(
          std::begin(kScratch), std::end(kScratch),
          [&](ZydisRegister reg) { return !uses_register(relo, reg); });
    encoded access;
    const auto *scratch = std::find_if(
        std::begin(kScratch), std::end(kScratch), [&](ZydisRegister r) {
          if (uses_register(relo, r) ||
              (reads_sp && source_sp != std::end(kScratch) && r == *source_sp))
            return false;
          // AH/BH/CH/DH cannot share an encoding with a REX prefix.
          access = encode_through(relo, m, r,
                                  reads_sp && source_sp != std::end(kScratch)
                                      ? *source_sp
                                      : ZYDIS_REGISTER_NONE);
          return access.length != 0;
        });
    if (scratch == std::end(kScratch) ||
        (reads_sp && source_sp == std::end(kScratch)))
      return false;
    bool writes_sp = false;
    for (unsigned i = 0; i < insn.operand_count_visible; ++i)
      if (relo.operands[i].type == ZYDIS_OPERAND_TYPE_REGISTER &&
          ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,
                                           relo.operands[i].reg.value) ==
              ZYDIS_REGISTER_RSP)
        writes_sp |=
            (relo.operands[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0;
    const auto id = static_cast<uint8_t>(ZydisRegisterGetId(*scratch));
    const auto sp_id =
        reads_sp ? static_cast<uint8_t>(ZydisRegisterGetId(*source_sp))
                 : uint8_t{0};
    if (reads_sp) {
      emit_spill(sp_id);
      emit_push(id);
      note(144, std::array<register_slot, 2>{
                    {{kDwarfRegisters[sp_id], 8}, {kDwarfRegisters[id], 0}}});
    } else {
      emit_spill(id);
    }
    if (reads_sp)
      emit({static_cast<uint8_t>(sp_id >= 8 ? 0x4C : 0x48), 0x8D,
            static_cast<uint8_t>(0x84 | ((sp_id & 7) << 3)), 0x24, 0x90, 0, 0,
            0}); // lea source_sp,[rsp+144]
    emit({static_cast<uint8_t>(id >= 8 ? 0x49 : 0x48),
          static_cast<uint8_t>(0xB8 | (id & 7))}); // mov scratch, imm64
    emit64(abs);
    emit_bytes(access.bytes.data(), access.length);
    if (reads_sp && writes_sp) {
      emit({static_cast<uint8_t>(sp_id >= 8 ? 0x4C : 0x48), 0x87,
            static_cast<uint8_t>(0x44 | ((sp_id & 7) << 3)), 0x24, 8});
      note(144, register_slot{kDwarfRegisters[id], 0});
    }
    emit_pop(id); // pop scratch
    if (reads_sp) {
      if (writes_sp) {
        note(136);
        emit({0x48, 0x8B, 0x24, 0x24}); // mov rsp,[rsp]
      } else {
        note(136, register_slot{kDwarfRegisters[sp_id], 0});
        emit_pop(sp_id);
      }
    }
    if (!writes_sp) {
      note(128);
      emit({0x48, 0x8D, 0xA4, 0x24, 0x80, 0, 0, 0});
    }
    note(0);
    return true;
  }

  uintptr_t at_;
  uintptr_t fn_;
  // The snapshot of the copied bytes, at `fn_` in the target.
  std::span<const uint8_t> target_;
  const relocation_info &relocations_;
  bool sizing_only_;
  bool unwind_;

  std::vector<uint8_t> out_;
  std::vector<std::pair<uintptr_t, uintptr_t>> offsets_;
  std::vector<fixup> fixups_;
  std::vector<stack_adjustment> stack_;
  std::unique_ptr<executable_relays> relays_;
  // The preserved original covers exactly the bytes self-reads access.
  intptr_t original_begin_ = INTPTR_MAX;
  intptr_t original_end_ = INTPTR_MIN;
  bool preserve_original_ = false;
  // Source offset copied up to.
  size_t cursor_ = 0;
};

} // namespace

std::tuple<relocation_info, size_t>
collect_relocations(uintptr_t address, size_t jump_size,
                    std::span<const uint8_t> original_prefix,
                    size_t function_size) {
  relocation_info info;
  info.overwritten_size = jump_size;
  // A branch leaving a bounded scan is a tail call or an exit, not more of
  // this function, so the scan no longer runs into a neighbour, and the
  // noreturn-call guess is not needed. Padding after a short function still
  // comes from the window.
  const auto bounds =
      info.read_source(address, jump_size, original_prefix, function_size, 1);
  if (bounds.window == 0)
    return {std::move(info), 0};
  prologue_scan scan(info, address, jump_size, function_size, bounds);
  const size_t copied = scan.run() && scan.check() ? scan.select() : 0;
  return {std::move(info), copied};
}

trampoline_buffer create_trampoline(uintptr_t at, uintptr_t return_address,
                                    std::span<const uint8_t> target,
                                    const relocation_info &relocations,
                                    bool sizing_only, bool unwind) {
  const auto fn = reinterpret_cast<uintptr_t>(target.data());
  if (const auto snapshot = relocations.snapshot(fn, target.size());
      !snapshot.empty())
    target = snapshot;
  trampoline_writer writer(at, fn, target, relocations, sizing_only, unwind);
  for (const auto &relocation : relocations.relocations) {
    if (!writer.relocate(std::get<relocation_entry>(relocation)))
      return {0, {}, {}};
  }
  return writer.finish(return_address);
}

namespace {
// Stub layout.
constexpr uint8_t kEndbr64[] = {0xF3, 0x0F, 0x1E, 0xFA};
constexpr uint8_t kMovR11[] = {0x49, 0xBB};
// Counts the entry in one of the context's shards and leaves the shard's
// address in r10 for the wrapper to decrement. The shard is stack bits 12-15
// xor bits 20-23: consecutive glibc thread stacks are 8MB plus a guard page
// apart and Windows ones 1MB, so each layout spreads threads over different
// shards, while one thread at one depth keeps hitting its own line. r10 is
// volatile and carries no argument in either ABI. SysV passes a nested
// function's static chain in it, but the wrapper's ordinary call into the
// original never preserved that anyway.
constexpr uint8_t kCountEntry[] = {
    0x49, 0x89, 0xE2,                         // mov r10, rsp
    0x49, 0xC1, 0xEA, 0x08,                   // shr r10, 8
    0x49, 0x31, 0xE2,                         // xor r10, rsp
    0x49, 0xC1, 0xEA, 0x06,                   // shr r10, 6
    0x41, 0x81, 0xE2, 0xC0, 0x03, 0x00, 0x00, // and r10d, 0x3C0
    0x4D, 0x01, 0xDA,                         // add r10, r11
    0xF0, 0x49, 0xFF, 0x02,                   // lock inc qword [r10]
};
static_assert(0x3C0 ==
              (entry_counts::kShards - 1) * sizeof(entry_counts::shard));
static_assert(offsetof(entry_counts, shards) == 0);
// Replaces the count in the stub of a permanent hook, which nothing ever
// waits to drain. Same length, so the stub layout does not change.
constexpr uint8_t kCountNone[] = {
    0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, // nop dword [rax+rax+0]
    0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x0F, 0x1F, 0x40, 0x00,                         // nop dword [rax+0]
};
static_assert(sizeof(kCountNone) == sizeof(kCountEntry));
constexpr uint8_t kJmpRip[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
constexpr size_t kCtxOffset = sizeof(kEndbr64) + sizeof(kMovR11);
constexpr size_t kCountOffset = kCtxOffset + sizeof(uintptr_t);
constexpr size_t kJmpOffset = kCountOffset + sizeof(kCountEntry);
constexpr size_t kWrapperOffset = kJmpOffset + sizeof(kJmpRip);
static_assert(kWrapperOffset == kStubCodeSize);
static_assert(kWrapperOffset + sizeof(uintptr_t) == kStubSize);
} // namespace

// endbr64 is a NOP without IBT and the required landing pad with it. The
// entry is counted before the jump because uninstall waits for it to drain.
stub_bytes create_absolute_jump(uintptr_t target_address, uintptr_t data,
                                bool counted) {
  stub_bytes bytes(kStubSize);
  uint8_t *out = bytes.data();
  std::memcpy(out, kEndbr64, sizeof(kEndbr64));
  std::memcpy(out + sizeof(kEndbr64), kMovR11, sizeof(kMovR11));
  std::memcpy(out + kCtxOffset, &data, sizeof(data));
  std::memcpy(out + kCountOffset, counted ? kCountEntry : kCountNone,
              sizeof(kCountEntry));
  std::memcpy(out + kJmpOffset, kJmpRip, sizeof(kJmpRip));
  std::memcpy(out + kWrapperOffset, &target_address, sizeof(target_address));
  return bytes;
}

bool needs_ibt_landing(uintptr_t address,
                       std::span<const uint8_t> original_prefix) {
  std::array<uint8_t, ZYDIS_MAX_INSTRUCTION_LENGTH> bytes{};
  if (original_prefix.empty()) {
    const size_t available = read_code_prefix(address, bytes);
    original_prefix = {bytes.data(), available};
  }
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  ZydisDecodedInstruction instruction{};
  // Decoding also recognizes endbr64 with redundant instruction prefixes.
  return ZYAN_FAILED(ZydisDecoderDecodeInstruction(
             &decoder, nullptr, original_prefix.data(), original_prefix.size(),
             &instruction)) ||
         instruction.mnemonic == ZYDIS_MNEMONIC_ENDBR64;
}

// Follows pure forwarders to the code they reach, so hooking one hooks its
// destination: jmp rel32; a backward jmp rel8 (an MSVC hotpatch jump into
// the padding above the function); jmp [rip + disp] import thunks; each
// optionally after endbr64 or `push rbp; mov rbp, rsp; pop rbp`, with an
// optional BND prefix. A forward jmp rel8 is more likely a rotated loop
// entry inside this function than a forwarder, so it is not followed.
uintptr_t maybe_resolve_jump(uintptr_t address) {
  // Bounds a cycle of forwarders; real chains are one or two hops.
  constexpr int kMaxHops = 8;
  for (int hop = 0; hop < kMaxHops; ++hop) {
    std::array<uint8_t, 16> bytes{};
    const size_t available = read_code_prefix(address, bytes);
    size_t at = 0;
    if (available >= 4 && std::memcmp(bytes.data(), kEndbr64, 4) == 0) {
      at = 4;
    } else if (available >= 5 && bytes[0] == 0x55 && bytes[1] == 0x48 &&
               bytes[2] == 0x89 && bytes[3] == 0xE5 && bytes[4] == 0x5D) {
      at = 5;
    }
    if (at < available && bytes[at] == 0xF2)
      ++at;
    uintptr_t next = 0;
    if (at + 5 <= available && bytes[at] == 0xE9) {
      int32_t rel = 0;
      std::memcpy(&rel, bytes.data() + at + 1, sizeof(rel));
      next = address + at + 5 + rel;
      // Both bare and IBT landings must stay the target for stacking.
      if (const auto *anchor = anchor_at(next, Arch::kX86_64);
          anchor != nullptr && anchor->target == address)
        return address;
    } else if (at + 2 <= available && bytes[at] == 0xEB &&
               static_cast<int8_t>(bytes[at + 1]) < 0) {
      next = address + at + 2 + static_cast<int8_t>(bytes[at + 1]);
    } else if (at + 6 <= available && bytes[at] == 0xFF &&
               bytes[at + 1] == 0x25) {
      int32_t disp = 0;
      std::memcpy(&disp, bytes.data() + at + 2, sizeof(disp));
      if (!read_code_bytes(address + at + 6 + disp,
                           {reinterpret_cast<uint8_t *>(&next), sizeof(next)}))
        return address;
      // A far landing also remains the target when its anchor owns it.
      if (const auto *anchor = anchor_at(next, Arch::kX86_64);
          anchor != nullptr && anchor->target == address)
        return address;
      // A lazily bound PLT slot still points back into its own module's
      // stubs; following it would hook the resolver path, not the import.
      const auto region = query_memory(address);
      if (next == 0 || !region ||
          (next >= region->start && next - region->start < region->size))
        return address;
    } else {
      return address;
    }
    address = next;
  }
  return address;
}

} // namespace x64
} // namespace detail
} // namespace spud

// Translates DWARF CFI and LSDAs for copied code into .eh_frame records.
// The platform files look frames up and register what this produces.
#include "memory/unwind/dwarf.h"

#include "detour/detour_impl.h"
#include "memory/readable.h"
#include "memory/safe_patch.h"
#include "memory/unwind.h"

#include <spud/arch.h>
#include <spud/diagnostics.h>

#if SPUD_UNWIND_SUPPORT && (SPUD_OS_LINUX || SPUD_OS_APPLE)
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <Zydis/Zydis.h>

namespace spud::detail {
namespace dwarf {
namespace {

struct reader {
  std::span<const uint8_t> bytes;
  uintptr_t address = 0;
  size_t at = 0;
  bool valid = true;
  std::vector<std::pair<uintptr_t, std::vector<uint8_t>>> *metadata = nullptr;

  uint64_t fixed(size_t width) {
    if (width > 8 || at > bytes.size() || width > bytes.size() - at) {
      valid = false;
      return 0;
    }
    uint64_t value = 0;
    std::memcpy(&value, bytes.data() + at, width);
    at += width;
    return value;
  }
  uint64_t uleb() {
    uint64_t value = 0;
    for (unsigned shift = 0; shift < 64 && valid; shift += 7) {
      const auto byte = fixed(1);
      if (shift == 63 && (byte & 0x7E) != 0) {
        valid = false;
        break;
      }
      value |= (byte & 0x7F) << shift;
      if ((byte & 0x80) == 0) {
        return value;
      }
    }
    valid = false;
    return 0;
  }
  int64_t sleb() {
    uint64_t value = 0;
    for (unsigned shift = 0; shift < 64 && valid; shift += 7) {
      const auto byte = fixed(1);
      if (shift == 63 && (byte & 0x7F) != 0 && (byte & 0x7F) != 0x7F) {
        valid = false;
        break;
      }
      value |= (byte & 0x7F) << shift;
      if ((byte & 0x80) == 0) {
        if (shift < 57 && (byte & 0x40) != 0) {
          value |= ~uint64_t{0} << (shift + 7);
        }
        return static_cast<int64_t>(value);
      }
    }
    valid = false;
    return 0;
  }
  bool skip(size_t n) {
    if (at > bytes.size() || n > bytes.size() - at) {
      valid = false;
      return false;
    }
    at += n;
    return true;
  }
};

size_t encoded_width(uint8_t encoding) {
  switch (encoding & 15) {
  case 0:
  case 8:
    return 8;
  case 2:
  case 10:
    return 2;
  case 3:
  case 11:
    return 4;
  case 4:
  case 12:
    return 8;
  default:
    return 0;
  }
}

uintptr_t encoded(reader &r, uint8_t encoding, uintptr_t text = 0,
                  uintptr_t data = 0, uintptr_t function = 0) {
  const uintptr_t field = r.address + r.at;
  uint64_t value = 0;
  if (encoding == 0xFF) {
    return 0;
  }
  const auto width = encoded_width(encoding);
  if (width != 0) {
    value = r.fixed(width);
    if ((encoding & 8) != 0 && width < 8 &&
        (value & (uint64_t{1} << (width * 8 - 1))) != 0) {
      value |= ~uint64_t{0} << (width * 8);
    }
  } else if ((encoding & 15) == 1) {
    value = r.uleb();
  } else if ((encoding & 15) == 9) {
    value = static_cast<uint64_t>(r.sleb());
  } else {
    r.valid = false;
  }
  // A null encoded type is catch-all even with a relative/indirect encoding.
  if (value == 0)
    return 0;
  switch (encoding & 0x70) {
  case 0:
    break;
  case 0x10:
    value += field;
    break;
  case 0x20:
    if (text == 0)
      r.valid = false;
    value += text;
    break;
  case 0x30:
    if (data == 0)
      r.valid = false;
    value += data;
    break;
  case 0x40:
    if (function == 0)
      r.valid = false;
    value += function;
    break;
  default:
    r.valid = false;
  }
  if ((encoding & 0x80) != 0 && value != 0) {
    const uintptr_t address = value;
    if (!read_code_bytes(address, {reinterpret_cast<uint8_t *>(&value), 8})) {
      r.valid = false;
    } else if (r.metadata != nullptr) {
      std::vector<uint8_t> bytes(8);
      std::memcpy(bytes.data(), &value, 8);
      r.metadata->emplace_back(address, std::move(bytes));
    }
  }
  return value;
}

// One guarded read covers a typical record. A longer one reads the rest.
bool read_record(uintptr_t address, std::vector<uint8_t> &bytes) {
  bytes.resize(128);
  bytes.resize(read_code_prefix(address, bytes));
  uint32_t length = 0;
  if (bytes.size() < 4)
    return false;
  std::memcpy(&length, bytes.data(), 4);
  if (length < 4 || length > kMetadataLimit - 4)
    return false;
  const size_t have = bytes.size();
  bytes.resize(length + 4);
  return have >= bytes.size() ||
         read_code_bytes(address + have, std::span(bytes).subspan(have));
}

} // namespace

// CIE rules at function entry: CFA is SP before the call, and x86-64 finds
// its return address at CFA - 8.
std::vector<uint8_t> entry_rules() {
  writer initial;
  initial.fixed(12, 1);
  initial.uleb(kArm64 ? 31 : 7);
  initial.uleb(kArm64 ? 0 : 8);
  if (!kArm64) {
    initial.fixed(0x90, 1);
    initial.uleb(1);
  }
  return std::move(initial.bytes);
}

std::optional<arm64_branch> decode_arm64_branch(uint32_t word) {
  unsigned bits = 0, shift = 5;
  if ((word & 0xFC000000u) == 0x14000000u) {
    bits = 26;
    shift = 0;
  } else if ((word & 0xFF000000u) == 0x54000000u ||
             (word & 0x7E000000u) == 0x34000000u) {
    bits = 19;
  } else if ((word & 0x7E000000u) == 0x36000000u) {
    bits = 14;
  } else if ((word & 0x7E000000u) == 0x74000000u) {
    bits = 9;
  } else {
    return std::nullopt;
  }
  const uint32_t value = (word >> shift) & ((1u << bits) - 1);
  return arm64_branch{static_cast<int64_t>(value ^ (1u << (bits - 1))) -
                          (1u << (bits - 1)),
                      bits == 26};
}

namespace {

// Parses an FDE and its CIE into `f`. `extent_only` stops after the FDE's
// initial location and range and dereferences no pointer, so it can run on
// records read in place.
bool parse_records(std::span<const uint8_t> fde_bytes, uintptr_t fde,
                   std::span<const uint8_t> cie_bytes, uintptr_t cie, frame &f,
                   bool extent_only) {
  auto *metadata = extent_only ? nullptr : &f.metadata;
  reader fr{fde_bytes, fde, 8, true, metadata};
  reader cr{cie_bytes, cie, 4, true, metadata};
  if (cr.fixed(4) != 0) {
    return false;
  }
  const unsigned version = cr.fixed(1);
  if (version != 1 && version != 3 && version != 4) {
    return false;
  }
  std::array<uint8_t, 8> augmentation{};
  size_t letters = 0;
  while (cr.valid) {
    const uint8_t c = cr.fixed(1);
    if (c == 0) {
      break;
    }
    if (letters == augmentation.size())
      return false;
    augmentation[letters++] = c;
  }
  if (version == 4 && (cr.fixed(1) != 8 || cr.fixed(1) != 0)) {
    return false;
  }
  f.code_align = cr.uleb();
  f.data_align = cr.sleb();
  f.ra = version == 1 ? cr.fixed(1) : cr.uleb();
  uint8_t pc_encoding = 0, lsda_encoding = 0xFF;
  if (letters != 0) {
    if (augmentation[0] != 'z') {
      return false;
    }
    const size_t length = cr.uleb();
    const size_t aug_begin = cr.at;
    if (length > cr.bytes.size() - aug_begin) {
      return false;
    }
    for (size_t i = 1; i < letters; ++i) {
      switch (augmentation[i]) {
      case 'R':
        pc_encoding = cr.fixed(1);
        break;
      case 'L':
        lsda_encoding = cr.fixed(1);
        break;
      case 'P': {
        auto encoding = static_cast<uint8_t>(cr.fixed(1));
        if (extent_only)
          encoding &= 0x7F;
        f.personality = encoded(cr, encoding, f.text, f.data);
        break;
      }
      case 'S':
        f.signal = true;
        break;
      case 'B':
        if (!kArm64)
          return false;
        f.b_key = true;
        break;
      case 'G':
        if (!kArm64)
          return false;
        f.mte_tagged = true;
        break;
      default:
        return false;
      }
    }
    if (cr.at > aug_begin + length || !cr.valid) {
      return false;
    }
    cr.at = aug_begin + length;
  }
  f.begin = encoded(fr, pc_encoding, f.text, f.data);
  f.length = encoded(fr, pc_encoding & 15);
  if (extent_only)
    return fr.valid && cr.valid;
  if (letters != 0) {
    const size_t length = fr.uleb();
    const size_t aug_begin = fr.at;
    if (length > fr.bytes.size() - aug_begin) {
      return false;
    }
    if (lsda_encoding != 0xFF) {
      f.lsda = encoded(fr, lsda_encoding, f.text, f.data, f.begin);
    }
    if (fr.at > aug_begin + length) {
      return false;
    }
    fr.at = aug_begin + length;
  }
  if (!fr.valid || !cr.valid || f.code_align == 0)
    return false;
  f.initial.assign(cie_bytes.begin() + cr.at, cie_bytes.end());
  f.instructions.assign(fde_bytes.begin() + fr.at, fde_bytes.end());
  f.instructions_address = fde + fr.at;
  f.pc_encoding = pc_encoding;
  return true;
}

} // namespace

bool parse_extent(std::span<const uint8_t> fde_bytes, uintptr_t fde,
                  std::span<const uint8_t> cie_bytes, uintptr_t cie, frame &f) {
  return parse_records(fde_bytes, fde, cie_bytes, cie, f, true);
}

bool read_fde(uintptr_t fde, frame &f) {
  std::vector<uint8_t> fde_bytes, cie_bytes;
  if (!read_record(fde, fde_bytes) || fde_bytes.size() < 8)
    return false;
  uint32_t pointer = 0;
  std::memcpy(&pointer, fde_bytes.data() + 4, 4);
  const uintptr_t cie = fde + 4 - pointer;
  if (pointer == 0)
    return false;
  // A CIE usually precedes its FDEs. When it is close, one read covers it
  // and the records in between, which nothing writes, so preparation and
  // commit each read the pair as one region.
  std::span<const uint8_t> cie_record;
  if (cie < fde && fde - cie <= 256) {
    cie_bytes.resize(fde - cie);
    uint32_t length = 0;
    if (cie_bytes.size() < 4 || !read_code_bytes(cie, cie_bytes))
      return false;
    std::memcpy(&length, cie_bytes.data(), 4);
    if (length < 4 || length > cie_bytes.size() - 4)
      return false;
    cie_record = std::span(cie_bytes).first(size_t{length} + 4);
  } else if (read_record(cie, cie_bytes)) {
    cie_record = cie_bytes;
  }
  if (cie_record.empty() ||
      !parse_records(fde_bytes, fde, cie_record, cie, f, false))
    return false;
  f.metadata.emplace_back(fde, std::move(fde_bytes));
  f.metadata.emplace_back(cie, std::move(cie_bytes));
  return true;
}

namespace {

struct register_rule {
  uint64_t reg;
  unsigned opcode;
  uint64_t source = 0;
  std::vector<uint8_t> expression = {};
  std::vector<uint8_t> bytes = {};
};

struct cfa_state {
  uint64_t reg = kArm64 ? 31 : 7;
  int64_t offset = kArm64 ? 0 : 8;
  std::vector<uint8_t> expression;
  std::vector<register_rule> registers;
};

struct cfi_op {
  size_t pc;
  std::vector<uint8_t> bytes;
  cfa_state cfa;
};

// Track rules that read live registers or SP. Offset rules depend only on
// CFA and remain unchanged when temporary spills adjust that CFA.
bool parse_cfi(std::span<const uint8_t> bytes, frame &f, cfa_state &cfa,
               std::vector<cfa_state> &saved, std::vector<cfi_op> &ops,
               bool initial = false) {
  reader r{bytes, initial ? 0 : f.instructions_address, 0, true, &f.metadata};
  const auto original = cfa.registers;
  auto erase = [&](uint64_t reg) {
    std::erase_if(cfa.registers,
                  [&](const auto &rule) { return rule.reg == reg; });
  };
  auto restore = [&](uint64_t reg) {
    erase(reg);
    for (const auto &rule : original)
      if (rule.reg == reg)
        cfa.registers.push_back(rule);
  };
  size_t pc = 0;
  while (r.valid && r.at < bytes.size()) {
    const size_t begin = r.at;
    const unsigned op = r.fixed(1);
    std::optional<register_rule> rule;
    uint64_t advance = 0;
    if ((op & 0xC0) == 0x40) {
      advance = op & 0x3F;
    } else if ((op & 0xC0) == 0x80) {
      erase(op & 0x3F);
      r.uleb();
    } else if ((op & 0xC0) == 0xC0) {
      restore(op & 0x3F);
    } else {
      switch (op) {
      case 0:
        continue;
      case 1:
        if (initial)
          return false;
        {
          const uintptr_t location =
              encoded(r, f.pc_encoding, f.text, f.data, f.begin);
          if (!r.valid || location < f.begin || location - f.begin < pc ||
              location - f.begin > f.length)
            return false;
          pc = location - f.begin;
        }
        continue;
      case 2:
        advance = r.fixed(1);
        break;
      case 3:
        advance = r.fixed(2);
        break;
      case 4:
        advance = r.fixed(4);
        break;
      case 5:
      case 20:
        erase(r.uleb());
        r.uleb();
        break;
      case 7:
        erase(r.uleb());
        break;
      case 6:
        restore(r.uleb());
        break;
      case 8:
      case 9: {
        const auto reg = r.uleb();
        rule = register_rule{reg, op, op == 8 ? reg : r.uleb()};
      } break;
      case 46:
        r.uleb();
        break;
      case 10:
        saved.push_back(cfa);
        break;
      case 11:
        if (saved.empty())
          return false;
        cfa = saved.back();
        saved.pop_back();
        break;
      case 12:
        cfa.reg = r.uleb();
        {
          const auto value = r.uleb();
          if (value > std::numeric_limits<int64_t>::max())
            return false;
          cfa.offset = static_cast<int64_t>(value);
        }
        cfa.expression.clear();
        break;
      case 13:
        cfa.reg = r.uleb();
        cfa.expression.clear();
        break;
      case 14: {
        const auto value = r.uleb();
        if (value > std::numeric_limits<int64_t>::max())
          return false;
        cfa.offset = static_cast<int64_t>(value);
      } break;
      case 15: {
        const size_t length = r.uleb();
        const size_t begin_expression = r.at;
        if (!r.skip(length))
          return false;
        cfa.expression.assign(bytes.begin() + begin_expression,
                              bytes.begin() + r.at);
        break;
      }
      case 16:
      case 22: {
        const auto reg = r.uleb();
        const size_t length = r.uleb(), start = r.at;
        if (!r.skip(length))
          return false;
        rule = register_rule{
            reg, op, 0, {bytes.begin() + start, bytes.begin() + r.at}};
      } break;
      case 17:
      case 21:
        erase(r.uleb());
        r.sleb();
        break;
      case 18:
        cfa.reg = r.uleb();
        if (__builtin_mul_overflow(r.sleb(), f.data_align, &cfa.offset))
          return false;
        cfa.expression.clear();
        break;
      case 19:
        if (__builtin_mul_overflow(r.sleb(), f.data_align, &cfa.offset))
          return false;
        break;
      case 45:
        break; // AArch64 negate_ra_state.
      case 47:
        erase(r.uleb());
        r.uleb();
        break; // GNU_negative_offset_extended.
      default:
        // This includes AArch64 negate_ra_state_with_pc (0x2C). Its row
        // location names the signing instruction, an authentication modifier
        // that a moved row cannot reproduce.
        return false;
      }
    }
    if (!r.valid || (initial && advance != 0) ||
        advance > (std::numeric_limits<size_t>::max() - pc) / f.code_align) {
      return false;
    }
    if (advance != 0 || (op & 0xC0) == 0x40 || (op >= 2 && op <= 4)) {
      pc += advance * f.code_align;
    } else {
      if (rule) {
        erase(rule->reg);
        rule->bytes.assign(bytes.begin() + begin, bytes.begin() + r.at);
        cfa.registers.push_back(std::move(*rule));
      }
      ops.push_back({pc, {bytes.begin() + begin, bytes.begin() + r.at}, cfa});
    }
  }
  return r.valid;
}

void register_value(writer &out, uint64_t reg, int64_t offset,
                    const stack_adjustment &stack) {
  const uint64_t sp = kArm64 ? 31 : 7;
  if (const auto *slot = stack.saved_register(reg)) {
    out.fixed(0x70 + sp, 1); // Original register value in the spill slot.
    out.sleb(slot->stack_offset);
    if (slot->saved_width == stack_adjustment::register_slot::width::kWord) {
      out.fixed(0x94, 1); // Only AX was exchanged; its high bits stay live.
      out.fixed(2, 1);
      out.fixed(0x70 + reg, 1);
      out.sleb(0);
      out.fixed(0x10, 1);
      out.uleb(0xFFFF);
      out.fixed(0x20, 1); // not; and; or.
      out.fixed(0x1A, 1);
      out.fixed(0x21, 1);
    } else {
      out.fixed(6, 1); // DW_OP_deref.
    }
    if (offset != 0) {
      out.fixed(0x11, 1);
      out.sleb(offset);
      out.fixed(0x22, 1);
    }
  } else {
    out.fixed(reg < 32 ? 0x70 + reg : 0x92, 1);
    if (reg >= 32)
      out.uleb(reg);
    out.sleb(offset);
  }
}

// Adjust each live-register read before evaluating the original arithmetic
// or dereference. Changed LEB widths also move relative branch destinations.
bool translate_expression(std::span<const uint8_t> bytes,
                          const stack_adjustment &stack, writer &out,
                          bool cfa) {
  reader r{bytes};
  struct branch {
    size_t field, end;
    int64_t target;
  };
  std::vector<branch> branches;
  std::vector<std::pair<size_t, size_t>> boundaries;
  while (r.valid && r.at < bytes.size()) {
    const size_t begin = r.at, output = out.bytes.size();
    boundaries.emplace_back(begin, output);
    const unsigned op = r.fixed(1);
    if ((op >= 0x70 && op <= 0x8F) || op == 0x92) {
      const uint64_t reg = op == 0x92 ? r.uleb() : op - 0x70;
      int64_t offset = r.sleb();
      if (reg == (kArm64 ? 31u : 7u) &&
          __builtin_add_overflow(offset, stack.displacement, &offset))
        return false;
      register_value(out, reg, offset, stack);
      continue;
    }
    if (op == 0x28 || op == 0x2F) {
      const int16_t delta = static_cast<int16_t>(r.fixed(2));
      out.fixed(op, 1);
      out.fixed(0, 2);
      branches.push_back({output + 1, output + 3, int64_t(r.at) + delta});
      continue;
    }
    if (op >= 0x30 && op <= 0x4F) {
      // DW_OP_lit0..31.
    } else if ((op >= 0x19 && op <= 0x22) || (op >= 0x24 && op <= 0x27) ||
               (op >= 0x29 && op <= 0x2E) || op == 6 ||
               (op >= 0x12 && op <= 0x14) || (op >= 0x16 && op <= 0x18) ||
               op == 0x96 || op == 0x9F || (!cfa && op == 0x9C)) {
      // Arithmetic, stack operations, dereference, and CFA in register rules.
    } else {
      switch (op) {
      case 3:
      case 0x0E:
      case 0x0F:
        r.fixed(8);
        break;
      case 8:
      case 9:
      case 0x15:
      case 0x94:
      case 0x95:
        r.fixed(1);
        break;
      case 0x0A:
      case 0x0B:
        r.fixed(2);
        break;
      case 0x0C:
      case 0x0D:
        r.fixed(4);
        break;
      case 0x10:
      case 0x23:
        r.uleb();
        break;
      case 0x11:
        r.sleb();
        break;
      default:
        // Calls, entry values, frame-base reads and vendor operations can
        // hide register reads outside this expression. Do not guess them.
        return false;
      }
    }
    if (!r.valid)
      return false;
    out.append(bytes.subspan(begin, r.at - begin));
  }
  if (!r.valid)
    return false;
  boundaries.emplace_back(bytes.size(), out.bytes.size());
  for (const auto &branch : branches) {
    const auto destination = std::find_if(
        boundaries.begin(), boundaries.end(), [&](const auto &boundary) {
          return branch.target >= 0 &&
                 boundary.first == uint64_t(branch.target);
        });
    if (destination == boundaries.end())
      return false;
    const int64_t delta = int64_t(destination->second) - int64_t(branch.end);
    if (delta < INT16_MIN || delta > INT16_MAX)
      return false;
    const int16_t encoded = static_cast<int16_t>(delta);
    std::memcpy(out.bytes.data() + branch.field, &encoded, sizeof(encoded));
  }
  return true;
}

// The source rows, parsed once per preparation and replayed for each range.
struct cfi_program {
  cfa_state initial;
  std::vector<cfi_op> ops;
};

bool parse_frame_cfi(frame &f, cfi_program &program) {
  cfa_state cfa;
  std::vector<cfa_state> saved;
  std::vector<cfi_op> initial;
  if (!parse_cfi(f.initial, f, cfa, saved, initial, true) || !saved.empty() ||
      !parse_cfi(f.instructions, f, cfa, saved, program.ops))
    return false;
  if (!initial.empty())
    program.initial = initial.back().cfa;
  return true;
}

bool translate_cfi(const cfi_program &program, size_t copied,
                   std::span<const std::pair<uintptr_t, uintptr_t>> offsets,
                   std::span<const stack_adjustment> stack, size_t begin_offset,
                   size_t source_offset, bool veneer, writer &out) {
  const auto &ops = program.ops;
  cfa_state cfa = program.initial;
  size_t pc = 0, adjustment = 0;
  stack_adjustment adjustment_state{0, 0};
  auto advance_to = [&](size_t next) {
    if (next != pc) {
      out.fixed(4, 1); // DW_CFA_advance_loc4, normalized code alignment 1.
      out.fixed(next - pc, 4);
      pc = next;
    }
  };
  auto adjusted_cfa = [&] {
    const auto displacement = adjustment_state.displacement;
    if (!cfa.expression.empty()) {
      if (displacement == 0 && !adjustment_state.has_saved_registers()) {
        out.fixed(15, 1);
        out.uleb(cfa.expression.size());
        out.append(cfa.expression);
      } else {
        // The common breg(SP, offset) CFA can be normalized to def_cfa.
        reader expression{cfa.expression};
        const unsigned op = expression.fixed(1);
        const uint64_t reg = op == 0x92 ? expression.uleb() : op - 0x70;
        const int64_t offset = expression.sleb();
        int64_t adjusted = 0;
        if (((op >= 0x70 && op <= 0x8F) || op == 0x92) && expression.valid &&
            expression.at == cfa.expression.size() &&
            reg == (kArm64 ? 31u : 7u) &&
            !__builtin_add_overflow(offset, displacement, &adjusted) &&
            adjusted >= 0) {
          out.fixed(12, 1);
          out.uleb(reg);
          out.uleb(adjusted);
        } else {
          writer translated;
          if (!translate_expression(cfa.expression, adjustment_state,
                                    translated, true))
            return false;
          out.fixed(15, 1);
          out.uleb(translated.bytes.size());
          out.append(translated.bytes);
        }
      }
    } else if (cfa.reg == (kArm64 ? 31u : 7u)) {
      int64_t adjusted = 0;
      if (__builtin_add_overflow(cfa.offset, displacement, &adjusted) ||
          adjusted < 0)
        return false;
      out.fixed(12, 1);
      out.uleb(cfa.reg);
      out.uleb(adjusted);
    } else {
      writer expression;
      register_value(expression, cfa.reg, cfa.offset, adjustment_state);
      out.fixed(15, 1);
      out.uleb(expression.bytes.size());
      out.append(expression.bytes);
    }
    for (const auto &rule : cfa.registers) {
      const bool borrowed =
          adjustment_state.saved_register(rule.source) != nullptr;
      if (displacement == 0 && !adjustment_state.has_saved_registers()) {
        out.append(rule.bytes);
      } else if (rule.opcode == 8 || rule.opcode == 9) {
        if (!borrowed && rule.source != (kArm64 ? 31u : 7u)) {
          out.append(rule.bytes);
          continue;
        }
        writer expression;
        register_value(expression, rule.source,
                       rule.source == (kArm64 ? 31u : 7u) ? displacement : 0,
                       adjustment_state);
        out.fixed(22, 1);
        out.uleb(rule.reg);
        out.uleb(expression.bytes.size());
        out.append(expression.bytes);
      } else {
        writer expression;
        if (!translate_expression(rule.expression, adjustment_state, expression,
                                  false))
          return false;
        out.fixed(rule.opcode, 1);
        out.uleb(rule.reg);
        out.uleb(expression.bytes.size());
        out.append(expression.bytes);
      }
    }
    return true;
  };
  for (const auto &op : ops) {
    if (op.pc > begin_offset + (veneer ? source_offset : copied))
      break;
    const size_t mapped =
        veneer || op.pc <= begin_offset
            ? 0
            : relocated_location_at(offsets, op.pc - begin_offset);
    while (!veneer && adjustment < stack.size() &&
           stack[adjustment].offset < mapped) {
      advance_to(stack[adjustment].offset);
      adjustment_state = stack[adjustment++];
      if (!adjusted_cfa())
        return false;
    }
    advance_to(mapped);
    out.append(op.bytes);
    cfa = op.cfa;
    if ((adjustment_state.displacement != 0 ||
         adjustment_state.has_saved_registers()) &&
        !adjusted_cfa())
      return false;
  }
  while (!veneer && adjustment < stack.size()) {
    advance_to(stack[adjustment].offset);
    adjustment_state = stack[adjustment++];
    if (!adjusted_cfa())
      return false;
  }
  return true;
}

// Exception entries can live after the normal RET, or in a separate part
// with its own FDE such as GCC's .cold, and escape the relocator's local
// scan. Their paths must not re-enter bytes that the installed stub
// overwrites. Branches are followed into any unwind entry they reach; code
// with no entry is trusted, as nothing describes it as part of a function.
bool validate_landing_path(frame &f, uintptr_t source, size_t copied,
                           uintptr_t landing) {
  if (landing >= source && landing - source < copied)
    return note_failure("handler_path_enters_patch", landing);
  // A tail call into another function walks that function too, up to this
  // bound. Real cold parts are one or two per function.
  constexpr size_t kMaxFragments = 16;
  // One read per fragment. Call sites and veneers can share a handler, so
  // each instruction is validated and snapshotted once for this frame.
  auto fragment_at = [&](uintptr_t pc, code_fragment *&found) {
    found = nullptr;
    if (f.fragments.empty())
      f.fragments.push_back({f.begin, f.length, {}, {}});
    for (auto &fragment : f.fragments)
      if (pc >= fragment.begin && pc - fragment.begin < fragment.length)
        found = &fragment;
    if (found == nullptr) {
      const auto extent = covering_extent(pc);
      if (!extent)
        return true;
      if (f.fragments.size() == kMaxFragments)
        return note_failure("handler_path_too_long", pc);
      f.fragments.push_back(
          {extent->first, extent->second - extent->first, {}, {}});
      found = &f.fragments.back();
    }
    if (found->length > kMetadataLimit)
      return note_failure("handler_path_too_long", pc);
    if (found->visited.empty()) {
      found->code.resize(found->length);
      found->code.resize(f.code.read(found->begin, found->code));
      found->visited.assign(found->length, false);
    }
    return true;
  };
  std::vector<uintptr_t> pending{landing};
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  auto extend = [&](uintptr_t target) {
    if (target >= source && target - source < copied)
      return note_failure("handler_path_enters_patch", target);
    pending.push_back(target);
    return true;
  };
  while (!pending.empty()) {
    const uintptr_t pc = pending.back();
    pending.pop_back();
    code_fragment *fragment = nullptr;
    if (!fragment_at(pc, fragment))
      return false;
    if (fragment == nullptr)
      continue;
    // Only the source function's own body must prove every transfer. In
    // other entries a register jump is a tail call or a dispatch that this
    // check used to trust outright, so it ends the path there.
    const bool own = fragment == &f.fragments.front();
    const size_t offset = pc - fragment->begin;
    if (fragment->visited[offset])
      continue;
    fragment->visited[offset] = true;
    const size_t remaining = fragment->length - offset;
    const uint8_t *bytes = fragment->code.data() + offset;
    const size_t available =
        offset < fragment->code.size()
            ? std::min<size_t>(fragment->code.size() - offset, kArm64 ? 4 : 15)
            : 0;
    size_t length = 0;
    bool terminal = false, branch = false;
    uintptr_t target = 0;
    if (kArm64) {
      if (available < 4)
        return note_failure("handler_path_undecodable", pc);
      uint32_t word;
      std::memcpy(&word, bytes, 4);
      length = 4;
      if (const auto direct = decode_arm64_branch(word)) {
        target = pc + direct->words * 4;
        branch = true;
        terminal = direct->unconditional;
      } else if ((word & 0xFFFFFC1Fu) == 0xD65F0000u) {
        terminal = true; // RET
      } else if ((word & 0xFE000000u) == 0xD6000000u &&
                 (word & 0xFFFFFC1Fu) != 0xD63F0000u) {
        // Unknown register transfers, including authenticated branches, have
        // no proven destination. Ordinary BLR returns to the next word.
        if (own)
          return note_failure("handler_path_indirect", pc);
        terminal = true;
      }
    } else {
      ZydisDecodedInstruction instruction;
      if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr, bytes,
                                                      available, &instruction)))
        return note_failure("handler_path_undecodable", pc);
      length = instruction.length;
      const auto category = instruction.meta.category;
      terminal = category == ZYDIS_CATEGORY_RET ||
                 category == ZYDIS_CATEGORY_UNCOND_BR;
      if (category == ZYDIS_CATEGORY_UNCOND_BR ||
          category == ZYDIS_CATEGORY_COND_BR) {
        if (instruction.raw.imm[0].is_relative) {
          target = pc + length + instruction.raw.imm[0].value.s;
          branch = true;
        } else if (own || category == ZYDIS_CATEGORY_COND_BR) {
          return note_failure("handler_path_indirect", pc);
        }
      }
    }
    f.metadata.emplace_back(pc, std::vector<uint8_t>(bytes, bytes + length));
    if (branch && !extend(target))
      return false;
    if (!terminal && length < remaining && !extend(pc + length))
      return false;
  }
  return true;
}

// The source LSDA, read and checked once per preparation. Each range keeps
// only its call sites and the actions and types they reach.
struct lsda_table {
  struct call_site {
    size_t begin;
    size_t length;
    size_t landing;
    size_t action;
  };
  std::vector<uint8_t> bytes;
  uintptr_t lp_start = 0;
  uint8_t type_encoding = 0xFF;
  size_t types = 0;
  size_t actions = 0;
  std::vector<call_site> calls;
  // Extent of the bytes any range consumed, snapshotted after translation.
  size_t used = 0;
  bool exhausted = false;

  // Grows the copy to cover [0, end) where readable, doubling so a large
  // table still takes a few reads. Real tables are tens to hundreds of
  // bytes, so the first read usually covers everything.
  void extend(uintptr_t address, size_t end) {
    end = std::min(end, kMetadataLimit);
    if (end <= bytes.size() || exhausted)
      return;
    const size_t old = bytes.size();
    bytes.resize(std::min(kMetadataLimit, std::max(end, old * 2)));
    const size_t read =
        read_code_prefix(address + old, std::span(bytes).subspan(old));
    exhausted = old + read < bytes.size();
    bytes.resize(old + read);
  }
};

bool read_lsda(frame &f, size_t copied, size_t begin_offset, size_t patch_size,
               lsda_table &table) {
  if (f.lsda == 0)
    return true;
  auto &bytes = table.bytes;
  table.extend(f.lsda, 512);
  reader r{bytes, f.lsda, 0, true, &f.metadata};
  // Each extension can move the buffer under the reader.
  auto extend = [&](size_t length) {
    if (length > kMetadataLimit)
      return false;
    table.extend(f.lsda, r.at + length);
    r.bytes = bytes;
    return length <= bytes.size() - std::min(r.at, bytes.size());
  };
  const auto lp_encoding = static_cast<uint8_t>(r.fixed(1));
  table.lp_start = lp_encoding == 0xFF
                       ? f.begin
                       : encoded(r, lp_encoding, f.text, f.data, f.begin);
  table.type_encoding = static_cast<uint8_t>(r.fixed(1));
  if (table.type_encoding != 0xFF) {
    const size_t distance = r.uleb();
    if (!r.valid || !extend(distance))
      return false;
    table.types = r.at + distance;
    if (encoded_width(table.type_encoding) == 0)
      return false;
  }
  const auto call_encoding = static_cast<uint8_t>(r.fixed(1));
  if ((call_encoding & 0xF0) != 0)
    return false;
  const size_t table_length = r.uleb();
  if (!r.valid || !extend(table_length))
    return false;
  table.actions = r.at + table_length;
  while (r.valid && r.at < table.actions) {
    const size_t begin = encoded(r, call_encoding);
    const size_t length = encoded(r, call_encoding);
    const size_t landing = encoded(r, call_encoding);
    const size_t action = r.uleb();
    if (length > std::numeric_limits<size_t>::max() - begin)
      return false;
    const uintptr_t original_landing =
        landing == 0 ? 0 : table.lp_start + landing;
    const bool copied_call =
        begin >= begin_offset && begin + length <= begin_offset + copied;
    const bool copied_landing =
        original_landing >= f.begin + begin_offset &&
        original_landing - f.begin - begin_offset < copied;
    if (original_landing != 0 && (!copied_call || !copied_landing) &&
        !validate_landing_path(f, f.begin + begin_offset, patch_size,
                               original_landing))
      return false;
    table.calls.push_back({begin, length, landing, action});
  }
  if (!r.valid || r.at != table.actions)
    return false;
  table.used = table.actions;
  return true;
}

bool translate_lsda(frame &f, lsda_table &table, size_t copied,
                    std::span<const std::pair<uintptr_t, uintptr_t>> offsets,
                    const unwind_range &range, bool veneer, size_t begin_offset,
                    uintptr_t entry,
                    std::span<const size_t> instruction_offsets, writer &out) {
  if (f.lsda == 0)
    return true;
  const auto &bytes = table.bytes;
  const auto type_encoding = table.type_encoding;
  const size_t actions = table.actions, types = table.types;
  writer calls;
  std::vector<size_t> pending;
  for (const auto &[begin, length, landing, action] : table.calls) {
    if (begin >= begin_offset + copied || begin + length <= begin_offset ||
        (veneer && (begin_offset + range.source_offset < begin ||
                    begin_offset + range.source_offset >= begin + length))) {
      continue;
    }
    const size_t start =
        veneer ? 0
               : relocated_location_at(offsets, std::max(begin, begin_offset) -
                                                    begin_offset);
    const size_t end =
        veneer ? range.end - range.begin
               : relocated_location_at(
                     offsets, std::min(begin_offset + copied, begin + length) -
                                  begin_offset);
    uintptr_t destination = landing == 0 ? 0 : table.lp_start + landing;
    if (destination >= f.begin + begin_offset &&
        destination - f.begin - begin_offset < copied) {
      const size_t offset = destination - f.begin - begin_offset;
      if (!std::binary_search(instruction_offsets.begin(),
                              instruction_offsets.end(), offset))
        return false;
      destination = entry + relocated_location_at(offsets, offset);
    }
    calls.uleb(start);
    calls.uleb(end - start);
    calls.uleb(destination);
    calls.uleb(action);
    if (action != 0) {
      if (action - 1 > kMetadataLimit - actions)
        return false;
      pending.push_back(actions + action - 1);
    }
  }
  size_t action_end = actions, type_count = 0, specifications_end = types;
  std::vector<size_t> visited;
  while (!pending.empty()) {
    size_t position = pending.back();
    pending.pop_back();
    std::vector<size_t> path;
    for (;;) {
      // Distinct call sites can share an action suffix. Only a cycle on the
      // current chain is invalid, and every node is decoded at most once.
      if (std::find(path.begin(), path.end(), position) != path.end())
        return false;
      if (std::find(visited.begin(), visited.end(), position) != visited.end())
        break;
      if (visited.size() >= 256)
        return false;
      path.push_back(position);
      visited.push_back(position);
      table.extend(f.lsda, position + 20); // Two SLEB128 fields.
      reader a{bytes, f.lsda, position};
      const int64_t filter = a.sleb();
      const size_t displacement_at = a.at;
      const int64_t displacement = a.sleb();
      if (!a.valid)
        return false;
      if (filter < 0) {
        if (type_encoding == 0xFF ||
            filter == std::numeric_limits<int64_t>::min() ||
            static_cast<uint64_t>(-filter - 1) > kMetadataLimit - types)
          return false;
        // A list holds at most 256 types of at most two bytes each.
        table.extend(f.lsda, types + static_cast<size_t>(-filter - 1) + 520);
        reader specification{bytes, f.lsda,
                             types + static_cast<size_t>(-filter - 1)};
        size_t count = 0;
        for (;;) {
          const size_t type = specification.uleb();
          if (!specification.valid || type > 256 || ++count > 256)
            return false;
          type_count = std::max(type_count, type);
          if (type == 0)
            break;
        }
        specifications_end = std::max(specifications_end, specification.at);
      } else {
        type_count = std::max(type_count, static_cast<size_t>(filter));
      }
      action_end = std::max(action_end, a.at);
      if (displacement == 0)
        break;
      if (displacement < static_cast<int64_t>(actions) -
                             static_cast<int64_t>(displacement_at) ||
          displacement >= static_cast<int64_t>(kMetadataLimit) -
                              static_cast<int64_t>(displacement_at)) {
        return false;
      }
      position = static_cast<size_t>(static_cast<int64_t>(displacement_at) +
                                     displacement);
    }
  }
  if (type_count > 256 || (type_count != 0 && type_encoding == 0xFF))
    return false;
  writer suffix;
  suffix.fixed(1, 1); // ULEB call-site offsets.
  suffix.uleb(calls.bytes.size());
  suffix.append(calls.bytes);
  suffix.append({bytes.data() + actions, action_end - actions});
  const size_t width = encoded_width(type_encoding);
  if (type_count != 0 && type_count > types / width)
    return false;
  for (size_t i = type_count; i > 0; --i) {
    reader type{bytes, f.lsda, types - i * width, true, &f.metadata};
    const uintptr_t pointer =
        encoded(type, type_encoding, f.text, f.data, f.begin);
    if (!type.valid)
      return false;
    suffix.fixed(pointer, 8);
  }
  out.fixed(0, 1);
  out.fixed(0, 8); // Absolute landing addresses cover source and copied pads.
  out.fixed(type_encoding == 0xFF ? 0xFF : 0, 1);
  if (type_encoding != 0xFF)
    out.uleb(suffix.bytes.size());
  out.append(suffix.bytes);
  if (specifications_end > types)
    out.append({bytes.data() + types, specifications_end - types});
  table.used = std::max({table.used, action_end, types, specifications_end});
  return true;
}

// A complete CIE record for `f`'s rules, with absolute pointer encodings so
// the records need not sit near what they point to.
std::vector<uint8_t> cie_record(const frame &f, bool lsda) {
  writer cie;
  cie.fixed(0, 4);
  cie.fixed(0, 4);
  cie.fixed(3, 1);
  cie.fixed('z', 1);
  if (f.personality != 0)
    cie.fixed('P', 1);
  if (lsda)
    cie.fixed('L', 1);
  cie.fixed('R', 1);
  if (f.signal)
    cie.fixed('S', 1);
  if (f.b_key)
    cie.fixed('B', 1);
  if (f.mte_tagged)
    cie.fixed('G', 1);
  cie.fixed(0, 1);
  cie.uleb(1);
  cie.sleb(f.data_align);
  cie.uleb(f.ra);
  cie.uleb(1 + (f.personality != 0 ? 9 : 0) + (lsda ? 1 : 0));
  if (f.personality != 0) {
    cie.fixed(0, 1);
    cie.fixed(f.personality, 8);
  }
  if (lsda)
    cie.fixed(0, 1);
  cie.fixed(0, 1);
  cie.append(f.initial);
  cie.finish(0);
  return std::move(cie.bytes);
}

frame leaf_frame() {
  frame f;
  f.initial = entry_rules();
  return f;
}

} // namespace

std::vector<uint8_t> encode(std::span<const record> records) {
  writer out;
  std::vector<size_t> cies;
  std::vector<std::pair<size_t, std::span<const uint8_t>>> lsdas;
  for (const auto &r : records) {
    const auto shared =
        std::find_if(cies.begin(), cies.end(), [&](size_t offset) {
          return out.bytes.size() - offset >= r.cie.size() &&
                 std::equal(r.cie.begin(), r.cie.end(),
                            out.bytes.begin() + offset);
        });
    const size_t cie = shared != cies.end() ? *shared : out.bytes.size();
    if (shared == cies.end()) {
      cies.push_back(cie);
      out.append(r.cie);
    }
    const size_t fde = out.bytes.size();
    out.fixed(0, 4);
    out.fixed(fde + 4 - cie, 4);
    out.fixed(r.pc, 8);
    out.fixed(r.length, 8);
    out.uleb(!r.lsda.empty() ? 8 : 0);
    if (!r.lsda.empty()) {
      lsdas.emplace_back(out.bytes.size(), r.lsda);
      out.fixed(0, 8);
    }
    out.append(r.cfi);
    out.finish(fde);
  }
  out.fixed(0, 4);
  std::vector<std::pair<size_t, size_t>> pointers;
  for (const auto &[pointer, lsda] : lsdas) {
    pointers.emplace_back(pointer, out.bytes.size());
    out.append(lsda);
  }
  // The buffer is final now. Moving the vector keeps these addresses.
  for (const auto &[pointer, offset] : pointers) {
    const auto address = reinterpret_cast<uintptr_t>(out.bytes.data() + offset);
    std::memcpy(out.bytes.data() + pointer, &address, 8);
  }
  return std::move(out.bytes);
}

bool translate(uintptr_t allocation, size_t size, uintptr_t entry,
               uintptr_t source, size_t copied,
               const trampoline_buffer &trampoline,
               std::span<const size_t> instruction_offsets, size_t patch_size,
               source_view code, translation &out) {
  const std::span<const std::pair<uintptr_t, uintptr_t>> offsets =
      trampoline.relocation_offsets;
  const std::span<const unwind_range> ranges = trampoline.unwind_ranges;
  const std::span<const stack_adjustment> stack = trampoline.stack_adjustments;
  const size_t patch = patch_size != 0 ? patch_size : copied;
  frame f;
  f.code = code;
  bool found = false;
  if (source != 0 && !find_frame(source, copied, patch, f, found))
    return note_failure("unsupported_frame_record", source);
  auto add_record = [&](uintptr_t pc, size_t length, const frame &rules,
                        std::vector<uint8_t> instructions,
                        std::vector<uint8_t> exceptions) {
    out.records.push_back({pc, length, cie_record(rules, !exceptions.empty()),
                           std::move(instructions), std::move(exceptions)});
  };
  const size_t begin_offset = source - f.begin;
  lsda_table lsda;
  if (!read_lsda(f, copied, begin_offset, patch, lsda))
    return note_failure("unsupported_exception_table", f.lsda);
  // Code read for landing paths closes short gaps between snapshots, except
  // for the bytes this install overwrites.
  auto snapshot_source = [&] {
    if (f.lsda != 0)
      f.metadata.emplace_back(
          f.lsda, std::vector<uint8_t>(lsda.bytes.begin(),
                                       lsda.bytes.begin() + lsda.used));
    std::vector<std::pair<uintptr_t, std::span<const uint8_t>>> known;
    for (const auto &fragment : f.fragments) {
      const std::span<const uint8_t> code = fragment.code;
      const uintptr_t begin = fragment.begin, end = begin + code.size();
      if (source >= end || source + patch <= begin) {
        known.emplace_back(begin, code);
        continue;
      }
      if (source > begin)
        known.emplace_back(begin, code.first(source - begin));
      if (source + patch < end)
        known.emplace_back(source + patch,
                           code.subspan(source + patch - begin));
    }
    out.snapshots =
        unwind_registration::merge_snapshots(std::move(f.metadata), known);
  };
  if (!found) {
    // No frame rules: a leaf record, but an LSDA from the compact fallback
    // still has its landing paths checked.
    if (f.lsda != 0) {
      writer unused;
      const unwind_range range{0, copied, 0};
      if (!translate_lsda(f, lsda, copied, offsets, range, false, begin_offset,
                          entry, instruction_offsets, unused))
        return note_failure("unsupported_exception_table", f.lsda);
      snapshot_source();
    }
    add_record(allocation, size, leaf_frame(), {}, {});
    return true;
  }
  cfi_program program;
  if (ranges.empty() || !parse_frame_cfi(f, program))
    return note_failure("unsupported_cfi", f.instructions_address);
  if (entry > allocation)
    add_record(allocation, entry - allocation, leaf_frame(), {}, {});
  for (size_t i = 0; i < ranges.size(); ++i) {
    const auto &range = ranges[i];
    const bool fixed =
        i != 0 || range.frame_state == unwind_range::state::kFixed;
    writer cfi;
    writer exceptions;
    if (!translate_cfi(program, copied, offsets, stack, begin_offset,
                       range.source_offset, fixed, cfi))
      return note_failure("unsupported_cfi", f.instructions_address);
    if (!translate_lsda(f, lsda, copied, offsets, range, fixed, begin_offset,
                        entry, instruction_offsets, exceptions))
      return note_failure("unsupported_exception_table", f.lsda);
    add_record(entry + range.begin, range.end - range.begin, f,
               std::move(cfi.bytes), std::move(exceptions.bytes));
  }
  snapshot_source();
  return true;
}

} // namespace dwarf
} // namespace spud::detail
#endif

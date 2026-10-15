// Owns the arm64 jump stub and the relocation of an overwritten prologue
// into a trampoline.
#include "aarch64.h"

#include "detour/chain.h"
#include "detour/detour_impl.h"
#include "memory/readable.h"

#include "memory/safe_patch.h"
#include <spud/detour.h>

#include <capstone/capstone.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#if defined(__linux__) && defined(__aarch64__)
#include <sys/auxv.h>
#endif

namespace spud {
namespace detail {
namespace arm64 {

namespace {
// Stub words.
constexpr uint32_t kMovzX11 = 0xD2800000u | 11u;
constexpr uint32_t kMovkX11 = 0xF2800000u | 11u;
// Picks the entry shard into x10: stack bits 12-15 xor bits 20-23, as on
// x86-64, so threads on separate stacks usually count on separate lines.
// x10 is a temporary that carries no argument; the wrapper decrements the
// shard it names.
constexpr uint32_t kPickShard[] = {
    0x910003EAu, // mov x10, sp
    0xCA4A214Au, // eor x10, x10, x10, lsr #8
    0xD34C3D4Au, // ubfx x10, x10, #12, #4
    0x8B0A196Au, // add x10, x11, x10, lsl #6
};
static_assert(sizeof(entry_counts::shard) == 64 && entry_counts::kShards == 16);
static_assert(offsetof(entry_counts, shards) == 0);
// Counts the entry before jumping, because uninstall waits for it to drain.
// x16 and x17 are intra-procedure-call scratch registers. Neither form takes
// an offset, so the shard address is computed into x10 first. The LSE form
// avoids an exclusive retry loop on contended hooks; it is padded to the
// same four words so the stub size and boundaries do not depend on the CPU.
constexpr uint32_t kCountEntry[] = {
    0xC85FFD50u, // ldaxr x16, [x10]
    0x91000610u, // add x16, x16, #1
    0xC811FD50u, // stlxr w17, x16, [x10]
    0x35FFFFB1u, // cbnz w17, #-12
};
constexpr uint32_t kCountEntryLse[] = {
    0xD2800030u, // mov x16, #1
    0xF8F00150u, // ldaddal x16, x16, [x10]
    0xD503201Fu, // nop
    0xD503201Fu, // nop
};
// A permanent hook is never drained, so its stub neither picks a shard nor
// counts entries.
constexpr uint32_t kCountNone[] = {0xD503201Fu, 0xD503201Fu, 0xD503201Fu,
                                   0xD503201Fu};
static_assert(sizeof(kCountEntry) == sizeof(kCountEntryLse));
static_assert(sizeof(kCountEntry) == sizeof(kCountNone));
static_assert(sizeof(kPickShard) == sizeof(kCountNone));
// Word indexes: bti, four context moves, shard pick, count, four wrapper
// moves, br.
constexpr size_t kPickWord = 5;
constexpr size_t kCountWord = kPickWord + std::size(kPickShard);
constexpr size_t kWrapperWord = kCountWord + std::size(kCountEntry);
constexpr size_t kStubWords = kStubCodeSize / sizeof(uint32_t);
static_assert(kStubWords == kWrapperWord + 5);

constexpr uint32_t kNop = 0xD503201Fu;
constexpr uint32_t kB = 0x14000000u;
constexpr uint32_t kBtiJ = 0xD503249Fu;
// ldr x<rt>, <imm19>, with a zero immediate.
constexpr uint32_t kLdrLiteral = 0x58000000u;

constexpr uint32_t br_word(unsigned reg) {
  return 0xD61F0000u | (reg << 5);
}
constexpr uint32_t kBrX16 = br_word(16);

// ldr x<reg>, <literal_words * 4>; br x<reg>: the absolute jump used when a
// direct B cannot reach. Only x16 and x17 are used: they are the
// intra-procedure-call registers a linker veneer may clobber too, and BR
// through them is accepted by a BTI c landing pad.
constexpr std::array<uint32_t, 2> absolute_jump(unsigned reg,
                                                unsigned literal_words) {
  return {kLdrLiteral | (literal_words << 5) | reg, br_word(reg)};
}

// movz for hw 0, movk for the others: x11 = bits [16 * hw, 16 * hw + 16).
constexpr uint32_t mov_x11_word(unsigned hw) {
  return (hw == 0 ? kMovzX11 : kMovkX11) | (hw << 21);
}

constexpr uint32_t mov_x16_word(unsigned hw) {
  // Load the wrapper after the counter loop releases x16 and x17. BR x16
  // is compatible with BTI c on a guarded stub page.
  return (mov_x11_word(hw) & ~31u) | 16u;
}

// Every A64 form whose meaning depends on where it executes.
enum class pc_form : uint8_t {
  none,
  branch26, // b, bl
  branch19, // b.cond, bc.cond, cbz, cbnz
  branch14, // tbz, tbnz
  branch9,  // FEAT_CMPBR register and immediate comparisons
  adr,
  adrp,
  literal, // ldr/ldrsw/prfm (literal) and ldr s/d/q (literal)
};

pc_form classify(uint32_t w) {
  if ((w & 0x7E000000u) == 0x74000000u) {
    const unsigned condition = (w >> 21) & 7u;
    const unsigned size = (w >> 14) & 3u;
    const bool immediate = (w & (1u << 24)) != 0;
    if (condition == 4 || condition == 5 ||
        (immediate ? (size & 1u) != 0
                   : size == 1 || (size != 0 && (w >> 31) != 0))) {
      return pc_form::none;
    }
    return pc_form::branch9;
  }
  if ((w & 0x7C000000u) == 0x14000000u) {
    return pc_form::branch26;
  }
  if ((w & 0xFF000000u) == 0x54000000u || (w & 0x7E000000u) == 0x34000000u) {
    return pc_form::branch19;
  }
  if ((w & 0x7E000000u) == 0x36000000u) {
    return pc_form::branch14;
  }
  if ((w & 0x9F000000u) == 0x10000000u) {
    return pc_form::adr;
  }
  if ((w & 0x9F000000u) == 0x90000000u) {
    return pc_form::adrp;
  }
  if ((w & 0x3B000000u) == 0x18000000u) {
    return pc_form::literal;
  }
  return pc_form::none;
}

bool is_branch(pc_form form) {
  return form == pc_form::branch26 || form == pc_form::branch19 ||
         form == pc_form::branch14 || form == pc_form::branch9;
}

size_t literal_width(uint32_t w) {
  const unsigned opc = w >> 30;
  if (((w >> 26) & 1u) != 0) {
    return size_t{4} << opc;
  }
  return opc == 3 ? 0 : (opc == 1 ? 8 : 4);
}

int64_t sign_extend(uint64_t value, unsigned bits) {
  const uint64_t sign = uint64_t{1} << (bits - 1);
  return static_cast<int64_t>((value ^ sign) - sign);
}

// adr and adrp split their 21-bit immediate into immlo [30:29] and immhi
// [23:5].
int64_t adr_immediate(uint32_t w) {
  return sign_extend((((w >> 5) & 0x7FFFFu) << 2) | ((w >> 29) & 3u), 21);
}

// The address `w` references when it executes at `pc`. adrp yields a page.
uintptr_t pc_target(uint32_t w, pc_form form, uintptr_t pc) {
  int64_t offset = 0;
  switch (form) {
  case pc_form::branch26:
    offset = sign_extend(w & 0x3FFFFFFu, 26) * 4;
    break;
  case pc_form::branch19:
  case pc_form::literal:
    offset = sign_extend((w >> 5) & 0x7FFFFu, 19) * 4;
    break;
  case pc_form::branch14:
    offset = sign_extend((w >> 5) & 0x3FFFu, 14) * 4;
    break;
  case pc_form::branch9:
    offset = sign_extend((w >> 5) & 0x1FFu, 9) * 4;
    break;
  case pc_form::adr:
    offset = adr_immediate(w);
    break;
  case pc_form::adrp:
    return (pc & ~uintptr_t{0xFFF}) +
           static_cast<uintptr_t>(adr_immediate(w) * 4096);
  case pc_form::none:
    break;
  }
  return pc + static_cast<uintptr_t>(offset);
}

// Stores `value` in the `bits`-wide field at `lsb` if it fits.
bool put_field(uint32_t &w, int64_t value, unsigned lsb, unsigned bits) {
  const int64_t limit = int64_t{1} << (bits - 1);
  if (value < -limit || value >= limit) {
    return false;
  }
  const uint32_t mask = ((uint32_t{1} << bits) - 1) << lsb;
  w = (w & ~mask) | ((static_cast<uint32_t>(value) << lsb) & mask);
  return true;
}

// Re-encodes `w` so that, executed at `pc`, it references `target`. False
// when the immediate cannot reach.
bool retarget(uint32_t &w, pc_form form, uintptr_t pc, uintptr_t target) {
  if (form == pc_form::adr || form == pc_form::adrp) {
    int64_t imm = static_cast<int64_t>(target - pc);
    if (form == pc_form::adrp) {
      imm = static_cast<int64_t>((target & ~uintptr_t{0xFFF}) -
                                 (pc & ~uintptr_t{0xFFF})) /
            4096;
    }
    uint32_t hi = w;
    if (!put_field(hi, imm >> 2, 5, 19)) {
      return false;
    }
    w = (hi & ~(3u << 29)) | ((static_cast<uint32_t>(imm) & 3u) << 29);
    return true;
  }
  const auto delta = static_cast<int64_t>(target - pc);
  if (delta % 4 != 0) {
    return false;
  }
  switch (form) {
  case pc_form::branch26:
    return put_field(w, delta / 4, 0, 26);
  case pc_form::branch19:
  case pc_form::literal:
    return put_field(w, delta / 4, 5, 19);
  case pc_form::branch14:
    return put_field(w, delta / 4, 5, 14);
  case pc_form::branch9:
    return put_field(w, delta / 4, 5, 9);
  default:
    return false;
  }
}

// retaasppc/retabsppc <label> authenticate LR against PC - offset.
bool is_pc_modified_return(uint32_t w) {
  return (w & 0xFFC0001Fu) == 0x5500001Fu;
}

// brk, hlt and udf. Compilers put them after calls to noreturn functions.
bool is_trap(uint32_t w) {
  return (w & 0xFFE0001Fu) == 0xD4200000u || (w & 0xFFE0001Fu) == 0xD4400000u ||
         (w >> 16) == 0;
}

// b, br, ret, eret, their PAC variants and traps. Nothing after them runs
// unless a branch lands there. bl and blr return, so they do not end the
// flow.
bool ends_flow(uint32_t w) {
  if ((w & 0xFC000000u) == 0x14000000u || is_trap(w) ||
      is_pc_modified_return(w)) {
    return true;
  }
  if ((w & 0xFE000000u) != 0xD6000000u) {
    return false;
  }
  const uint32_t opc = (w >> 21) & 0xFu;
  return opc != 0x1u && opc != 0x9u;
}

// The target register of br, braaz, brabz, braa or brab.
std::optional<unsigned> br_register(uint32_t w) {
  if ((w & 0xFFFFFC1Fu) == br_word(0) || (w & 0xFFFFF81Fu) == 0xD61F081Fu ||
      (w & 0xFFFFF800u) == 0xD71F0800u) {
    return (w >> 5) & 31u;
  }
  return std::nullopt;
}

// ret, retaa, retab, and the FEAT_PAuth_LR retaasppc/retabsppc <label>.
bool is_return(uint32_t w) {
  return (w & 0xFFFFFC1Fu) == 0xD65F0000u || w == 0xD65F0BFFu ||
         w == 0xD65F0FFFu || is_pc_modified_return(w);
}

// bl, blr and the PAC blr forms.
bool is_call(uint32_t w) {
  return (w & 0xFC000000u) == 0x94000000u || (w & 0xFEFFF000u) == 0xD63F0000u;
}

// bti c, bti jc, paciasp, pacibsp: the first word of a separately entered
// function, never something a compiler places after a call in the same one.
bool is_entry_marker(uint32_t w) {
  return w == kBtiC || w == kBtiJc || w == 0xD503233Fu || w == 0xD503237Fu;
}

// The widest single-register access, a Q register; pairs access twice this.
constexpr unsigned kMaxAccess = 16;

// A load or store through a base register: the offset it adds to the base
// and an upper bound on the bytes it touches. A register index is unknown;
// the access is then taken at the base.
struct memory_access {
  unsigned base;
  int64_t offset;
  unsigned width;
};

std::optional<memory_access> memory_access_of(uint32_t w) {
  const unsigned base = (w >> 5) & 31u;
  // Unsigned scaled offset.
  if ((w & 0x3B000000u) == 0x39000000u) {
    const unsigned scale =
        ((w & (1u << 26)) != 0 && ((w >> 22) & 3) >= 2) ? 4 : (w >> 30) & 3;
    return memory_access{base, static_cast<int64_t>((w >> 10) & 0xFFF) << scale,
                         kMaxAccess};
  }
  if ((w & 0x3B000000u) == 0x38000000u) {
    // Unscaled, pre- and post-index and unprivileged: a signed 9-bit
    // offset, which post-index applies only after the access.
    if ((w & (1u << 21)) == 0) {
      const bool post = ((w >> 10) & 3) == 1;
      return memory_access{base, post ? 0 : sign_extend((w >> 12) & 0x1FFu, 9),
                           kMaxAccess};
    }
    // Register offset, atomics and the PAC loads.
    return memory_access{base, 0, kMaxAccess};
  }
  // Load/store pair, with a signed 7-bit offset scaled by the register size.
  if ((w & 0x3A000000u) == 0x28000000u) {
    const unsigned opc = w >> 30;
    const unsigned size =
        (w & (1u << 26)) != 0 ? 4u << opc : (opc == 2 ? 8u : 4u);
    const bool post = ((w >> 23) & 3) == 1;
    return memory_access{base,
                         post ? 0 : sign_extend((w >> 15) & 0x7Fu, 7) * size,
                         2 * kMaxAccess};
  }
  // Exclusive, acquire and release forms take the base alone.
  if ((w & 0x3F000000u) == 0x08000000u) {
    return memory_access{base, 0, 2 * kMaxAccess};
  }
  return std::nullopt;
}

// Whether `w` writes the X register named by its low five bits. Stores,
// compares and branches name a register there that they only read.
bool writes_rt(uint32_t w) {
  if ((w & 0xFF000000u) == 0x54000000u || // b.cond, bc.cond
      (w & 0x7C000000u) == 0x34000000u || // cbz, cbnz, tbz, tbnz
      (w & 0x7E000000u) == 0x74000000u || // compare and branch
      (w & 0xFFF00000u) == 0xD5100000u || // msr (register)
      (w & 0xFFF80000u) == 0xD5080000u || // sys
      (w & 0xFF000000u) == 0xD8000000u) { // prfm (literal)
    return false;
  }
  const unsigned opc = (w >> 22) & 3;
  const bool simd = (w & (1u << 26)) != 0;
  if ((w & 0x3B000000u) == 0x38000000u || (w & 0x3B000000u) == 0x39000000u) {
    // Atomics return the old value in Rt.
    if ((w & 0x3B200C00u) == 0x38200000u)
      return true;
    const bool prfm = !simd && (w >> 30) == 3 && opc == 2;
    return !simd && opc != 0 && !prfm;
  }
  if ((w & 0x3A000000u) == 0x28000000u) {
    return !simd && (w & (1u << 22)) != 0;
  }
  if ((w & 0x3F000000u) == 0x08000000u) {
    // Loads have L set; CAS forms write Rs and only read Rt.
    const bool cas = (w & (1u << 23)) != 0 && (w & (1u << 21)) != 0;
    return (w & (1u << 22)) != 0 && !cas;
  }
  return true;
}

// One Capstone handle and instruction buffer for a decode pass.
class decoder {
public:
  explicit decoder(bool detail) {
    const cs_err opened =
        cs_open(CS_ARCH_AARCH64, CS_MODE_LITTLE_ENDIAN, &handle_);
    if (opened != CS_ERR_OK) {
      handle_ = 0;
      // CS_ERR_MEMSETUP is a Capstone build without CAPSTONE_USE_SYS_DYN_MEM
      // and no CS_OPT_MEM allocator: nothing can decode in this process.
      failure_ = opened == CS_ERR_MEMSETUP ? "decoder_allocator_missing"
                 : opened == CS_ERR_MEM    ? "decoder_out_of_memory"
                                           : "decoder_unavailable";
      return;
    }
    if (detail) {
      cs_option(handle_, CS_OPT_DETAIL, CS_OPT_ON);
    }
    insn_ = cs_malloc(handle_);
    if (insn_ == nullptr)
      failure_ = "decoder_out_of_memory";
  }
  ~decoder() {
    if (insn_ != nullptr) {
      cs_free(insn_, 1);
    }
    if (handle_ != 0) {
      cs_close(&handle_);
    }
  }
  decoder(const decoder &) = delete;
  decoder &operator=(const decoder &) = delete;

  // Decodes the word at `bytes` as if it ran at `pc`. Null when Capstone
  // does not know it.
  const cs_insn *decode(const uint8_t *bytes, uintptr_t pc) {
    size_t size = 4;
    uint64_t address = pc;
    if (insn_ == nullptr ||
        !cs_disasm_iter(handle_, &bytes, &size, &address, insn_)) {
      return nullptr;
    }
    return insn_;
  }
  csh handle() const {
    return handle_;
  }
  // Why no word can decode, or null when the decoder is ready.
  const char *failure() const {
    return failure_;
  }

private:
  csh handle_ = 0;
  cs_insn *insn_ = nullptr;
  const char *failure_ = nullptr;
};

bool is_padding(uintptr_t address, size_t size) {
  for (size_t off = 0; off + sizeof(uint32_t) <= size;
       off += sizeof(uint32_t)) {
    uint32_t word = 0;
    std::memcpy(&word, reinterpret_cast<const void *>(address + off),
                sizeof(word));
    if (word != kNop) {
      return false;
    }
  }
  return true;
}

uint32_t word_at(const uint8_t *bytes) {
  uint32_t w = 0;
  std::memcpy(&w, bytes, sizeof(w));
  return w;
}

// FEAT_LSE. Every arm64 Mac has it. Read on each call: stubs are encoded
// while threads are stopped, where a guarded static could deadlock.
bool lse_available() {
#if defined(__APPLE__) && defined(__aarch64__)
  return true;
#elif defined(__linux__) && defined(__aarch64__)
  return (getauxval(AT_HWCAP) & (1UL << 8)) != 0; // HWCAP_ATOMICS
#else
  return false;
#endif
}

bool mte_available() {
#if defined(__linux__) && defined(__aarch64__)
  return (getauxval(AT_HWCAP2) & (1UL << 18)) != 0;
#else
  return false;
#endif
}

// IP0/IP1 are scratch at ABI calls, but can be live across a local branch.
// Prove a scratch dead on every path before using an absolute veneer.
bool scratch_dead(uintptr_t address, unsigned reg) {
  decoder cs(true);
  std::vector<uintptr_t> pending{address};
  std::vector<uintptr_t> seen;
  // Reading through the OS costs a syscall, so decode from a chunk that
  // stays inside one page and only refill when a branch leaves it.
  std::array<uint8_t, 512> chunk{};
  uintptr_t chunk_begin = 0;
  size_t chunk_size = 0;
  auto same = [&](unsigned r) {
    return r == (reg == 16 ? AARCH64_REG_X16 : AARCH64_REG_X17) ||
           r == (reg == 16 ? AARCH64_REG_W16 : AARCH64_REG_W17);
  };
  while (!pending.empty()) {
    const uintptr_t pc = pending.back();
    pending.pop_back();
    if (std::find(seen.begin(), seen.end(), pc) != seen.end()) {
      continue;
    }
    if (seen.size() >= 128) {
      return false;
    }
    seen.push_back(pc);
    if (pc - chunk_begin >= chunk_size) {
      chunk_begin = pc;
      const size_t page_left = 0x1000 - (pc & 0xFFF);
      chunk_size = read_code_prefix(
                       pc, {chunk.data(), std::min(chunk.size(), page_left)}) &
                   ~size_t{3};
      if (chunk_size == 0) {
        return false;
      }
    }
    const auto *bytes = chunk.data() + (pc - chunk_begin);
    const uint32_t w = word_at(bytes);
    const pc_form form = classify(w);
    if (is_trap(w)) {
      continue;
    }
    if (form == pc_form::branch9) {
      if ((w & 31u) == reg ||
          ((w & (1u << 24)) == 0 && ((w >> 16) & 31u) == reg)) {
        return false;
      }
      pending.push_back(pc_target(w, form, pc));
      pending.push_back(pc + 4);
      continue;
    }
    const cs_insn *insn = cs.decode(bytes, pc);
    if (insn == nullptr) {
      return false;
    }
    cs_regs reads{}, writes{};
    uint8_t nread = 0, nwrite = 0;
    if (cs_regs_access(cs.handle(), insn, reads, &nread, writes, &nwrite) !=
            CS_ERR_OK ||
        std::any_of(reads, reads + nread, same)) {
      return false;
    }
    // Under PACM these also consume X16 as a second modifier, even if the
    // decoder's baseline register-access metadata does not list it.
    if (reg == 16 && (w == 0xD50323BFu || w == 0xD50323FFu ||
                      w == 0xD65F0BFFu || w == 0xD65F0FFFu)) {
      return false;
    }
    // A call or return is an ABI boundary. The call's own target register
    // was checked above; BLR X16 still needs the incoming X16 value.
    if (is_call(w) || is_return(w) ||
        std::any_of(writes, writes + nwrite, same)) {
      continue;
    }
    // So is a register branch through IP0 or IP1: AAPCS64 lets a veneer
    // or tail call through one clobber the other, so the target cannot
    // read it. The branch register itself was checked above.
    if (const auto target = br_register(w);
        target && (*target == 16 || *target == 17)) {
      continue;
    }
    if (is_branch(form)) {
      pending.push_back(pc_target(w, form, pc));
      if (form == pc_form::branch26) {
        continue;
      }
    } else if (ends_flow(w)) {
      return false;
    }
    pending.push_back(pc + 4);
  }
  return true;
}
} // namespace

// BTI jc is a NOP without BTI and accepts both call and jump landings. The
// context is always loaded with four moves so the stub has a fixed size,
// which stacked hooks rely on when they copy and rewrite it.
stub_bytes create_absolute_jump(uintptr_t target_address, uintptr_t data,
                                bool counted) {
  uint32_t words[kStubWords] = {kBtiJc};
  for (unsigned hw = 0; hw < 4; ++hw) {
    const auto imm = static_cast<uint32_t>((data >> (16 * hw)) & 0xFFFFu);
    words[1 + hw] = mov_x11_word(hw) | (imm << 5);
  }
  std::memcpy(words + kPickWord, counted ? kPickShard : kCountNone,
              sizeof(kPickShard));
  std::memcpy(words + kCountWord,
              !counted          ? kCountNone
              : lse_available() ? kCountEntryLse
                                : kCountEntry,
              sizeof(kCountEntry));
  for (unsigned hw = 0; hw < 4; ++hw) {
    const auto imm =
        static_cast<uint32_t>((target_address >> (16 * hw)) & 0xFFFFu);
    words[kWrapperWord + hw] = mov_x16_word(hw) | (imm << 5);
  }
  words[kStubWords - 1] = kBrX16;

  stub_bytes bytes(kStubSize);
  std::memcpy(bytes.data(), words, sizeof(words));
  return bytes;
}

// Picks how many bytes to copy and which of them are PC-relative. The forms
// come from the instruction bits; Capstone only confirms every copied word
// is a known instruction.
std::tuple<relocation_info, size_t>
collect_relocations(uintptr_t address, size_t jump_size,
                    std::span<const uint8_t> original_prefix,
                    size_t function_size) {
  relocation_info info;
  info.overwritten_size = jump_size;
  // Refuses with a reason at `off` from the entry; see relocation_info.
  auto refuse = [&](const char *reason, size_t off,
                    detour_error error = detour_error::kDecodeFailed) {
    info.refuse(reason, address + off, error);
    return std::tuple{info, size_t{0}};
  };
  if (jump_size % 4 != 0 || address % 4 != 0)
    return refuse("misaligned_target", 0);
  // An unwind entry bounds the scan as on x86-64. A branch leaving it is a
  // tail call or an exit, and a noreturn BL at its end no longer runs the
  // scan into the next function.
  const auto scan =
      info.read_source(address, jump_size, original_prefix, function_size, 4);
  const size_t window = scan.window;
  const size_t limit = scan.limit;
  const bool bounded = scan.bounded;
  if (window == 0)
    return {info, 0};

  // A64 branch destinations come directly from the words. Scan up to 16KB,
  // stopping at a terminator no earlier branch reaches past. Only the resulting
  // copy is decoded.
  const auto *bytes = info.source_bytes.data();
  size_t size = jump_size;
  size_t scanned = 0;
  bool ended = false;
  intptr_t original_begin = 0;
  size_t original_end = 0;
  // Register values on the straight-line path to each word. Where another
  // branch joins, a value is still one the register may hold, which is
  // enough to refuse an alias, but no longer proves a BR destination.
  std::array<std::optional<uintptr_t>, 32> registers{};
  std::array<bool, 32> joined{};
  // Joins come from branches in the scanned range. Without an unwind entry
  // its end is not known yet, so the whole window is classified.
  std::vector<bool> joins(limit / 4);
  for (size_t off = 0; off < limit; off += 4) {
    const uint32_t w = word_at(bytes + off);
    const pc_form form = classify(w);
    if (!is_branch(form) || is_call(w)) {
      continue;
    }
    const uintptr_t t = pc_target(w, form, address + off) - address;
    if (t < limit) {
      joins[t / 4] = true;
    }
  }
  std::vector<uintptr_t> indirect_targets;
  // BL into the function after its entry, as (call offset, target).
  std::vector<std::pair<size_t, intptr_t>> interior_calls;
  // A BTI J landing inside the patch marks it as an indirect jump target.
  bool landing_in_patch = false;
  for (size_t off = 0; off < jump_size; off += 4) {
    const uint32_t w = word_at(bytes + off);
    landing_in_patch |= w == kBtiJ || w == kBtiJc;
  }
  // Compilers dispatch a jump table by adding a loaded offset to a base,
  // add xN, xB, xM/wM{, lsl/sxtw/sxtb}, in the block that ends with the BR.
  // Its destinations are data, and a case label may sit in the patch.
  auto table_dispatch = [&](size_t at, unsigned reg) {
    for (size_t steps = 0; at != 0 && !joins[at / 4] && steps < 16; ++steps) {
      at -= 4;
      const uint32_t w = word_at(bytes + at);
      if (ends_flow(w) || is_call(w)) {
        return false;
      }
      if ((w & 31u) == reg && writes_rt(w)) {
        return (w & 0x1F000000u) == 0x0B000000u;
      }
    }
    return false;
  };
  intptr_t reached = -1;
  // Ends the inferred function at `end`. A terminator inside the patch
  // requires enough following padding to hold the stub; without it the
  // function is genuinely too short, a refusal at `end`.
  auto finish = [&](size_t end) {
    if (end < jump_size && !is_padding(reinterpret_cast<uintptr_t>(bytes + end),
                                       jump_size - end)) {
      info.refuse("function_too_short", address + end,
                  detour_error::kInsufficientBytes);
      return false;
    }
    ended = true;
    info.function_end = end;
    scanned = std::max(end, size);
    return true;
  };
  for (size_t off = 0; off < limit; off += 4) {
    const uint32_t w = word_at(bytes + off);
    const pc_form form = classify(w);
    // A call to a noreturn function often ends a function without a trap.
    // What follows it and no branch reaches is the next function if it
    // starts like one.
    if (function_size == 0 && !bounded && off != 0 &&
        reached < static_cast<intptr_t>(off) && is_entry_marker(w) &&
        is_call(word_at(bytes + off - 4))) {
      if (!finish(off)) {
        return {info, 0};
      }
      break;
    }
    if ((w & 0x7E000000u) == 0x74000000u && form == pc_form::none) {
      return refuse("undecodable_instruction", off);
    }
    const size_t end = off + 4;
    scanned = end;
    if (off != 0 && ends_flow(word_at(bytes + off - 4))) {
      registers.fill(std::nullopt);
    }
    if (joins[off / 4]) {
      joined.fill(true);
    }
    if (is_call(w)) {
      registers.fill(std::nullopt);
    }
    if (is_branch(form)) {
      const auto t =
          static_cast<intptr_t>(pc_target(w, form, address + off) - address);
      const bool call = form == pc_form::branch26 && (w >> 31) != 0;
      if (!call && t >= 0 &&
          (t < static_cast<intptr_t>(limit) ||
           (!bounded && form != pc_form::branch26))) {
        reached = std::max(reached, t);
      }
      if (!call && off >= size && t >= 0 && t < static_cast<intptr_t>(size)) {
        size = end;
      }
      if (call && t > 0 && t < static_cast<intptr_t>(window)) {
        interior_calls.emplace_back(off, t);
      }
    }
    // Address identity and a snapshot of the old code cannot both be
    // supplied through a derived pointer. Refuse known aliases instead.
    // Base 31 is SP, which the scan does not track.
    if (const auto access = memory_access_of(w);
        access && access->base != 31 && registers[access->base]) {
      const uintptr_t effective =
          *registers[access->base] + static_cast<uintptr_t>(access->offset);
      const auto delta = static_cast<intptr_t>(effective - address);
      if (delta < static_cast<intptr_t>(jump_size) &&
          delta + static_cast<intptr_t>(access->width) > 0) {
        return refuse("patched_bytes_referenced", off);
      }
    }
    if (form == pc_form::adr || form == pc_form::adrp) {
      registers[w & 31] = pc_target(w, form, address + off);
      joined[w & 31] = false;
    } else if ((w & 0xFF000000u) == 0x91000000u) {
      const auto base = registers[(w >> 5) & 31];
      const unsigned shift = ((w >> 22) & 1) ? 12 : 0;
      joined[w & 31] = joined[(w >> 5) & 31];
      registers[w & 31] =
          base
              ? std::optional<uintptr_t>{*base + (((w >> 10) & 0xFFF) << shift)}
              : std::nullopt;
    } else if (form == pc_form::literal) {
      const uintptr_t abs = pc_target(w, form, address + off);
      const auto delta = static_cast<intptr_t>(abs - address);
      if (delta < static_cast<intptr_t>(jump_size) &&
          delta + static_cast<intptr_t>(literal_width(w)) > 0) {
        size = std::max(size, end);
      }
      registers[w & 31].reset();
      joined[w & 31] = false;
      // Literals in the window come from the snapshot, which also holds
      // the original prefix when a stub already covers it.
      uintptr_t value = 0;
      bool known = false;
      if ((w & 0xFF000000u) == 0x58000000u) {
        if (window >= 8 && abs - address <= window - 8) {
          std::memcpy(&value, bytes + (abs - address), 8);
          known = true;
        } else {
          known =
              read_code_bytes(abs, {reinterpret_cast<uint8_t *>(&value), 8});
        }
      }
      if (known) {
        registers[w & 31] = value;
      }
    } else if (const auto reg = br_register(w)) {
      // An unproven BR is taken as a tail call: through the GOT, a vtable
      // or a function pointer, it enters another function's entry. Only
      // dispatch that may stay in this function is refused: a destination
      // the registers place in the copy, a jump table, or any unproven one
      // when the patch holds a BTI J landing.
      const auto destination = registers[*reg];
      if (destination) {
        indirect_targets.push_back(*destination);
        if (*destination >= address && *destination < address + limit) {
          reached =
              std::max(reached, static_cast<intptr_t>(*destination - address));
        }
      }
      if ((!destination || joined[*reg]) &&
          (landing_in_patch || table_dispatch(off, *reg))) {
        return refuse("indirect_branch_unproven", off);
      }
    } else if (w != kNop && !is_branch(form) && !ends_flow(w)) {
      // Unmodelled writes invalidate the address proof conservatively.
      registers.fill(std::nullopt);
    }
    // A pointer into the overwritten bytes reaches the stub instead of the
    // code it named, and the copy rebuilds the same pointer. ADRP alone is
    // only a page; its derived address is checked when it is formed. The
    // entry itself stays valid: calling it calls the hook.
    const bool derived = form == pc_form::adr || form == pc_form::literal ||
                         (w & 0xFF000000u) == 0x91000000u;
    if (derived && registers[w & 31] &&
        *registers[w & 31] - address - 1 < jump_size - 1) {
      return refuse("patched_bytes_referenced", off);
    }
    if (function_size == 0 && ends_flow(w) &&
        reached < static_cast<intptr_t>(end)) {
      if (!finish(end)) {
        return {info, 0};
      }
      break;
    }
  }
  // An explicit extent shorter than the patch ends like an inferred one: in
  // a terminator followed by padding.
  if (function_size != 0 && function_size < jump_size &&
      scanned == function_size) {
    if (!ends_flow(word_at(bytes + scanned - 4)))
      return refuse("function_too_short", scanned,
                    detour_error::kInsufficientBytes);
    if (!finish(scanned))
      return {info, 0};
  }
  // A bounded scan that reaches the end of the entry has seen all of it,
  // but must still cover the patch: the bytes after the entry belong to
  // another function.
  if (!ended && ((function_size != 0 && scanned == function_size) ||
                 (bounded && scanned == limit && limit >= jump_size))) {
    ended = true;
    if (ends_flow(word_at(bytes + scanned - 4)))
      info.function_end = scanned;
  }
  // No terminator that every earlier branch stays before, within the
  // window: the end of the function is unknown, not short. The address is
  // where the scan gave up.
  if (!ended) {
    return refuse("function_end_not_found", scanned);
  }
  // A call to the entry is a new invocation through the hook. One from
  // outside the copy to a later copied offset would run patched bytes.
  for (const auto &[off, t] : interior_calls) {
    if (off >= size && t < static_cast<intptr_t>(size)) {
      return refuse("branch_into_patch", off);
    }
  }
  for (const uintptr_t destination : indirect_targets) {
    if (destination >= address && destination < address + size) {
      return refuse("branch_into_patch", destination - address);
    }
  }

  // Every copied word must be one the decoder knows; the PC-relative ones
  // all come from classify(). The bundled decoder predates CMPBR, whose
  // bits the scan already validated.
  decoder cs(false);
  if (cs.failure() != nullptr)
    return refuse(cs.failure(), 0);
  for (size_t off = 0; off < size; off += 4) {
    const uint32_t w = word_at(bytes + off);
    const pc_form form = classify(w);
    if (form != pc_form::branch9 &&
        cs.decode(bytes + off, address + off) == nullptr) {
      return refuse("undecodable_instruction", off);
    }
    // PACIASPPC, PACIBSPPC, their PACNB forms, AUTIxSPPC <label> and
    // RETAxSPPC <label> use the PC as a PAC modifier. PACIASP/PACIBSP,
    // AUTIASP/AUTIBSP and RETAA/RETAB modify with SP only and copy
    // unchanged, except after PACM (FEAT_PAuth_LR), which makes the next one
    // take X16 as well; that pairing is not worth splitting or proving.
    if (w == 0xDAC1A3FEu || w == 0xDAC1A7FEu || w == 0xDAC183FEu ||
        w == 0xDAC187FEu || (w & 0xFFE0001Fu) == 0xF380001Fu ||
        (w & 0xFFE0001Fu) == 0xF3A0001Fu || is_pc_modified_return(w) ||
        w == 0xD50324FFu ||
        (form == pc_form::branch26 && (w >> 31) != 0 &&
         pc_target(w, form, address + off) == address + off + 4)) {
      return refuse("unsupported_instruction", off);
    }
    // ldr q (literal) with opc 3 is unallocated.
    if (form == pc_form::literal && ((w >> 26) & 1u) != 0 && (w >> 30) == 3) {
      return refuse("undecodable_instruction", off);
    }
    if (form == pc_form::literal && literal_width(w) != 0) {
      const auto delta =
          static_cast<intptr_t>(pc_target(w, form, address + off) - address);
      const auto width = static_cast<intptr_t>(literal_width(w));
      if (delta < static_cast<intptr_t>(jump_size) && delta + width > 0) {
        original_begin = std::min(original_begin, delta);
        original_end =
            std::max(original_end, static_cast<size_t>(delta + width));
      }
    }
    if (form != pc_form::none) {
      info.relocations.emplace_back(relocation_entry{off, w});
    }
  }
  // Consumers only ask about the copied instructions.
  for (size_t off = 0; off < size; off += 4) {
    info.instruction_offsets.push_back(off);
  }
  const size_t end = std::max({size, scanned, original_end});
  if (end > window) {
    return refuse("preserved_bytes_unreadable", window);
  }
  info.source_bytes.resize(end);
  if (original_begin < 0 &&
      !info.read_before(static_cast<size_t>(-original_begin))) {
    return refuse("preserved_bytes_unreadable", 0);
  }
  return {info, size};
}

// Lays out the copied prologue, the exit to `return_address`, then the
// veneers and literals the rewritten words point at.
//
// `at` is where the trampoline will run. When it is known, a PC-relative
// word that still reaches its target from there is re-encoded in place, and
// the exit and veneers are direct B. With `at == 0` every word takes its
// position-independent form, which is never shorter, so that build is an
// upper bound on the size of the final one.
trampoline_buffer create_trampoline(uintptr_t at, uintptr_t return_address,
                                    std::span<const uint8_t> target,
                                    const relocation_info &relocations,
                                    bool sizing_only, bool unwind) {
  const auto fn = reinterpret_cast<uintptr_t>(target.data());
  if (at % 4 != 0 || fn % 4 != 0 || target.size() % 4 != 0) {
    return {0, {}, {}};
  }
  if (const auto snapshot = relocations.snapshot(fn, target.size());
      !snapshot.empty())
    target = snapshot;
  std::vector<uint32_t> out;
  std::vector<std::pair<uintptr_t, uintptr_t>> offsets;

  // A word in `out` whose immediate is filled in once the destination has a
  // place: an instruction of the copy (by original offset), a veneer to an
  // absolute address, an 8-byte literal, or preserved original bytes.
  enum class dest_kind : uint8_t { code, veneer, literal, original };
  struct fixup {
    size_t word;
    pc_form form;
    dest_kind kind;
    uint64_t value;
  };
  std::vector<fixup> fixups;
  std::vector<stack_adjustment> stack;
  std::vector<unwind_range> ranges;
  intptr_t original_begin = 0;
  intptr_t original_end = static_cast<intptr_t>(target.size());
  bool preserve_original = false;

  auto near = [&](uint32_t &w, pc_form form, uintptr_t abs) {
    return at != 0 && retarget(w, form, at + out.size() * 4, abs);
  };
  auto emit_quad = [&](uint64_t value) {
    const bool padding = ((at + out.size() * 4) & 7u) != 0;
    if (padding) {
      out.push_back(kNop);
    }
    const size_t position = out.size();
    out.push_back(static_cast<uint32_t>(value));
    out.push_back(static_cast<uint32_t>(value >> 32));
    if (sizing_only && at == 0 && !padding) {
      out.push_back(kNop);
    }
    return position;
  };
  // ldr x<reg>, =value
  auto emit_load_literal = [&](unsigned reg, uint64_t value) {
    fixups.push_back({out.size(), pc_form::literal, dest_kind::literal, value});
    out.push_back(kLdrLiteral | reg);
  };
  // Rewrites a literal load as a load through the address.
  auto emit_load_through = [&](uint32_t w, uintptr_t abs) {
    const unsigned rt = w & 31u;
    const unsigned opc = w >> 30;
    const bool simd = ((w >> 26) & 1u) != 0;
    if (!simd && opc == 3) {
      // PRFM is an optional hint. Loads into XZR still access memory.
      out.push_back(kNop);
      return;
    }
    const bool spill = simd || rt == 31;
    const unsigned base = spill ? 17u : rt;
    if (spill) {
      // Keep SP aligned and the spill below the 128-byte Darwin red zone.
      out.push_back(0xD10243FFu); // sub sp, sp, #144
      if (SPUD_UNWIND_SUPPORT && unwind)
        stack.push_back({out.size() * 4, 144});
      out.push_back(0xF90003F1u); // str x17, [sp]
      if (SPUD_UNWIND_SUPPORT && unwind)
        stack.push_back(
            {out.size() * 4, 144, stack_adjustment::register_slot{17, 0}});
    }
    // ldr wt / ldr xt / ldrsw xt, and ldr st / ldr dt / ldr qt, from [base].
    constexpr uint32_t kGpr[] = {0xB9400000u, 0xF9400000u, 0xB9800000u};
    constexpr uint32_t kSimd[] = {0xBD400000u, 0xFD400000u, 0x3DC00000u};
    emit_load_literal(base, abs);
    out.push_back((simd ? kSimd[opc] : kGpr[opc]) | (base << 5) | rt);
    if (spill) {
      out.push_back(0xF94003F1u); // ldr x17, [sp]
      if (SPUD_UNWIND_SUPPORT && unwind)
        stack.push_back({out.size() * 4, 144});
      out.push_back(0x910243FFu); // add sp, sp, #144
      if (SPUD_UNWIND_SUPPORT && unwind)
        stack.push_back({out.size() * 4, 0});
    }
  };
  // Materializes an adr/adrp result. adr into xzr writes nothing.
  auto emit_address = [&](uint32_t w, uintptr_t abs) {
    const unsigned rd = w & 31u;
    if (rd == 31) {
      out.push_back(kNop);
    } else {
      emit_load_literal(rd, abs);
    }
  };
  auto is_internal = [&](uintptr_t abs) {
    return abs >= fn && abs <= fn + target.size() && (abs - fn) % 4 == 0;
  };

  size_t cursor = 0;
  auto copy_to = [&](size_t end) {
    for (; cursor < end; cursor += 4) {
      out.push_back(word_at(target.data() + cursor));
    }
  };

  for (const auto &relocation : relocations.relocations) {
    const auto &relo = std::get<relocation_entry>(relocation);
    copy_to(relo.address);
    offsets.emplace_back(relo.address, out.size() * 4 - relo.address);
    cursor = relo.address + 4;

    uint32_t w = relo.word;
    const pc_form form = classify(w);
    const uintptr_t abs = pc_target(w, form, fn + relo.address);
    switch (form) {
    case pc_form::branch9:
      if (!is_internal(abs) && near(w, form, abs)) {
        out.push_back(w);
      } else {
        // imm9 cannot reach a distant veneer or a heavily expanded copy.
        // Keep the comparison and use a nearby B bridge without touching NZCV.
        retarget(w, form, 0, 8);
        out.push_back(w);
        out.push_back(kB | 2u);
        fixups.push_back(
            {out.size(), pc_form::branch26,
             is_internal(abs) ? dest_kind::code : dest_kind::veneer,
             is_internal(abs) ? abs - fn : abs});
        out.push_back(kB);
      }
      break;
    case pc_form::branch26:
    case pc_form::branch19:
    case pc_form::branch14:
      // Inside the copy, including its end, which is the exit jump.
      if (is_internal(abs) &&
          !(form == pc_form::branch26 && (w >> 31) != 0 && abs == fn)) {
        fixups.push_back({out.size(), form, dest_kind::code, abs - fn});
      } else if (!near(w, form, abs)) {
        // The copied word keeps its condition and, for bl, its link; only
        // the destination moves to a veneer.
        fixups.push_back({out.size(), form, dest_kind::veneer, abs});
      }
      out.push_back(w);
      break;
    case pc_form::adr:
    case pc_form::adrp:
      if (near(w, form, abs)) {
        out.push_back(w);
      } else {
        emit_address(w, abs);
      }
      break;
    case pc_form::literal: {
      const auto data_offset = static_cast<intptr_t>(abs - fn);
      const auto width = static_cast<intptr_t>(literal_width(w));
      if (width != 0 && data_offset + width > 0 &&
          data_offset < static_cast<intptr_t>(relocations.overwritten_size)) {
        preserve_original = true;
        original_begin = std::min(original_begin, data_offset);
        original_end = std::max(original_end, data_offset + width);
        fixups.push_back({out.size(), form, dest_kind::original,
                          static_cast<uint64_t>(data_offset)});
        out.push_back(w);
      } else if (near(w, form, abs)) {
        out.push_back(w);
      } else {
        if (!sizing_only && mte_available() && literal_width(w) != 0) {
          // Register-base loads add tag checking absent from LDR literal.
          return {0, {}, {}};
        }
        emit_load_through(w, abs);
      }
      break;
    }
    case pc_form::none:
      out.push_back(w);
      break;
    }
    offsets.emplace_back(cursor, out.size() * 4 - cursor);
  }
  copy_to(target.size());
  offsets.emplace_back(target.size(), out.size() * 4 - target.size());

  // A direct branch keeps x17 and lands in a BTI-guarded page without a
  // landing pad, which the indirect fallback cannot. A BL veneer needs no
  // liveness proof: AAPCS64 lets veneers clobber IP0 and IP1 on the way to
  // any call target.
  auto emit_jump = [&](uintptr_t to, bool unreachable = false,
                       bool call = false) {
    uint32_t b = kB;
    if (near(b, pc_form::branch26, to)) {
      out.push_back(b);
    } else {
      unsigned reg = 16;
      if (!sizing_only && !unreachable && !call && !scratch_dead(to, reg)) {
        reg = 17;
        if (!scratch_dead(to, reg)) {
          return false;
        }
      }
      if (!sizing_only && !unreachable && bti_guarded_page(to)) {
        uint32_t landing = 0;
        if (!read_code_bytes(to, {reinterpret_cast<uint8_t *>(&landing), 4})) {
          return false;
        }
        if (landing != kBtiJc && landing != kBtiC) {
          return false;
        }
      }
      const size_t load = out.size();
      const auto jump = absolute_jump(reg, 0);
      out.insert(out.end(), jump.begin(), jump.end());
      const size_t literal = emit_quad(to);
      if (!retarget(out[load], pc_form::literal, load * 4, literal * 4)) {
        return false;
      }
    }
    return true;
  };
  if (!emit_jump(return_address,
                 relocations.function_end != 0 &&
                     relocations.function_end <= target.size())) {
    return {0, {}, {}};
  }
  if (SPUD_UNWIND_SUPPORT && unwind)
    ranges.push_back({0, out.size() * 4, 0});

  size_t original_start = 0;
  if (preserve_original) {
    const uintptr_t begin = fn + original_begin;
    const size_t length = static_cast<size_t>(original_end - original_begin);
    // Retain the original alignment for scalar and vector self-loads. The
    // size estimate reserves every possible four-byte padding amount.
    const size_t padding =
        (begin - (at + out.size() * 4)) & (kPreservedAlignment - 1);
    out.resize(out.size() + padding / 4, kNop);
    original_start = out.size();
    const auto original = relocations.snapshot(begin, length);
    if (original.empty())
      return {0, {}, {}};
    for (size_t off = 0; off < length; off += 4) {
      out.push_back(word_at(original.data() + off));
    }
    if (at == 0) {
      out.resize(out.size() + (kPreservedAlignment - 4 - padding) / 4, kNop);
    }
  }
  for (const auto &f : fixups) {
    size_t dest = out.size();
    switch (f.kind) {
    case dest_kind::code:
      dest = relocated_location_at(offsets, f.value) / 4;
      break;
    case dest_kind::veneer:
      if (!emit_jump(f.value, false,
                     f.form == pc_form::branch26 && (out[f.word] >> 31) != 0)) {
        return {0, {}, {}};
      }
      if (SPUD_UNWIND_SUPPORT && unwind) {
        const auto position =
            std::upper_bound(offsets.begin(), offsets.end(), f.word * 4,
                             [](size_t pc, const auto &entry) {
                               return pc < entry.first + entry.second;
                             });
        const size_t source_offset =
            position == offsets.begin() ? 0 : std::prev(position)->first;
        ranges.push_back({dest * 4, out.size() * 4, source_offset});
      }
      break;
    case dest_kind::literal:
      dest = emit_quad(f.value);
      break;
    case dest_kind::original:
      dest = original_start +
             (static_cast<intptr_t>(f.value) - original_begin) / 4;
      break;
    }
    // Both ends are in this buffer, so the distance does not depend on `at`.
    uint32_t w = out[f.word];
    if (!retarget(w, f.form, f.word * 4, dest * 4)) {
      return {0, {}, {}};
    }
    out[f.word] = w;
  }

  std::vector<uint8_t> bytes(out.size() * 4);
  std::memcpy(bytes.data(), out.data(), bytes.size());
  return {0, std::move(bytes), std::move(offsets), std::move(ranges),
          std::move(stack)};
}

uintptr_t maybe_resolve_jump(uintptr_t address) {
  uint32_t w = 0;
  if (!read_code_bytes(address, {reinterpret_cast<uint8_t *>(&w), sizeof(w)}) ||
      (w & 0xFC000000u) != kB) {
    return address;
  }
  // A hook's bare landing pad is a B too. Following it would hook the
  // anchor instead of stacking on the target.
  const uintptr_t next = pc_target(w, pc_form::branch26, address);
  if (anchor_at(next, Arch::kArm64) != nullptr) {
    return address;
  }
  return next;
}

} // namespace arm64
} // namespace detail
} // namespace spud

#pragma once

#include <spud/arch.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace spud::detail {

// The largest full stub of any architecture (72 bytes on ARM64), rounded up.
inline constexpr size_t kMaxStubSize = 80;

// Stubs have an architecture-defined size. Encoding one while threads are
// stopped must not enter an allocator a stopped thread could own.
struct stub_bytes {
  std::array<uint8_t, kMaxStubSize> bytes{};
  size_t length = 0;

  stub_bytes() = default;
  explicit stub_bytes(size_t size) : length(size) {
    assert(size <= bytes.size());
  }
  uint8_t *data() {
    return bytes.data();
  }
  const uint8_t *data() const {
    return bytes.data();
  }
  size_t size() const {
    return length;
  }
  bool empty() const {
    return length == 0;
  }
  void resize(size_t size, uint8_t value = 0) {
    assert(size <= bytes.size());
    for (size_t i = length; i < size; ++i)
      bytes[i] = value;
    length = size;
  }
  uint8_t &operator[](size_t i) {
    return bytes[i];
  }
  operator std::span<const uint8_t>() const {
    return {data(), size()};
  }
};

namespace x64 {

// Fixed size of the x64 jump stub: endbr64 (4) + mov r11, imm64 (10) +
// entry shard pick and lock inc (28) + jmp [rip+0] (6) + embedded wrapper (8).
inline constexpr size_t kStubSize = 56;
inline constexpr size_t kStubCodeSize = 48;
inline constexpr uint32_t kEndbrLanding = 0xFA1E0FF3;
inline constexpr size_t kBranchPadSize = 5;
// Existing IBT entries keep endbr64 before the branch. Other targets need
// only the branch, even when the generated stubs require IBT landing pads.
inline constexpr size_t kShortStubSize = 9;

// `counted` stubs increment one of the context's entry shards and pass its
// address to the wrapper in r10; a permanent hook's stub has NOPs there.
// Both have the same size.
stub_bytes create_absolute_jump(uintptr_t target, uintptr_t container,
                                bool counted = true);

inline stub_bytes create_short_jump(uintptr_t from, uintptr_t to,
                                    bool endbr = true) {
  const size_t prefix = endbr ? 4 : 0;
  const size_t size = prefix + kBranchPadSize;
  const intptr_t rel =
      static_cast<intptr_t>(to) - static_cast<intptr_t>(from + size);
  if (rel != static_cast<int32_t>(rel)) {
    return {};
  }
  stub_bytes bytes(size);
  if (endbr)
    std::memcpy(bytes.data(), &kEndbrLanding, sizeof(kEndbrLanding));
  bytes[prefix] = 0xE9;
  const auto disp = static_cast<int32_t>(rel);
  std::memcpy(bytes.data() + prefix + 1, &disp, sizeof(disp));
  return bytes;
}

inline bool try_decode_short_jump(const void *bytes, size_t available,
                                  uintptr_t site, uintptr_t *body,
                                  size_t *size = nullptr) noexcept {
  if (bytes == nullptr || body == nullptr || available < kBranchPadSize) {
    return false;
  }
  const auto *b = static_cast<const uint8_t *>(bytes);
  const size_t prefix =
      std::memcmp(b, &kEndbrLanding, sizeof(kEndbrLanding)) == 0 ? 4 : 0;
  const size_t length = prefix + kBranchPadSize;
  if (available < length || b[prefix] != 0xE9)
    return false;
  int32_t disp = 0;
  std::memcpy(&disp, b + prefix + 1, sizeof(disp));
  *body = site + length + static_cast<intptr_t>(disp);
  if (size != nullptr)
    *size = length;
  return true;
}

// A register-free absolute jump when the anchor is outside rel32 reach.
// Only targets that already have endbr64 keep it in front of the jump.
inline constexpr size_t kFarPadSize = 14;
inline constexpr size_t kFarStubSize = 18;

inline stub_bytes create_far_jump(uintptr_t to, bool endbr = true) {
  const size_t prefix = endbr ? 4 : 0;
  stub_bytes bytes(prefix + kFarPadSize);
  if (endbr)
    std::memcpy(bytes.data(), &kEndbrLanding, sizeof(kEndbrLanding));
  const uint8_t code[] = {0xFF, 0x25, 0, 0, 0, 0};
  std::memcpy(bytes.data() + prefix, code, sizeof(code));
  std::memcpy(bytes.data() + prefix + sizeof(code), &to, sizeof(to));
  return bytes;
}

// The same jmp [rip] can be a foreign thunk, so the caller must still prove
// that the decoded destination is the target's own anchor.
inline bool try_decode_far_jump(const void *bytes, size_t available,
                                uintptr_t *body,
                                size_t *size = nullptr) noexcept {
  if (bytes == nullptr || body == nullptr || available < kFarPadSize) {
    return false;
  }
  const auto *b = static_cast<const uint8_t *>(bytes);
  const size_t prefix =
      std::memcmp(b, &kEndbrLanding, sizeof(kEndbrLanding)) == 0 ? 4 : 0;
  const size_t length = prefix + kFarPadSize;
  const uint8_t code[] = {0xFF, 0x25, 0, 0, 0, 0};
  if (available < length || std::memcmp(b + prefix, code, sizeof(code)) != 0)
    return false;
  std::memcpy(body, b + prefix + sizeof(code), sizeof(*body));
  if (size != nullptr)
    *size = length;
  return true;
}

// endbr64 (4) + jmp [rip+disp32] (6). Jumps through a pointer slot within
// rel32 reach, loaded when the jump runs. The anchor's dispatch and each
// hook's original() are this jump, so relinking a chain is a pointer store.
// It is one instruction, so a thread is either before the load or already
// at the destination.
inline constexpr size_t kIndirectJumpSize = 10;

inline stub_bytes create_indirect_jump(uintptr_t at, uintptr_t slot) {
  const intptr_t rel = static_cast<intptr_t>(slot) -
                       static_cast<intptr_t>(at + kIndirectJumpSize);
  if (rel != static_cast<int32_t>(rel)) {
    return {};
  }
  stub_bytes bytes(kIndirectJumpSize);
  const uint8_t code[] = {0xF3, 0x0F, 0x1E, 0xFA, 0xFF, 0x25};
  std::memcpy(bytes.data(), code, sizeof(code));
  const auto disp = static_cast<int32_t>(rel);
  std::memcpy(bytes.data() + sizeof(code), &disp, sizeof(disp));
  return bytes;
}

inline bool try_decode_indirect_jump(const void *bytes, size_t available,
                                     uintptr_t at, uintptr_t *slot) noexcept {
  if (bytes == nullptr || slot == nullptr || available < kIndirectJumpSize) {
    return false;
  }
  const auto *b = static_cast<const uint8_t *>(bytes);
  const uint8_t code[] = {0xF3, 0x0F, 0x1E, 0xFA, 0xFF, 0x25};
  if (std::memcmp(b, code, sizeof(code)) != 0) {
    return false;
  }
  int32_t disp = 0;
  std::memcpy(&disp, b + sizeof(code), sizeof(disp));
  *slot = at + kIndirectJumpSize + static_cast<intptr_t>(disp);
  return true;
}

} // namespace x64

#if SPUD_AARCH64_SUPPORT
namespace arm64 {

// BTI (4), context moves (16), entry shard pick (16), entry counter (16: an
// exclusive loop, or an LSE add and padding), wrapper moves (16), BR (4).
// No embedded literal: function entries only need four-byte alignment.
inline constexpr size_t kStubSize = 72;
inline constexpr size_t kStubCodeSize = 72;
// A landing pad is a bare B imm26, or a BTI word followed by one. B reaches
// ±128MB.
inline constexpr size_t kBranchPadSize = 4;
inline constexpr size_t kShortStubSize = 8;

// bti jc: a NOP without BTI, and accepts both call and jump landings.
inline constexpr uint32_t kBtiJc = 0xD50324DFu;
inline constexpr uint32_t kBtiC = 0xD503245Fu;
// The landing word of a bare branch pad.
inline constexpr uint32_t kNoLanding = 0;

// `counted` stubs increment one of the context's entry shards and pass its
// address to the wrapper in x10; a permanent hook's stub has NOPs there.
// Both have the same size and boundaries.
stub_bytes create_absolute_jump(uintptr_t target, uintptr_t container,
                                bool counted = true);

// `landing` is the word placed in front of the branch, or kNoLanding for a
// bare `b to` at `from`.
inline stub_bytes create_short_jump(uintptr_t from, uintptr_t to,
                                    uint32_t landing) {
  const size_t branch_at = landing == kNoLanding ? 0 : 4;
  if ((to & 3u) != 0) {
    return {};
  }
  const intptr_t diff =
      static_cast<intptr_t>(to) - static_cast<intptr_t>(from + branch_at);
  if ((diff & 3) != 0 || diff < -0x08000000ll || diff >= 0x08000000ll) {
    return {};
  }
  const uint32_t imm26 =
      static_cast<uint32_t>(static_cast<int32_t>(diff / 4)) & 0x03FFFFFFu;
  const uint32_t br = 0x14000000u | imm26;
  stub_bytes bytes(branch_at + 4);
  if (branch_at != 0) {
    std::memcpy(bytes.data(), &landing, sizeof(landing));
  }
  std::memcpy(bytes.data() + branch_at, &br, sizeof(br));
  return bytes;
}

// Accepts the pads create_short_jump writes: a bare B, or bti c / bti jc and
// a B. Reads the second word only after a BTI. A bare B is also ordinary
// forwarder code, so the caller must still prove `body` is the hook's.
inline bool try_decode_short_jump(const void *bytes, size_t available,
                                  uintptr_t site, uintptr_t *body,
                                  size_t *site_size,
                                  uint32_t *landing) noexcept {
  if (bytes == nullptr || body == nullptr || site_size == nullptr ||
      landing == nullptr || available < kBranchPadSize) {
    return false;
  }
  const auto *b = static_cast<const uint8_t *>(bytes);
  uint32_t w = 0;
  std::memcpy(&w, b, sizeof(w));
  size_t branch_at = 0;
  *landing = kNoLanding;
  if (w == kBtiC || w == kBtiJc) {
    if (available < kShortStubSize) {
      return false;
    }
    *landing = w;
    branch_at = 4;
    std::memcpy(&w, b + 4, sizeof(w));
  }
  if ((w >> 26) != 0x5u) {
    return false;
  }
  const int32_t imm26 = static_cast<int32_t>(w << 6) >> 6;
  *body = site + branch_at + static_cast<intptr_t>(imm26) * 4;
  *site_size = branch_at + 4;
  return true;
}

// ldr x16, #8; br x16; .quad body, optionally after the same BTI word a
// short pad would carry. Written when no block within B reach of the target
// could hold the body. x16 (IP0) is free at a function entry under AAPCS64,
// and BR x16 lands on the body's bti jc. The literal is only four-byte
// aligned behind a BTI word or at a four-byte aligned target; LDR (literal)
// of an X register has no alignment requirement when SCTLR_EL1.A is clear,
// as on every OS spud runs on, and the pad is written with threads stopped,
// so single-copy atomicity does not matter either.
inline constexpr size_t kFarPadSize = 16;
inline constexpr size_t kFarStubSize = 20;
inline constexpr uint32_t kLdrX16Lit8 = 0x58000050u;
inline constexpr uint32_t kBrX16Word = 0xD61F0200u;

inline stub_bytes create_far_jump(uintptr_t to, uint32_t landing) {
  if ((to & 3u) != 0) {
    return {};
  }
  const size_t code_at = landing == kNoLanding ? 0 : 4;
  stub_bytes bytes(code_at + kFarPadSize);
  if (code_at != 0) {
    std::memcpy(bytes.data(), &landing, sizeof(landing));
  }
  const uint32_t code[] = {kLdrX16Lit8, kBrX16Word};
  std::memcpy(bytes.data() + code_at, code, sizeof(code));
  std::memcpy(bytes.data() + code_at + sizeof(code), &to, sizeof(to));
  return bytes;
}

// Accepts the pads create_far_jump writes. Reads a word only after the one
// before it matched, since the caller may hand in arbitrary code. Like the
// short pad, `ldr x16; br x16` is also ordinary veneer code, so the caller
// must still prove `body` is the hook's.
inline bool try_decode_far_jump(const void *bytes, size_t available,
                                uintptr_t *body, size_t *site_size,
                                uint32_t *landing) noexcept {
  if (bytes == nullptr || body == nullptr || site_size == nullptr ||
      landing == nullptr || available < kFarPadSize) {
    return false;
  }
  const auto *b = static_cast<const uint8_t *>(bytes);
  uint32_t w = 0;
  std::memcpy(&w, b, sizeof(w));
  size_t code_at = 0;
  *landing = kNoLanding;
  if (w == kBtiC || w == kBtiJc) {
    if (available < kFarStubSize) {
      return false;
    }
    *landing = w;
    code_at = 4;
    std::memcpy(&w, b + 4, sizeof(w));
  }
  if (w != kLdrX16Lit8) {
    return false;
  }
  std::memcpy(&w, b + code_at + 4, sizeof(w));
  if (w != kBrX16Word) {
    return false;
  }
  std::memcpy(body, b + code_at + 8, sizeof(*body));
  *site_size = code_at + kFarPadSize;
  return true;
}

// bti jc; ldr x16, slot; br x16. The arm64 form of x64's indirect jump: the
// slot is a literal within 1MB. Unlike x64 this is two instructions, so a
// thread stopped between them holds the destination in x16, which removal
// scans for (see any_return_in_ranges).
inline constexpr size_t kIndirectJumpSize = 12;

inline stub_bytes create_indirect_jump(uintptr_t at, uintptr_t slot) {
  const intptr_t rel =
      static_cast<intptr_t>(slot) - static_cast<intptr_t>(at + 4);
  if ((at & 3u) != 0 || (rel & 3) != 0 || rel < -0x100000 || rel >= 0x100000) {
    return {};
  }
  const uint32_t ldr =
      0x58000000u | ((static_cast<uint32_t>(rel / 4) & 0x7FFFFu) << 5) | 16u;
  const uint32_t words[] = {kBtiJc, ldr, kBrX16Word};
  stub_bytes bytes(kIndirectJumpSize);
  std::memcpy(bytes.data(), words, sizeof(words));
  return bytes;
}

inline bool try_decode_indirect_jump(const void *bytes, size_t available,
                                     uintptr_t at, uintptr_t *slot) noexcept {
  if (bytes == nullptr || slot == nullptr || available < kIndirectJumpSize) {
    return false;
  }
  uint32_t w[3] = {};
  std::memcpy(w, bytes, sizeof(w));
  if (w[0] != kBtiJc || (w[1] & 0xFF00001Fu) != 0x58000010u ||
      w[2] != kBrX16Word) {
    return false;
  }
  const int32_t imm19 = static_cast<int32_t>(w[1] << 8) >> 13;
  *slot = at + 4 + static_cast<intptr_t>(imm19) * 4;
  return true;
}

} // namespace arm64
#endif

// Pool block layouts, the same offsets on both architectures.
//
// Anchor: the indirect jump the target's landing reaches, the head slot it
// jumps through, the anchor that owns the block (validated before decoding
// trusts it), then the relocated original prologue.
inline constexpr size_t kAnchorHeadOffset = 16;
inline constexpr size_t kAnchorOwnerOffset = 24;
inline constexpr size_t kAnchorPrefixSize = 32;

// Node: a hook's entry stub at the start, then its original() stub, an
// indirect jump through the next slot that follows it. The slot is 8-byte
// aligned so its loads and stores are single-copy atomic.
inline size_t indirect_jump_size_for_arch(Arch arch);
inline size_t node_original_offset(Arch arch);
inline size_t node_next_offset(Arch arch) {
  return (node_original_offset(arch) + indirect_jump_size_for_arch(arch) + 7) &
         ~size_t{7};
}
inline size_t node_block_size(Arch arch) {
  return node_next_offset(arch) + sizeof(uintptr_t);
}

// Fixed stub size for an architecture. The body in the pool block is always
// this many bytes; the target holds a short or far landing that jumps to it.
inline size_t stub_size_for_arch(Arch arch) {
  switch (arch) {
  case Arch::kX86_64:
    return x64::kStubSize;
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return arm64::kStubSize;
#endif
  default:
    // kX86 has no detour backend; anything else is invalid.
    assert(false && "unsupported architecture for stub codec");
    return x64::kStubSize;
  }
}

inline size_t node_original_offset(Arch arch) {
  return stub_size_for_arch(arch);
}

inline size_t indirect_jump_size_for_arch(Arch arch) {
  switch (arch) {
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return arm64::kIndirectJumpSize;
#endif
  default:
    return x64::kIndirectJumpSize;
  }
}

inline stub_bytes create_indirect_jump_for_arch(Arch arch, uintptr_t at,
                                                uintptr_t slot) {
  switch (arch) {
  case Arch::kX86_64:
    return x64::create_indirect_jump(at, slot);
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return arm64::create_indirect_jump(at, slot);
#endif
  default:
    return {};
  }
}

inline bool try_decode_indirect_jump_for_arch(Arch arch, const void *bytes,
                                              size_t available, uintptr_t at,
                                              uintptr_t *slot) {
  switch (arch) {
  case Arch::kX86_64:
    return x64::try_decode_indirect_jump(bytes, available, at, slot);
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return arm64::try_decode_indirect_jump(bytes, available, at, slot);
#endif
  default:
    return false;
  }
}

// A nonzero landing preserves the architecture's indirect-branch pad.
inline size_t short_stub_size_for_arch(Arch arch, uint32_t landing) {
  switch (arch) {
  case Arch::kX86_64:
    return landing == 0 ? x64::kBranchPadSize : x64::kShortStubSize;
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return landing == arm64::kNoLanding ? arm64::kBranchPadSize
                                        : arm64::kShortStubSize;
#endif
  default:
    (void)landing;
    assert(false && "unsupported architecture for stub codec");
    return x64::kShortStubSize;
  }
}

inline size_t far_stub_size_for_arch(Arch arch, uint32_t landing) {
  switch (arch) {
  case Arch::kX86_64:
    return landing == 0 ? x64::kFarPadSize : x64::kFarStubSize;
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return landing == arm64::kNoLanding ? arm64::kFarPadSize
                                        : arm64::kFarStubSize;
#endif
  default:
    (void)landing;
    assert(false && "unsupported architecture for stub codec");
    return x64::kFarStubSize;
  }
}

inline stub_bytes create_short_jump_for_arch(Arch arch, uintptr_t from,
                                             uintptr_t to, uint32_t landing) {
  switch (arch) {
  case Arch::kX86_64:
    return x64::create_short_jump(from, to, landing != 0);
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return arm64::create_short_jump(from, to, landing);
#endif
  default:
    (void)landing;
    return {};
  }
}

inline stub_bytes create_far_jump_for_arch(Arch arch, uintptr_t to,
                                           uint32_t landing) {
  switch (arch) {
  case Arch::kX86_64:
    return x64::create_far_jump(to, landing != 0);
#if SPUD_AARCH64_SUPPORT
  case Arch::kArm64:
    return arm64::create_far_jump(to, landing);
#endif
  default:
    (void)landing;
    return {};
  }
}

} // namespace spud::detail

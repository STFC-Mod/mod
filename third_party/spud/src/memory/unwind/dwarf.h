// The DWARF format shared by Linux and Apple: the source frame, the
// translator dwarf.cc implements, and the lookups each platform supplies.
#pragma once

#include "memory/unwind.h"

#include <spud/arch.h>

#if SPUD_UNWIND_SUPPORT && (SPUD_OS_LINUX || SPUD_OS_APPLE)
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace spud::detail::dwarf {

constexpr size_t kMetadataLimit = 65536;
#if defined(SPUD_ARCH_ARM64)
constexpr bool kArm64 = true;
#else
constexpr bool kArm64 = false;
#endif

struct writer {
  std::vector<uint8_t> bytes;
  void fixed(uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) {
      bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
  }
  void uleb(uint64_t value) {
    do {
      const auto byte = static_cast<uint8_t>(value & 0x7F);
      value >>= 7;
      fixed(value != 0 ? byte | 0x80 : byte, 1);
    } while (value != 0);
  }
  void sleb(int64_t value) {
    for (;;) {
      const auto byte = static_cast<uint8_t>(value & 0x7F);
      value >>= 7;
      const bool done = (value == 0 && (byte & 0x40) == 0) ||
                        (value == -1 && (byte & 0x40) != 0);
      fixed(done ? byte : byte | 0x80, 1);
      if (done) {
        return;
      }
    }
  }
  void append(std::span<const uint8_t> value) {
    bytes.insert(bytes.end(), value.begin(), value.end());
  }
  void finish(size_t begin) {
    while (bytes.size() % 8 != 0) {
      fixed(0, 1);
    }
    const auto length = static_cast<uint32_t>(bytes.size() - begin - 4);
    std::memcpy(bytes.data() + begin, &length, 4);
  }
};

struct code_fragment {
  uintptr_t begin;
  size_t length;
  std::vector<uint8_t> code;
  std::vector<bool> visited;
};

struct frame {
  uint64_t code_align = 1;
  int64_t data_align = -8;
  uint64_t ra = kArm64 ? 30 : 16;
  uintptr_t begin = 0;
  size_t length = 0;
  uintptr_t personality = 0;
  uintptr_t lsda = 0;
  uintptr_t text = 0;
  uintptr_t data = 0;
  uintptr_t instructions_address = 0;
  uint8_t pc_encoding = 0;
  bool signal = false;
  // AArch64 CIE flags without augmentation data: return addresses signed
  // with the B key, and MTE-tagged stack frames.
  bool b_key = false;
  bool mte_tagged = false;
  std::vector<uint8_t> initial;
  std::vector<uint8_t> instructions;
  std::vector<std::pair<uintptr_t, std::vector<uint8_t>>> metadata;
  // Where the function's code is read from; see source_view.
  source_view code;
  // Code that landing paths run through, starting with the function body,
  // and the offsets already decoded. Shared by every call site and range
  // translated from this frame.
  std::vector<code_fragment> fragments;
};

// CIE rules at function entry: CFA is SP before the call, and x86-64 finds
// its return address at CFA - 8.
std::vector<uint8_t> entry_rules();

// Direct AArch64 branches: B, B.cond and BC.cond, CBZ/CBNZ, TBZ/TBNZ, and
// CB<cc> compare-and-branch. The displacement is in instruction words.
struct arm64_branch {
  int64_t words;
  bool unconditional;
};
std::optional<arm64_branch> decode_arm64_branch(uint32_t word);

// [begin, end) of an FDE into `f`, parsed from records read in place: no
// pointer is dereferenced and nothing is snapshotted.
bool parse_extent(std::span<const uint8_t> fde_bytes, uintptr_t fde,
                  std::span<const uint8_t> cie_bytes, uintptr_t cie, frame &f);

// Reads the FDE at `fde` and its CIE into `f`, which already holds the
// lookup's text and data bases, and snapshots both records.
bool read_fde(uintptr_t fde, frame &f);

// One translated record: an address range, the CIE body its rules need,
// its CFI program and its LSDA.
struct record {
  uintptr_t pc;
  size_t length;
  std::vector<uint8_t> cie;
  std::vector<uint8_t> cfi;
  std::vector<uint8_t> lsda;
};

struct translation {
  std::vector<record> records;
  std::vector<std::pair<uintptr_t, std::vector<uint8_t>>> snapshots;
};

// Records for an allocation, as unwind_registration::prepare describes it,
// and the source bytes they depend on. false records a refusal reason.
bool translate(uintptr_t allocation, size_t size, uintptr_t entry,
               uintptr_t source, size_t copied,
               const trampoline_buffer &trampoline,
               std::span<const size_t> instruction_offsets, size_t patch_size,
               source_view code, translation &out);

// .eh_frame bytes for `records`: CIEs and FDEs, a zero terminator, then the
// LSDAs the FDEs point to by absolute address. Identical CIEs are shared.
std::vector<uint8_t> encode(std::span<const record> records);

// Platform lookups, defined by unwind/linux.cc and unwind/apple.cc.
//
// The source frame for `source`: `found` stays false when the unwinder has
// no entry for it, which is not a refusal. `patch` is how many bytes at
// `source` the hook overwrites.
bool find_frame(uintptr_t source, size_t copied, size_t patch, frame &f,
                bool &found);
// [begin, end) of the unwind entry covering `pc`.
std::optional<std::pair<uintptr_t, uintptr_t>> covering_extent(uintptr_t pc);

#if SPUD_OS_APPLE
// Compact unwind, translated into `f` as DWARF rows (compact.cc).
bool snapshot_compact(uintptr_t source, uint32_t encoding, frame &f);
bool compact_to_dwarf(uintptr_t source, size_t copied, uint32_t encoding,
                      frame &f);
bool copied_calls(const frame &f, uintptr_t source, size_t copied, bool &calls);
#endif

} // namespace spud::detail::dwarf
#endif

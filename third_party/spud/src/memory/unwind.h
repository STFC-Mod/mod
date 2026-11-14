#pragma once

#include <spud/arch.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace spud::detail {
struct trampoline_buffer;

// Windows xdata RVAs must share the generated code's address window, so
// the pool reserves them before the allocation: a trampoline's records,
// or one frameless record for a leaf allocation such as a node block.
#if SPUD_OS_WIN && SPUD_UNWIND_SUPPORT
inline constexpr size_t kWindowsUnwindBytes = 640;
inline constexpr size_t kWindowsLeafUnwindBytes = 48;
#else
inline constexpr size_t kWindowsUnwindBytes = 0;
inline constexpr size_t kWindowsLeafUnwindBytes = 0;
#endif

// Relative to the trampoline entry. A veneer carries the frame state of
// the original branch; its instructions have no original code interval.
struct unwind_range {
  size_t begin;
  size_t end;
  size_t source_offset;
  enum class state { kCopied, kFixed } frame_state = state::kCopied;
};

struct stack_adjustment {
  size_t offset;
  int64_t displacement;
  struct register_slot {
    unsigned dwarf_register;
    int64_t stack_offset;
    enum class width { kWord, kQword } saved_width = width::kQword;
  };
  // A borrowed register retains its source value at SP + stack_offset.
  std::variant<std::monostate, register_slot, std::array<register_slot, 2>>
      saved_registers = {};

  bool has_saved_registers() const {
    return saved_registers.index() != 0;
  }
  const register_slot *saved_register(uint64_t reg) const {
    if (const auto *slot = std::get_if<register_slot>(&saved_registers))
      return slot->dwarf_register == reg ? slot : nullptr;
    if (const auto *pair =
            std::get_if<std::array<register_slot, 2>>(&saved_registers))
      for (const auto &slot : *pair)
        if (slot.dwarf_register == reg)
          return &slot;
    return nullptr;
  }
};

// [begin, end) of the function whose unwind entry starts exactly at
// `address`: its DWARF FDE, Apple unwind record or Windows RUNTIME_FUNCTION,
// with contiguous chained Windows fragments. On Apple the image's
// LC_FUNCTION_STARTS also bound it, which covers functions inside a
// coalesced compact entry. Empty when nothing starts there and in builds
// without SPUD_UNWIND_SUPPORT, whose binaries leave out the table readers.
// Takes the loader's lock on Linux and Apple; call it outside the suspend
// window.
std::optional<std::pair<uintptr_t, uintptr_t>>
function_extent(uintptr_t address);

// [address, next listed start) by the LC_FUNCTION_STARTS table of the
// 64-bit Mach-O image whose header is mapped at `header`. Empty when the
// table is missing or malformed, `address` is not listed, or it is the last
// entry. Built on every host; only Apple's function_extent calls it.
std::optional<std::pair<uintptr_t, uintptr_t>>
macho_function_extent(uintptr_t header, uintptr_t address);

// The relocator's snapshot of the source code. It starts with the original
// prologue even when a hook's landing covers that now, as when a transaction
// replaces a target's last hook, so translations read the source through it
// rather than from memory.
struct source_view {
  uintptr_t address = 0;
  std::span<const uint8_t> bytes;

  // Reads like read_code_prefix, with the bytes this view covers taken
  // from it.
  size_t read(uintptr_t at, std::span<uint8_t> out) const;
};

// Describes one executable allocation to the platform unwinder. The shared
// parts (source snapshots, dependencies, module retention bookkeeping) live
// in unwind.cc; each platform file defines `platform`, prepare, publish and
// the destructor: unwind/linux.cc, unwind/apple.cc and unwind/windows.cc.
// Refusals record a reason with note_failure() before returning false.
class unwind_registration {
public:
  // Defined with the destructor, where `platform` is complete.
  unwind_registration();
  ~unwind_registration();
  unwind_registration(const unwind_registration &) = delete;
  unwind_registration &operator=(const unwind_registration &) = delete;

  // Preparation and registration run before suspension.
  // A frameless record over the whole allocation.
  bool prepare_leaf(uintptr_t allocation, size_t size);
  // Records for a trampoline at `entry` whose first `copied` bytes came from
  // `source`. A stub prefix before `entry` gets a leaf record. false means
  // source metadata was found but could not be translated safely.
  // `patch_size` is the overwritten prefix, `copied` when zero.
  bool prepare(uintptr_t allocation, size_t size, uintptr_t entry,
               uintptr_t source, size_t copied,
               const trampoline_buffer &trampoline,
               std::span<const size_t> instruction_offsets = {},
               size_t patch_size = 0, source_view code = {});
  bool publish();
  bool source_matches() const;
  // Also checks the source snapshots of `registration`, a relay prepared
  // from the same source, at commit.
  void add_dependency(const unwind_registration *registration);
  // The pool must drop the module reference after releasing its mutex;
  // dlclose can run user destructors that allocate executable memory.
  void *release_source_module();
  static void close_source_module(void *module);
  static bool retain_source_module(uintptr_t source, void *&module);

  // Source bytes the records were derived from, by address. Commit rereads
  // them inside the stop window.
  using snapshot = std::pair<uintptr_t, std::vector<uint8_t>>;
  // `known` holds code read during preparation that the install does not
  // write, such as a function body without the patched prefix.
  static std::vector<snapshot> merge_snapshots(
      std::vector<snapshot> snapshots,
      std::span<const std::pair<uintptr_t, std::span<const uint8_t>>> known =
          {});

private:
  // The generated records and whatever the platform needs to unregister
  // them. Null until prepare succeeds.
  struct platform;

  std::vector<snapshot> source_bytes_;
  void *source_module_ = nullptr;
  std::unique_ptr<platform> platform_;
};

} // namespace spud::detail

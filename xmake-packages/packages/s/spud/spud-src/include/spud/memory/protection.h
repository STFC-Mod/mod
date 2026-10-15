#pragma once

#include <cstddef>
#include <cstdint>

namespace spud {

// Page access. Combine with | and test with has_protection().
enum class mem_protection : uint8_t {
  kNone = 0,
  kRead = 1 << 0,
  kWrite = 1 << 1,
  kExecute = 1 << 2,

  kReadWrite = kRead | kWrite,
  kReadExecute = kRead | kExecute,
  kReadWriteExecute = kRead | kWrite | kExecute,
};

constexpr mem_protection operator|(mem_protection a, mem_protection b) {
  return static_cast<mem_protection>(static_cast<uint8_t>(a) |
                                     static_cast<uint8_t>(b));
}

constexpr mem_protection operator&(mem_protection a, mem_protection b) {
  return static_cast<mem_protection>(static_cast<uint8_t>(a) &
                                     static_cast<uint8_t>(b));
}

constexpr mem_protection &operator|=(mem_protection &a, mem_protection b) {
  return a = a | b;
}

// True when `have` grants every access in `want`.
constexpr bool has_protection(mem_protection have, mem_protection want) {
  return (have & want) == want;
}

// Changes the protection of the pages under [address, address + size) and
// puts the previous protection back when it goes out of scope. The range
// may cross into a neighbouring mapping only when that mapping has the
// same protection. ok() is false when nothing was changed; last_os_error()
// then says why.
//
// kWrite alone grants read as well. On Apple platforms kReadWriteExecute
// on ordinary code becomes read-write with copy-on-write: the kernel
// refuses writable and executable together outside MAP_JIT, so the pages
// cannot run while the scope is open. Patch live code with the detour
// API, which goes through a separate writable alias instead.
class protection_scope {
public:
  protection_scope(uintptr_t address, size_t size, mem_protection protect);
  ~protection_scope();

  protection_scope(const protection_scope &) = delete;
  protection_scope &operator=(const protection_scope &) = delete;
  protection_scope(protection_scope &&other) noexcept;
  protection_scope &operator=(protection_scope &&other) noexcept;

  // Keeps the new protection: nothing is restored.
  void detach() {
    address_ = 0;
    size_ = 0;
    ok_ = false;
  }

  bool ok() const {
    return ok_;
  }

private:
  void restore();

  uintptr_t address_ = 0;
  size_t size_ = 0;
  uintptr_t aligned_address_ = 0;
  size_t aligned_size_ = 0;
  uintptr_t original_protection_ = 0;
  bool ok_ = false;
};
} // namespace spud

#pragma once

#include <spud/memory/protection.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <type_traits>

namespace spud {

struct memory_region {
  uintptr_t start = 0;
  size_t size = 0;
  mem_protection protection{};
};

// The mapping that contains `address`, or nullopt when nothing is mapped.
[[nodiscard]] std::optional<memory_region> query_memory(uintptr_t address);
// Changes protection until changed again. See protection_scope for a
// change that puts itself back.
[[nodiscard]] bool protect_memory(uintptr_t address, size_t size,
                                  mem_protection protection);

// Copy bytes out of or into this process. Reads go through the OS first,
// so an unreadable page fails instead of faulting; execute-only pages are
// made readable for the copy. Writes make the range writable for the copy,
// put the protection back, and flush the instruction cache when the
// mapping is executable. A range may cross adjacent mappings with
// different protections; each is handled on its own. A range with an
// unmapped hole fails before anything is written, but a protection change
// that fails part-way leaves the mappings before it written.
[[nodiscard]] bool read_memory(uintptr_t address, void *out, size_t size);
[[nodiscard]] bool write_memory(uintptr_t address, const void *in, size_t size);

[[nodiscard]] inline bool read_memory(uintptr_t address,
                                      std::span<std::byte> out) {
  return read_memory(address, out.data(), out.size());
}

[[nodiscard]] inline bool read_memory(uintptr_t address,
                                      std::span<uint8_t> out) {
  return read_memory(address, out.data(), out.size());
}

[[nodiscard]] inline bool write_memory(uintptr_t address,
                                       std::span<const std::byte> in) {
  return write_memory(address, in.data(), in.size());
}

[[nodiscard]] inline bool write_memory(uintptr_t address,
                                       std::span<const uint8_t> in) {
  return write_memory(address, in.data(), in.size());
}

// write_memory(address, {0x90, 0x90})
[[nodiscard]] inline bool write_memory(uintptr_t address,
                                       std::initializer_list<uint8_t> bytes) {
  return write_memory(address, bytes.begin(), bytes.size());
}

template <typename T>
  requires std::is_trivially_copyable_v<T>
[[nodiscard]] std::optional<T> read_memory(uintptr_t address) {
  T value{};
  if (!read_memory(address, &value, sizeof(T))) {
    return std::nullopt;
  }
  return value;
}

template <typename T>
  requires std::is_trivially_copyable_v<T>
[[nodiscard]] std::optional<T> read_memory(const T *address) {
  return read_memory<T>(reinterpret_cast<uintptr_t>(address));
}

namespace detail {
template <typename T> inline constexpr bool is_span_v = false;
template <typename T, size_t N>
inline constexpr bool is_span_v<std::span<T, N>> = true;
} // namespace detail

// Writes the object representation of `value`. A span is trivially
// copyable too, but its bytes are a pointer; spans take the overloads above.
template <typename T>
  requires std::is_trivially_copyable_v<T> && (!detail::is_span_v<T>)
[[nodiscard]] bool write_memory(uintptr_t address, const T &value) {
  return write_memory(address, &value, sizeof(T));
}

template <typename T>
  requires std::is_trivially_copyable_v<T> && (!detail::is_span_v<T>)
[[nodiscard]] bool write_memory(T *address, const T &value) {
  return write_memory(reinterpret_cast<uintptr_t>(address), value);
}

} // namespace spud

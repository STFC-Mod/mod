#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

// From:
// http://stackoverflow.com/questions/1082192/how-to-generate-random-variable-names-in-c-using-macros/17624752#17624752
//
#define SPUD_PP_CAT(a, b) SPUD_PP_CAT_I(a, b)
#define SPUD_PP_CAT_I(a, b) SPUD_PP_CAT_II(~, a##b)
#define SPUD_PP_CAT_II(p, res) res

namespace spud {
// Process-wide choice for new hook preparations. Enabled by default.
// Existing hooks keep their records until freed. A build with
// SPUD_UNWIND_SUPPORT=0 always reports disabled.
void set_unwind_support(bool enabled);
bool unwind_support_enabled();

// Raw pool allocation; free with the same size. executable_memory below
// does the pairing for you. A free with another size, a second free, or a
// pointer the pool never handed out asserts in debug builds and is ignored
// in release builds, leaving the block allocated. The pool cannot describe
// code written by the caller, so these allocations never carry unwind
// records, whatever set_unwind_support says.
void *alloc_executable_memory(size_t size);
// Not named `near`: <Windows.h> defines near (and far) to nothing.
void *alloc_executable_memory(size_t size, uintptr_t hint);
void free_executable_memory(void *ptr, size_t size);

// Readable, writable and executable memory from spud's pool, returned to
// the pool when the owner goes away. Write to it inside
// SPUD_SCOPED_JIT_WRITE() and call flush_cache() before running it, or use
// write_memory(), which does both.
class executable_memory {
public:
  executable_memory() = default;
  explicit executable_memory(size_t size)
      : data_(static_cast<std::byte *>(alloc_executable_memory(size))),
        size_(data_ != nullptr ? size : 0) {}
  // Prefers an address a direct branch at `hint` can reach (2GB on x86-64,
  // 128MB on arm64), and takes any address when nothing nearby is free.
  executable_memory(size_t size, uintptr_t hint)
      : data_(static_cast<std::byte *>(alloc_executable_memory(size, hint))),
        size_(data_ != nullptr ? size : 0) {}
  ~executable_memory() {
    reset();
  }

  executable_memory(executable_memory &&other) noexcept
      : data_(std::exchange(other.data_, nullptr)),
        size_(std::exchange(other.size_, 0)) {}
  executable_memory &operator=(executable_memory &&other) noexcept {
    if (this != &other) {
      reset();
      data_ = std::exchange(other.data_, nullptr);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }
  executable_memory(const executable_memory &) = delete;
  executable_memory &operator=(const executable_memory &) = delete;

  explicit operator bool() const {
    return data_ != nullptr;
  }
  std::byte *data() const {
    return data_;
  }
  size_t size() const {
    return size_;
  }
  uintptr_t address() const {
    return reinterpret_cast<uintptr_t>(data_);
  }
  std::span<std::byte> bytes() const {
    return {data_, size_};
  }

  // The code at `offset` as a function pointer, e.g. as<int (*)(int)>().
  template <typename Fn>
    requires std::is_pointer_v<Fn> &&
             std::is_function_v<std::remove_pointer_t<Fn>>
  Fn as(size_t offset = 0) const {
    return reinterpret_cast<Fn>(data_ + offset);
  }

  void reset() {
    if (data_ != nullptr)
      free_executable_memory(data_, size_);
    data_ = nullptr;
    size_ = 0;
  }

private:
  std::byte *data_ = nullptr;
  size_t size_ = 0;
};

// Apple MAP_JIT pages are write-protected per thread; this scope makes
// them writable on the calling thread until it ends. Nested scopes keep the
// thread writable until the outermost one ends. Elsewhere it does nothing.
class jit_write_scope {
public:
  jit_write_scope();
  ~jit_write_scope();

  jit_write_scope(const jit_write_scope &) = delete;
  jit_write_scope &operator=(const jit_write_scope &) = delete;
};

// Publishes writes to executable memory for instruction fetch. Returns
// false only when the platform cache operation itself failed.
[[nodiscard]] bool flush_cache(void *ptr, size_t size);
} // namespace spud

#define SPUD_SCOPED_JIT_WRITE()                                                \
  ::spud::jit_write_scope SPUD_PP_CAT(spud_jit_write, __COUNTER__)

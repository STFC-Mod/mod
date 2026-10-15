#pragma once

#include <spud/arch.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace spud {

inline constexpr size_t kUnlimitedMatches = static_cast<size_t>(-1);

// Tokens are whitespace-separated (space, tab, CR, LF) or packed without
// separators. A token is two hex digits, `?`, or `??`; both wildcard forms
// are one byte. A pattern needs at least one fixed byte, so empty and
// all-wildcard input fail with kNoFixedBytes.
enum class signature_error {
  kOk = 0,
  kOddDigit,
  kInvalidHex,
  kUnexpectedCharacter,
  kNoFixedBytes,
};

constexpr std::string_view to_string(signature_error error) noexcept {
  switch (error) {
  case signature_error::kOk:
    return "ok";
  case signature_error::kOddDigit:
    return "odd_digit";
  case signature_error::kInvalidHex:
    return "invalid_hex";
  case signature_error::kUnexpectedCharacter:
    return "unexpected_character";
  case signature_error::kNoFixedBytes:
    return "no_fixed_bytes";
  }
  return "unknown";
}

// signature_error converts to std::error_code like detour_error; kOk is the
// empty code.
const std::error_category &signature_category() noexcept;

inline std::error_code make_error_code(signature_error error) noexcept {
  return {static_cast<int>(error), signature_category()};
}

// A pattern that does not own its bytes. Position i matches when the
// scanned byte agrees with bytes[i] in the bits set in mask[i]. The parser
// writes masks of 0xFF or 0 and byte 0 at wildcards; a hand-built view may
// mask part of a byte (0xF0 for 4?). Scans anchor on whole fixed bytes, so a
// view with none falls back to a compare at every offset. compiled_signature
// and the result of sig() both convert to it.
struct signature_view {
  const uint8_t *bytes = nullptr;
  const uint8_t *mask = nullptr;
  size_t size = 0;
};

namespace detail {

constexpr bool is_pattern_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

constexpr int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

struct parse_status {
  signature_error error = signature_error::kOk;
  size_t offset = 0;
};

// The one grammar, shared by compile_signature and sig(). Calls
// emit(byte, fixed) per token; on an error some tokens may already have been
// emitted.
template <class Emit>
constexpr parse_status parse_pattern(std::string_view text, Emit &&emit) {
  size_t i = 0;
  bool any_fixed = false;
  auto skip_space = [&] {
    while (i < text.size() && is_pattern_space(text[i])) {
      ++i;
    }
  };
  skip_space();
  while (i < text.size()) {
    const size_t token = i;
    if (text[i] == '?') {
      ++i;
      if (i < text.size() && text[i] == '?') {
        ++i;
      }
      emit(uint8_t{0}, false);
    } else {
      const int hi = hex_nibble(text[i++]);
      if (hi < 0) {
        return {signature_error::kUnexpectedCharacter, token};
      }
      if (i >= text.size() || is_pattern_space(text[i])) {
        return {signature_error::kOddDigit, token};
      }
      const int lo = hex_nibble(text[i++]);
      if (lo < 0) {
        return {signature_error::kInvalidHex, token};
      }
      emit(static_cast<uint8_t>((hi << 4) | lo), true);
      any_fixed = true;
    }
    skip_space();
  }
  if (!any_fixed) {
    return {signature_error::kNoFixedBytes, 0};
  }
  return {};
}

// Deliberately not constexpr: reaching one from sig() turns a bad pattern
// into a compile error that names the problem.
inline void signature_has_an_invalid_token() {}
inline void signature_needs_a_fixed_byte() {}

} // namespace detail

// A pattern parsed at compile time by sig(). Capacity is the literal's
// length, an upper bound on its byte count.
template <size_t Capacity> struct static_signature {
  uint8_t bytes[Capacity] = {};
  uint8_t mask[Capacity] = {};
  size_t size = 0;

  constexpr operator signature_view() const {
    return {bytes, mask, size};
  }
};

// Parses a literal pattern at compile time: a malformed pattern does not
// compile, and scanning it never parses or allocates.
//
//   constexpr auto kPrologue = spud::sig("48 89 5C 24 ? 57");
//   auto hit = spud::find_first(kPrologue, bytes);
template <size_t N> consteval static_signature<N> sig(const char (&text)[N]) {
  static_signature<N> out;
  const auto status = detail::parse_pattern(
      std::string_view(text, N - 1), [&](uint8_t byte, bool fixed) {
        out.bytes[out.size] = byte;
        out.mask[out.size] = fixed ? uint8_t{0xFF} : uint8_t{0};
        ++out.size;
      });
  if (status.error == signature_error::kNoFixedBytes) {
    detail::signature_needs_a_fixed_byte();
  } else if (status.error != signature_error::kOk) {
    detail::signature_has_an_invalid_token();
  }
  return out;
}

// A pattern parsed at run time. When parsing fails it keeps the error and
// matches nothing.
class compiled_signature {
public:
  compiled_signature() = default;

  bool ok() const {
    return error_ == signature_error::kOk;
  }
  signature_error error() const {
    return error_;
  }
  // Offset of the bad token in the pattern text.
  size_t error_offset() const {
    return error_offset_;
  }
  size_t size() const {
    return bytes_.size();
  }
  std::span<const uint8_t> bytes() const {
    return bytes_;
  }
  std::span<const uint8_t> mask() const {
    return mask_;
  }

  operator signature_view() const {
    return {bytes_.data(), mask_.data(), bytes_.size()};
  }

private:
  friend compiled_signature compile_signature(std::string_view pattern);

  std::vector<uint8_t> bytes_;
  std::vector<uint8_t> mask_;
  signature_error error_ = signature_error::kNoFixedBytes;
  size_t error_offset_ = 0;
};

[[nodiscard]] compiled_signature compile_signature(std::string_view pattern);

// One occurrence, bounded by the buffer (or merged module mapping) it was
// found in. Every accessor stays inside that buffer and returns an empty
// value instead of reading past it. A default match is empty.
class signature_match {
public:
  signature_match() = default;
  signature_match(std::span<const uint8_t> buffer, size_t offset)
      : buffer_(buffer), offset_(offset) {}

  explicit operator bool() const {
    return buffer_.data() != nullptr;
  }

  uintptr_t address() const {
    return buffer_.data() == nullptr
               ? 0
               : reinterpret_cast<uintptr_t>(buffer_.data() + offset_);
  }

  // The address as a pointer. A pointer type is returned as is, so
  // as<int (*)(int)>() matches executable_memory::as; any other type T gives
  // T*, so as<int(int)>() is the same function pointer and as<void>() a
  // void*.
  template <class T = void> auto as() const {
    if constexpr (std::is_pointer_v<T>) {
      return reinterpret_cast<T>(address());
    } else {
      return reinterpret_cast<T *>(address());
    }
  }

  // The match moved by delta bytes, either way. Empty when that leaves the
  // buffer; the end of the buffer itself is still inside.
  signature_match adjust(ptrdiff_t delta) const {
    size_t moved = 0;
    if (!shifted(delta, &moved)) {
      return {};
    }
    return {buffer_, moved};
  }

  // The T stored `offset` bytes from the match, when all of it lies inside
  // the buffer.
  template <class T> std::optional<T> read(ptrdiff_t offset = 0) const {
    static_assert(std::is_trivially_copyable_v<T>);
    size_t at = 0;
    if (!shifted(offset, &at) || buffer_.size() - at < sizeof(T)) {
      return std::nullopt;
    }
    T value;
    std::memcpy(&value, buffer_.data() + at, sizeof(T));
    return value;
  }

  // Target of a RIP-relative rel32 in an instruction that starts at the
  // match: the end of the instruction plus the signed displacement at
  // disp_offset. `mov rax, [rip+X]` (48 8B 05 X) is rip(3, 7), `call X`
  // (E8 X) is rip(1, 5). 0 when the displacement is outside the buffer.
  uintptr_t rip(ptrdiff_t disp_offset, size_t insn_length) const {
    const auto disp = read<int32_t>(disp_offset);
    if (!disp) {
      return 0;
    }
    return address() + insn_length +
           static_cast<uintptr_t>(static_cast<intptr_t>(*disp));
  }

  // x86 relative CALL (E8 rel32) at the match. 0 when the match is not a
  // CALL or the instruction would read past the buffer.
  uintptr_t call_target() const {
    const auto opcode = read<uint8_t>();
    return opcode == uint8_t{0xE8} ? rip(1, 5) : 0;
  }

private:
  // offset_ + delta, when it stays within [0, size]. Written so that no
  // intermediate value can overflow, PTRDIFF_MIN included.
  bool shifted(ptrdiff_t delta, size_t *out) const {
    if (buffer_.data() == nullptr) {
      return false;
    }
    if (delta < 0) {
      const size_t back = static_cast<size_t>(-(delta + 1)) + 1;
      if (back > offset_) {
        return false;
      }
      *out = offset_ - back;
      return true;
    }
    const auto forward = static_cast<size_t>(delta);
    if (forward > buffer_.size() - offset_) {
      return false;
    }
    *out = offset_ + forward;
    return true;
  }

  std::span<const uint8_t> buffer_;
  size_t offset_ = 0;
};

// Matches in address order. Indexing past the end gives an empty match, so
// `find_in_module(...).front().address()` is 0 rather than undefined.
class signature_matches {
public:
  signature_matches() = default;
  explicit signature_matches(std::vector<signature_match> matches)
      : matches_(std::move(matches)) {}

  auto begin() const {
    return matches_.begin();
  }
  auto end() const {
    return matches_.end();
  }
  size_t size() const {
    return matches_.size();
  }
  bool empty() const {
    return matches_.empty();
  }
  signature_match operator[](size_t index) const {
    return index < matches_.size() ? matches_[index] : signature_match{};
  }
  signature_match front() const {
    return (*this)[0];
  }

private:
  std::vector<signature_match> matches_;
};

// Which matcher runs. kAutomatic picks the widest SIMD the CPU and OS
// support, except on x86 for a pattern whose rarest byte is rare in code,
// where the memchr-based scalar matcher is faster. Asking for a backend this
// build or CPU lacks runs the scalar matcher; every backend returns the same
// matches.
enum class scan_backend : uint8_t {
  kAutomatic,
  kScalar,
  kSse2,
  kAvx2,
  kNeon,
};

struct scan_options {
  // Stops the scan after this many matches. A short pattern over a whole
  // process can otherwise match millions of times.
  size_t max_results = kUnlimitedMatches;
  scan_backend backend = scan_backend::kAutomatic;
  // Threads find_first_each may split its patterns across, the calling
  // thread included. Other calls scan on the calling thread only.
  unsigned threads = 1;
};

// Text patterns are parsed on every call and match nothing when they fail
// to parse; compile_signature reports why. Pass a compiled_signature or a
// sig() pattern to skip the parse.
[[nodiscard]] signature_matches find_matches(signature_view pattern,
                                             std::span<const uint8_t> buffer,
                                             scan_options options = {});
[[nodiscard]] signature_matches find_matches(std::string_view pattern,
                                             std::span<const uint8_t> buffer,
                                             scan_options options = {});

// The first match without building a result list. max_results is ignored.
[[nodiscard]] signature_match find_first(signature_view pattern,
                                         std::span<const uint8_t> buffer,
                                         scan_options options = {});
[[nodiscard]] signature_match find_first(std::string_view pattern,
                                         std::span<const uint8_t> buffer,
                                         scan_options options = {});

// Readable, executable mappings of one module. An empty name is the main
// executable; otherwise a name matches the file name, the file name without
// its extensions, or a trailing part of the path.
[[nodiscard]] signature_matches find_in_module(signature_view pattern,
                                               std::string_view module = {},
                                               scan_options options = {});
[[nodiscard]] signature_matches find_in_module(std::string_view pattern,
                                               std::string_view module = {},
                                               scan_options options = {});

// The first match of each pattern in one module, in pattern order, with an
// empty match for a pattern that does not occur. The same results as
// find_in_module with max_results = 1 per pattern, but the module is
// enumerated once and read in cache-sized chunks that every pattern scans
// before the next chunk, so memory is read once rather than once per
// pattern. max_results is ignored; with threads above 1 the patterns are
// split across that many threads, each walking the module the same way.
//
//   constexpr spud::signature_view kPatterns[] = {kSpawn, kTick, kDraw};
//   const auto hits = spud::find_first_each(kPatterns, "Game.exe");
[[nodiscard]] std::vector<signature_match>
find_first_each(std::span<const signature_view> patterns,
                std::string_view module = {}, scan_options options = {});

// Every readable, executable mapping in the process.
[[nodiscard]] signature_matches find_in_process(signature_view pattern,
                                                scan_options options = {});
[[nodiscard]] signature_matches find_in_process(std::string_view pattern,
                                                scan_options options = {});

} // namespace spud

template <>
struct std::is_error_code_enum<spud::signature_error> : std::true_type {};

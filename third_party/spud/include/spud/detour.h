#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef SPUD_DETOUR_TRACING
#define SPUD_DETOUR_TRACING 0
#endif

#if SPUD_DETOUR_TRACING
#include <cstdio>
#include <source_location>
#endif

// Tracing changes the layout of the hook context and handle. Those types
// sit in an inline namespace when it is on, so a consumer built with the
// other setting than libspud fails to link instead of misreading them.
// MSVC's linker also names the mismatch.
#if SPUD_DETOUR_TRACING
#define SPUD_TRACING_ABI_BEGIN inline namespace tracing {
#define SPUD_TRACING_ABI_END }
#if defined(_MSC_VER)
#pragma detect_mismatch("spud_detour_tracing", "1")
#endif
#else
#define SPUD_TRACING_ABI_BEGIN
#define SPUD_TRACING_ABI_END
#if defined(_MSC_VER)
#pragma detect_mismatch("spud_detour_tracing", "0")
#endif
#endif

#include "arch.h"
#include "diagnostics.h"
#include "error.h"

namespace spud {

// Linux freezes sibling threads from a cloned tracer that shares this
// address space but not this thread group. Direct ptrace of a sibling
// is EPERM. Set true only when the caller accepts a torn write.
void allow_unguarded_patches(bool allowed);

// Frees removed hooks whose calls have all returned: hooks that removed
// themselves, request_uninstall(), and uninstalls that stopped waiting.
// Installs and uninstalls also do this on their way in. Returns how many
// removed hooks are still waiting.
[[nodiscard]] size_t collect();

// Decode and emit a trampoline for `address` without writing the target.
// The plan is for the short landing (5 or 9 bytes on x86-64, 4 or 8 on ARM64),
// which the first install on a target writes when the target's anchor block
// lands within branch reach. Later hooks on the target never write it.
// Otherwise install writes the far landing (14 or 18 bytes on x86-64, 16 or 20
// on ARM64), which reaches a block anywhere. Also refuses PAC and an unreadable
// prologue. It does not freeze threads or translate unwind metadata, so
// install can still return `kSuspensionUnavailable` or `kUnwindFailed` after
// a clean plan. `trampoline_size` is an upper
// bound: install rebuilds the trampoline at its final address, where
// PC-relative instructions that still reach keep their short form.
struct detour_plan {
  detour_error error = detour_error::kDecodeFailed;
  // The address install patches. A pure forwarder (jmp, import thunk) is
  // followed to its destination, which then carries the hook for every
  // caller; this is that destination, or the input address.
  uintptr_t resolved_target = 0;
  // The short landing size, and the instruction-aligned bytes it displaces.
  // Fewer required bytes than the landing means kInsufficientBytes.
  size_t jump_size = 0;
  size_t required_patch_bytes = 0;
  size_t trampoline_size = 0;
  size_t relocated_instruction_count = 0;
  // The same pair for the far landing; set only when the short landing
  // fits. Fewer required bytes than the landing means install fails with
  // kInsufficientBytes if the pool cannot place the block within reach.
  size_t far_jump_size = 0;
  size_t far_required_patch_bytes = 0;
  // The target already holds spud's landing. Another install links into
  // its chain and writes nothing: the sizes describe the landing and the
  // copy in place, and no far landing or instruction count is reported.
  bool hooked = false;

  bool ok() const {
    return error == detour_error::kOk;
  }
};

// A nonzero function_size scans that complete contiguous instruction extent
// (at most 64KB), including code after early returns. Zero uses the bounded
// end: an unwind table entry starting at the target when one exists, or on
// Apple the image's LC_FUNCTION_STARTS, otherwise the scan heuristic. An
// explicit extent refuses an entry jump to another target. An extent shorter
// than the patch is accepted when it ends in a return, jump or trap followed by
// padding the patch can overwrite.
[[nodiscard]] detour_plan inspect_detour(uintptr_t address,
                                         Arch arch = Arch::kHost,
                                         size_t function_size = 0);

template <typename T>
  requires std::is_pointer_v<T>
[[nodiscard]] detour_plan inspect_detour(T target, Arch arch = Arch::kHost,
                                         size_t function_size = 0) {
  return inspect_detour(reinterpret_cast<uintptr_t>(target), arch,
                        function_size);
}

// What this build can do for `arch`. `protected_patch` is the OS mechanism,
// not a live probe; install still checks again at commit time.
struct detour_capabilities {
  bool decode = false;
  bool emit = false;
  bool execute = false;
  bool protected_patch = false;
};

[[nodiscard]] detour_capabilities
query_detour_capabilities(Arch arch = Arch::kHost);

// What this process is running with. Generated stubs retain IBT/BTI pads;
// x86 target entries preserve endbr64 only when already present. PAC cannot
// be satisfied from here, so install returns `kControlFlowEnforced`.
struct control_flow {
  // Not detected; user space has no portable IBT status query. Stubs carry
  // endbr64 landing pads either way; target patches keep existing pads only.
  bool ibt = false;
  bool shadow_stack = false;
  bool cfg = false;
  bool bti = false;
  bool pac = false;
};

[[nodiscard]] control_flow query_control_flow();

namespace detail {
SPUD_TRACING_ABI_BEGIN

#if SPUD_DETOUR_TRACING
using trace_location = std::source_location;
#else
struct trace_location {
  static constexpr trace_location current() noexcept {
    return {};
  }
};
#endif

// What a hook's wrapper reads. It sits right before the hook's entry
// counts (if it has any), and the entry stub hands the wrapper the counts'
// address, so both must stay alive at a stable address while the hook can
// be entered. The rest of a hook's state is internal (src/detour/chain.h).
struct alignas(64) detour_context {
  uintptr_t func = 0;
  // This hook's original(): a jump through its next slot, so the address
  // stays valid while hooks below it come and go.
  uintptr_t trampoline = 0;
  // The hook object for wrappers that call a stateful callable. Its box is
  // shared by the handle and by a removed hook that is still draining.
  void *callable = nullptr;
#if SPUD_DETOUR_TRACING
  std::source_location location;
#endif
};
static_assert(sizeof(detour_context) == 64,
              "the stub reaches the context one cache line before the counts");

SPUD_TRACING_ABI_END

// Calls currently executing a removable hook, spread over cache-line shards
// so threads calling one hook at once do not all bounce a single line. The
// jump stub picks a shard from its stack pointer and increments it on entry
// (so every entry path is counted), then hands the shard's address to the
// wrapper, which decrements that same shard on exit. Once a hook is
// unlinked nothing new is counted, and its memory is freed after all shards
// read zero. A permanent hook's stub counts nothing and has no shards.
struct alignas(64) entry_counts {
  struct alignas(64) shard {
    std::atomic<uint64_t> count{0};
  };
  static constexpr size_t kShards = 16;
  shard shards[kShards];

  // Whether any call is inside this hook. Exact only while every thread that
  // could enter is stopped; otherwise a snapshot. Calls leave with a release
  // decrement, so once this reads all zero their writes, including to the
  // callable, happen before whatever frees the hook next.
  bool active() const {
    for (const shard &s : shards) {
      if (s.count.load(std::memory_order_relaxed) != 0)
        return true;
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    return false;
  }
};
static_assert(sizeof(entry_counts::shard) == 64,
              "each entry shard must own one cache line");
static_assert(std::atomic<uint64_t>::is_always_lock_free &&
                  sizeof(std::atomic<uint64_t>) == sizeof(uint64_t),
              "entry shards must be plain lock-free words");

// A hook's context, chain links and entry counts. Defined in
// src/detour/chain.h.
struct hook_node;

// A stateful hook object and its reference count. The handle holds one
// reference. A removed hook that calls may still be running holds another
// until it is freed, and a detached or permanent hook holds one forever.
struct callable_box {
  std::atomic<size_t> refs{1};
  void *object = nullptr;
  void (*destroy)(callable_box *) = nullptr;
};

template <typename Fn> struct boxed_callable final : callable_box {
  Fn fn;
  template <typename F>
  explicit boxed_callable(F &&f) : fn(std::forward<F>(f)) {
    object = &fn;
    destroy = [](callable_box *box) {
      delete static_cast<boxed_callable *>(box);
    };
  }
};

inline void retain_callable(callable_box *box) {
  if (box != nullptr)
    box->refs.fetch_add(1, std::memory_order_relaxed);
}

inline void release_callable(callable_box *box) {
  if (box != nullptr && box->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
    box->destroy(box);
}

// Decrements the entry shard the stub counted this call in when the wrapper
// exits, including on exceptions. Release, so the hook's last writes are
// ordered before a remover that sees zero and frees its state; with
// unguarded patches on ARM64 the counts are all removal has to go on.
struct active_guard {
  explicit active_guard(std::atomic<uint64_t> *entry) : entry_(entry) {}
  ~active_guard() {
    entry_->fetch_sub(1, std::memory_order_release);
  }
  active_guard(const active_guard &) = delete;
  active_guard &operator=(const active_guard &) = delete;
  std::atomic<uint64_t> *entry_;
};

struct staged_unlink;
struct prepared_install;
struct hook_step;

// Applies transaction steps; a friend of detour.
detour_error commit(std::span<hook_step> steps);

SPUD_TRACING_ABI_BEGIN

class trampoline_pin {
public:
  trampoline_pin() = default;
  ~trampoline_pin() {
    reset();
  }
  trampoline_pin(const trampoline_pin &) = delete;
  trampoline_pin &operator=(const trampoline_pin &) = delete;
  trampoline_pin(trampoline_pin &&other) noexcept
      : address_(std::exchange(other.address_, 0)),
        pins_(std::exchange(other.pins_, nullptr)) {}
  trampoline_pin &operator=(trampoline_pin &&other) noexcept {
    if (this != &other) {
      reset();
      address_ = std::exchange(other.address_, 0);
      pins_ = std::exchange(other.pins_, nullptr);
    }
    return *this;
  }
  uintptr_t address() const {
    return address_;
  }
  void reset() noexcept {
    if (pins_ != nullptr)
      pins_->fetch_sub(1, std::memory_order_release);
    pins_ = nullptr;
    address_ = 0;
  }

private:
  friend struct detour;
  uintptr_t address_ = 0;
  // The hook's lease count, which keeps it from being freed.
  std::atomic<uint64_t> *pins_ = nullptr;
};

struct detour {
public:
  ~detour() {
    remove();
    drop_callable();
  }

  // This hook's original(). It jumps through a slot that removing a hook
  // below rewrites, so the address stays callable while this hook is in.
  uintptr_t trampoline() const;

  detour_error last_error() const {
    return last_error_;
  }
  // errno / GetLastError / kern_return_t behind last_error(), or 0 when the
  // last operation succeeded or failed above the OS. Stable per handle; see
  // spud::last_os_error() for the calling thread's most recent code.
  int last_os_error() const {
    return last_os_error_;
  }
  bool is_installed() const {
    return state_ == handle_state::kInstalled;
  }
  bool is_detached() const {
    return state_ == handle_state::kDetached;
  }

  // Set before install. The caller supplies the complete contiguous code
  // extent, including code beyond early returns; zero restores the heuristic.
  // Installed and detached handles cannot change their extent.
  [[nodiscard]] bool set_function_size(size_t size) {
    if (state_ != handle_state::kIdle || size > 0x10000 ||
        size > UINTPTR_MAX - address_)
      return false;
    function_size_ = size;
    return true;
  }

  detour(detour &&other) noexcept {
    take(other);
  }

  detour &operator=(detour &&other) noexcept {
    if (this != &other) {
      // Frees everything this object owns (unlinks when installed, drops the
      // spare context otherwise); detached objects own nothing. A hook that
      // cannot be removed keeps running and keeps its callable, unowned.
      remove();
      drop_callable();
      take(other);
    }
    return *this;
  }

protected:
  detour() = default;
  // `uncounted` is the same wrapper without the exit decrement, for
  // permanent installs whose stubs do not count entries.
  detour(uintptr_t address, uintptr_t func, uintptr_t wrapper,
         uintptr_t uncounted, trace_location location)
      : address_(address), func_(func), wrapper_(wrapper),
        uncounted_wrapper_(uncounted), location_(location) {}

  // Takes ownership of the object a callable wrapper reads from its context.
  void adopt_callable(callable_box *callable) {
    callable_ = callable;
  }
  detour_error install(Arch arch = Arch::kHost);
  // Installs without entry counting and detaches the handle on success.
  detour_error install_permanent(Arch arch);
  // Unlinks the hook, then waits a bounded time for calls still inside it
  // before freeing its memory. What has not drained by then is retired.
  detour_error remove();
  // Unlinks the hook and retires its memory without waiting.
  detour_error request_remove();
  trampoline_pin pin_trampoline();
  detour &detach();

private:
  friend detour_error detail::commit(std::span<hook_step> steps);

  detour(detour const &) = delete;
  detour &operator=(detour const &) = delete;

  // A handle without a node takes *spare when one is given, and uses what
  // `prepared` holds, since a transaction's suspend window cannot allocate.
  detour_error install_impl(Arch arch, bool permanent = false,
                            hook_node **spare = nullptr,
                            prepared_install *prepared = nullptr);
  // With staged set, the node and its memory are handed to *staged instead
  // of released. Otherwise they are freed once drained (only waited for
  // when `wait`) or retired.
  detour_error unlink(staged_unlink *staged, bool wait = true);
  detour_error relink(staged_unlink &staged);

  // Drops the thread-local OS code, runs `op`, then snapshots that code onto
  // this handle when the result is a failure.
  template <class Op> detour_error record_error(Op &&op) {
    detail::clear_os_error();
    const detour_error err = op();
    // A layout retried after a refusal leaves its reason behind.
    if (err == detour_error::kOk)
      detail::clear_os_error();
    last_error_ = err;
    last_os_error_ = err == detour_error::kOk ? 0 : spud::last_os_error();
    return last_error_;
  }

  // Lets go of the callable. An installed hook the handle failed to remove
  // can still call it, so it is leaked with that hook's node. Detached
  // and draining hooks hold references of their own.
  void drop_callable() noexcept {
    if (state_ != handle_state::kInstalled) {
      release_callable(callable_);
    }
    callable_ = nullptr;
  }

  void take(detour &other) noexcept {
    address_ = other.address_;
    callable_ = std::exchange(other.callable_, nullptr);
    function_size_ = other.function_size_;
    func_ = other.func_;
    wrapper_ = other.wrapper_;
    uncounted_wrapper_ = other.uncounted_wrapper_;
    wrappers_resolved_ = other.wrappers_resolved_;
    arch_ = other.arch_;
    node_ = other.node_;
    state_ = other.state_;
    location_ = other.location_;
    last_error_ = other.last_error_;
    last_os_error_ = other.last_os_error_;
    other.node_ = nullptr;
    other.state_ = handle_state::kIdle;
  }

  uintptr_t address_ = 0;
  size_t function_size_ = 0;
  uintptr_t func_ = 0;
  uintptr_t wrapper_ = 0;
  uintptr_t uncounted_wrapper_ = 0;
  bool wrappers_resolved_ = false;
  Arch arch_ = Arch::kHost;
  callable_box *callable_ = nullptr;

  // Allocated by the first install. idle: this handle owns node_, if any,
  // and it is not in a chain. installed: node_ is live in a chain and this
  // handle must uninstall it. detached: the chain owns node_; this handle
  // owns nothing.
  hook_node *node_ = nullptr;
  enum class handle_state : uint8_t { kIdle, kInstalled, kDetached };
  handle_state state_ = handle_state::kIdle;
  trace_location location_;
  detour_error last_error_ = detour_error::kOk;
  int last_os_error_ = 0;
};

SPUD_TRACING_ABI_END

// The jump stub leaves the address of the hook's entry counts in r11 (x11
// on arm64), with its context one cache line before it, and, for a counted
// hook, the entry shard it incremented in r10 (x10). Neither ABI
// uses those for arguments. The wrapper must read them before anything else
// can reuse those scratch registers, so the read is inline and the wrapper
// opts out of entry instrumentation (-finstrument-functions, -pg, XRay),
// which would run first and is free to clobber them. GCC instruments inlined
// callees too, so the attribute is on the read as well.
struct stub_registers {
  detour_context *ctx;
  // Garbage for a permanent hook, whose stub does not count.
  std::atomic<uint64_t> *entry;
};
#if defined(__GNUC__) || defined(__clang__)
#if defined(__clang__)
#define SPUD_WRAPPER_ATTRS                                                     \
  __attribute__((no_instrument_function, no_profile_instrument_function,       \
                 xray_never_instrument))
#else
#define SPUD_WRAPPER_ATTRS __attribute__((no_instrument_function))
#endif
[[gnu::always_inline]] SPUD_WRAPPER_ATTRS inline stub_registers
read_stub_registers() {
  entry_counts *counts;
  std::atomic<uint64_t> *entry;
  // One statement with both registers clobbered, so neither output can be
  // allocated to a register the other move still has to read.
#if defined(SPUD_ARCH_X86_64)
  __asm__ volatile("mov %%r11, %0\n\tmov %%r10, %1"
                   : "=r"(counts), "=r"(entry)
                   :
                   : "r10", "r11");
#else
  __asm__ volatile("mov %0, x11\n\tmov %1, x10"
                   : "=r"(counts), "=r"(entry)
                   :
                   : "x10", "x11");
#endif
  return {reinterpret_cast<detour_context *>(counts) - 1, entry};
}
#else
#define SPUD_WRAPPER_ATTRS
} // namespace detail
} // namespace spud
// MSVC has no x64 inline assembly; detour_context_support.masm stores r10
// through `entry` and returns r11, in one call so nothing runs in between.
extern "C" uintptr_t spud_read_context_value(uintptr_t *entry);
namespace spud {
namespace detail {
inline stub_registers read_stub_registers() {
  uintptr_t entry = 0;
  const uintptr_t counts = spud_read_context_value(&entry);
  return {reinterpret_cast<detour_context *>(counts) - 1,
          reinterpret_cast<std::atomic<uint64_t> *>(entry)};
}
#endif

// Foreign Windows exceptions can bypass the compiler's C++ destructor
// handling, so there the wrapper balances the entry count in a native
// termination scope.
#if defined(SPUD_OS_WIN) && defined(_MSC_VER)
#define SPUD_WRAPPER_SEH 1
#elif defined(SPUD_OS_WIN) && defined(__clang__)
#if !__is_identifier(__try)
#define SPUD_WRAPPER_SEH 1
#endif
#endif
#ifndef SPUD_WRAPPER_SEH
#define SPUD_WRAPPER_SEH 0
#endif

template <typename T>
concept Address = requires(T a) {
  { a } -> std::convertible_to<std::uintptr_t>;
} || std::is_pointer_v<T>;

} // namespace detail

template <typename T> struct detour;

template <typename Signature> class trampoline_lease {
public:
  trampoline_lease() = default;
  trampoline_lease(trampoline_lease &&) noexcept = default;
  trampoline_lease &operator=(trampoline_lease &&) noexcept = default;
  Signature *get() const {
    return reinterpret_cast<Signature *>(pin_.address());
  }
  explicit operator bool() const {
    return get() != nullptr;
  }
  void reset() noexcept {
    pin_.reset();
  }

private:
  template <typename T> friend struct detour;
  explicit trampoline_lease(detail::trampoline_pin pin)
      : pin_(std::move(pin)) {}
  detail::trampoline_pin pin_;
};

template <typename R, typename... Args>
struct detour<R(R (*)(Args...), Args...)> : public detail::detour {
public:
  using func_t = R(R (*)(Args...), Args...);
  using trampoline_t = R(Args...);
  using Self = detour<R(R (*)(Args...), Args...)>;

  // An empty handle. Assign a hook to it; until then try_install() returns
  // kNotFound.
  detour() = default;

  // Hooks `target` with a callable taking the next callable in the chain
  // followed by the target's arguments. A captureless lambda is called
  // directly from the wrapper, so it can be inlined there; anything with
  // state is moved into the handle and lives until the handle is gone and
  // no removed hook can still call it.
  template <typename F>
    requires std::is_class_v<std::remove_cvref_t<F>> &&
             std::is_invocable_r_v<R, std::remove_cvref_t<F> &, trampoline_t *,
                                   Args...>
  detour(trampoline_t *target, F &&hook,
         detail::trace_location location = detail::trace_location::current())
      : detail::detour(
            reinterpret_cast<uintptr_t>(target), 0,
            reinterpret_cast<uintptr_t>(
                &wrapper<call_object<std::remove_cvref_t<F>>, true>),
            reinterpret_cast<uintptr_t>(
                &wrapper<call_object<std::remove_cvref_t<F>>, false>),
            location) {
    using Fn = std::remove_cvref_t<F>;
    if constexpr (!stateless<Fn>) {
      adopt_callable(new detail::boxed_callable<Fn>(std::forward<F>(hook)));
    }
  }

  // Function pointer hooks, including a function template whose first
  // parameter is deduced from the target.
  detour(trampoline_t *target, func_t *hook,
         detail::trace_location location = detail::trace_location::current())
      : detail::detour(
            reinterpret_cast<uintptr_t>(target),
            reinterpret_cast<uintptr_t>(hook),
            reinterpret_cast<uintptr_t>(&wrapper<call_pointer, true>),
            reinterpret_cast<uintptr_t>(&wrapper<call_pointer, false>),
            location) {}

  template <detail::Address T, detail::Address F>
  static auto
  create(T target, F func,
         detail::trace_location location = detail::trace_location::current()) {
    return Self{
        reinterpret_cast<uintptr_t>(target), reinterpret_cast<uintptr_t>(func),
        reinterpret_cast<uintptr_t>(&wrapper<call_pointer, true>),
        reinterpret_cast<uintptr_t>(&wrapper<call_pointer, false>), location};
  }

  trampoline_t *trampoline() const {
    return reinterpret_cast<trampoline_t *>(this->detail::detour::trampoline());
  }
  // Keep this lease alive while a callable or saved context can reach the
  // trampoline outside a call. Removing the hook still unlinks it, but its
  // original() stays callable, skipping hooks removed below it, until the
  // lease is released; spud::collect() frees it after that.
  [[nodiscard]] trampoline_lease<trampoline_t> pin_trampoline() {
    return trampoline_lease<trampoline_t>{
        this->detail::detour::pin_trampoline()};
  }
  [[nodiscard]] detour_error try_install(Arch arch = Arch::kHost) {
    return this->detail::detour::install(arch);
  }

  // Installs a hook that can never be removed, and detaches the handle once
  // it is in (see detach()). Removable hooks count every call in and out so
  // removal can wait for them; a permanent hook skips both atomic updates, so
  // threads calling it at once never share a counter line.
  [[nodiscard]] detour_error try_install_permanent(Arch arch = Arch::kHost) {
    return this->detail::detour::install_permanent(arch);
  }

  // Unlinks the hook so new calls skip it, then waits up to five seconds
  // for calls already inside it and frees its memory. Calls still running
  // after that, including the one making this call when a hook removes
  // itself, keep the memory and callable alive until they return, and
  // spud::collect() frees them afterwards. A trampoline lease does the same
  // until it is released. Returns kOk once the hook is unlinked.
  [[nodiscard]] detour_error uninstall() {
    return this->detail::detour::remove();
  }

  // Unlinks like uninstall() but never waits. The hook's memory is freed by
  // a later install, uninstall or spud::collect() once its calls are done.
  [[nodiscard]] detour_error request_uninstall() {
    return this->detail::detour::request_remove();
  }

  // Leaves the hook installed for good and lets go of it. The handle owns
  // nothing from then on: try_install() and uninstall() return kNotFound.
  Self &detach() {
    this->detail::detour::detach();
    return *this;
  }

private:
  template <typename Fn>
  static constexpr bool stateless =
      std::is_empty_v<Fn> && std::is_default_constructible_v<Fn>;

  // The trampoline address is read inside call() so it never sits in the
  // wrapper's own frame: removal scans stacks conservatively, and a stale
  // copy there after the entry count drops reads as a pending return.
  static trampoline_t *next_of(detail::detour_context *ctx) {
    return reinterpret_cast<trampoline_t *>(ctx->trampoline);
  }

  struct call_pointer {
    static R call(detail::detour_context *ctx, Args &&...args) {
      return reinterpret_cast<func_t *>(ctx->func)(next_of(ctx),
                                                   std::forward<Args>(args)...);
    }
  };

  template <typename Fn> struct call_object {
    static R call(detail::detour_context *ctx, Args &&...args) {
      if constexpr (stateless<Fn>) {
        return Fn{}(next_of(ctx), std::forward<Args>(args)...);
      } else {
        return (*static_cast<Fn *>(ctx->callable))(next_of(ctx),
                                                   std::forward<Args>(args)...);
      }
    }
  };

  template <typename Call, bool Counted>
  SPUD_WRAPPER_ATTRS static R wrapper(Args... args) {
    const detail::stub_registers stub = detail::read_stub_registers();
#if SPUD_DETOUR_TRACING
    std::printf("%s\n", stub.ctx->location.function_name());
#endif
    if constexpr (!Counted) {
      // A permanent hook's stub did not count this entry.
      return Call::call(stub.ctx, std::forward<Args>(args)...);
    }
#if SPUD_WRAPPER_SEH
    else if constexpr ((std::is_void_v<R> ||
                        std::is_trivially_destructible_v<R>) &&
                       (std::is_trivially_destructible_v<Args> && ...)) {
      __try {
        return Call::call(stub.ctx, std::forward<Args>(args)...);
      } __finally {
        stub.entry->fetch_sub(1, std::memory_order_release);
      }
    }
#endif
    else {
      const detail::active_guard active_scope{stub.entry};
      return Call::call(stub.ctx, std::forward<Args>(args)...);
    }
  }

  detour(uintptr_t address, uintptr_t func, uintptr_t wrapper,
         uintptr_t uncounted, detail::trace_location location)
      : detail::detour(address, func, wrapper, uncounted, location) {}
};

template <typename R, typename... Args>
struct detour<R (*)(R (*)(Args...), Args...)>
    : public detour<R(R (*)(Args...), Args...)> {
  using detour<R(R (*)(Args...), Args...)>::detour;
};

// The handle type for hooks on a target of signature `Sig`:
// `spud::hook<int(int)> h{&target, [](auto original, int n) { ... }};`
template <typename Sig> struct hook_handle;
template <typename R, typename... Args> struct hook_handle<R(Args...)> {
  using type = detour<R(R (*)(Args...), Args...)>;
};
template <typename Sig> using hook = typename hook_handle<Sig>::type;

// Hooks a function pointer target. The signature comes from the target, and
// the hook is checked against it.
template <typename R, typename... Args, typename F>
  requires std::is_class_v<std::remove_cvref_t<F>> &&
           std::is_invocable_r_v<R, std::remove_cvref_t<F> &, R (*)(Args...),
                                 Args...>
[[nodiscard]] auto create_detour(
    R (*target)(Args...), F &&hook,
    detail::trace_location location = detail::trace_location::current()) {
  return detour<R(R (*)(Args...), Args...)>(target, std::forward<F>(hook),
                                            location);
}

template <typename R, typename... Args>
[[nodiscard]] auto create_detour(
    R (*target)(Args...),
    std::type_identity_t<R (*)(R (*)(Args...), Args...)> hook,
    detail::trace_location location = detail::trace_location::current()) {
  return detour<R(R (*)(Args...), Args...)>(target, hook, location);
}

namespace detail {

// A member function as the free function the ABI calls: `this` first. A
// const member takes `const C *`.
template <typename M> struct member_function;
#define SPUD_MEMBER_FUNCTION(qualifiers, self)                                 \
  template <typename R, typename C, typename... Args>                          \
  struct member_function<R (C::*)(Args...) qualifiers> {                       \
    using pointer = R (*)(self, Args...);                                      \
    using result = R;                                                          \
  };
SPUD_MEMBER_FUNCTION(, C *)
SPUD_MEMBER_FUNCTION(const, const C *)
SPUD_MEMBER_FUNCTION(noexcept, C *)
SPUD_MEMBER_FUNCTION(const noexcept, const C *)
#undef SPUD_MEMBER_FUNCTION

// The code address of a non-virtual member function that needs no `this`
// adjustment, or null. Itanium member pointers are {address or vtable
// offset, adjustment}; the virtual flag is the low bit of the first word on
// x86-64 and of the adjustment on ARM64. MSVC uses a plain code address for
// single inheritance, and a vcall thunk for a virtual function, which this
// cannot tell apart; hook virtual functions through spud/pointer.h.
template <typename M> uintptr_t member_address(M method) {
  uintptr_t words[sizeof(M) / sizeof(uintptr_t)] = {};
  std::memcpy(words, &method, sizeof(words));
#if defined(_MSC_VER)
  return sizeof(M) == sizeof(uintptr_t) ? words[0] : 0;
#elif defined(SPUD_ARCH_ARM64)
  return words[1] == 0 ? words[0] : 0;
#else
  return (words[0] & 1) == 0 && words[1] == 0 ? words[0] : 0;
#endif
}

} // namespace detail

// Hooks a non-virtual member function. The hook takes the original, then
// `this`, then the arguments: `[](auto original, Widget *self, int x)`. A
// virtual function, or one reached through a `this` adjustment, gives an
// empty handle whose install returns kNotFound.
template <typename M, typename F>
  requires std::is_member_function_pointer_v<M>
[[nodiscard]] auto create_detour(
    M method, F &&hook,
    detail::trace_location location = detail::trace_location::current()) {
  using pointer = typename detail::member_function<M>::pointer;
#if defined(_MSC_VER)
  // MSVC returns class types from member functions through a hidden pointer
  // that free functions do not use, so the hook could not match the ABI.
  static_assert(
      !std::is_class_v<typename detail::member_function<M>::result>,
      "MSVC member functions returning a class type cannot be detoured");
#endif
  return create_detour(
      reinterpret_cast<pointer>(detail::member_address(method)),
      std::forward<F>(hook), location);
}

namespace detail {
// FuncType is the hook signature R (*)(R (*)(Args...), Args...) for a hook
// whose first parameter is the original, given as a function pointer, a
// member function pointer, or a lambda (whose operator() is instantiated with
// a placeholder original when it is generic).
template <typename> struct function_traits;

template <typename Function>
struct function_traits
    : public function_traits<
          decltype(&Function::template operator()<void (*)(...)>)> {};

// A lambda or functor with a single, non-template operator().
template <typename Function>
  requires requires { &Function::operator(); }
struct function_traits<Function>
    : function_traits<decltype(&Function::operator())> {};

template <typename ClassType, typename ReturnType, typename FirstArg,
          typename... Arguments>
struct function_traits<ReturnType (ClassType::*)(FirstArg, Arguments...) const>
    : function_traits<ReturnType (*)(ReturnType (*)(Arguments...),
                                     Arguments...)> {};

template <typename ClassType, typename ReturnType, typename FirstArg,
          typename... Arguments>
struct function_traits<ReturnType (ClassType::*)(FirstArg, Arguments...)>
    : function_traits<ReturnType (*)(ReturnType (*)(Arguments...),
                                     Arguments...)> {};

template <typename ReturnType, typename FirstArg, typename... Arguments>
struct function_traits<ReturnType (*)(FirstArg, Arguments...)> {
  using FuncType = ReturnType (*)(ReturnType (*)(Arguments...), Arguments...);
};

} // namespace detail

// Hooks a raw address. The signature comes from the hook, which takes the
// original as its first parameter.
template <typename F,
          typename std::enable_if_t<!std::is_function_v<F>> * = nullptr>
[[nodiscard]] auto create_detour(
    auto target, F func,
    detail::trace_location location = detail::trace_location::current())
  requires(!std::is_function_v<std::remove_pointer_t<decltype(target)>> &&
           !std::is_member_function_pointer_v<decltype(target)>)
{
  using FuncT = typename detail::function_traits<F>::FuncType;
  return detour<FuncT>::create(target, static_cast<FuncT>(func), location);
}

template <typename F, typename std::enable_if_t<std::is_function_v<
                          std::remove_pointer_t<F>>> * = nullptr>
[[nodiscard]] auto create_detour(
    auto target, typename detail::function_traits<F>::FuncType func,
    detail::trace_location location = detail::trace_location::current()) {
  using FuncT = typename detail::function_traits<F>::FuncType;
  return detour<FuncT>::create(target, func, location);
}

template <typename H>
concept installable_hook = requires(H &h) {
  { h.try_install() } -> std::same_as<detour_error>;
  { h.uninstall() } -> std::same_as<detour_error>;
  { h.is_installed() } -> std::same_as<bool>;
};

namespace detail {
struct hook_step {
  void *hook;
  detour_error (*try_install)(void *);
  detour_error (*uninstall)(void *);
  bool (*is_installed)(void *);
  bool install;
  // Set for spud detours, which a rollback relinks in place. Other hooks are
  // undone through try_install and uninstall.
  detour *handle = nullptr;
};

template <installable_hook H> hook_step make_hook_step(H &h, bool install) {
  hook_step step{&h, [](void *p) { return static_cast<H *>(p)->try_install(); },
                 [](void *p) { return static_cast<H *>(p)->uninstall(); },
                 [](void *p) { return static_cast<H *>(p)->is_installed(); },
                 install};
  if constexpr (std::derived_from<H, detour>) {
    step.handle = &h;
  }
  return step;
}

// Uninstalls run newest first. Spelled out to keep <algorithm> out of this
// header.
inline void reverse_steps(std::span<hook_step> steps) {
  for (size_t i = 0, j = steps.size(); i + 1 < j; ++i, --j)
    std::swap(steps[i], steps[j - 1]);
}

// What iterating an lvalue `R` yields, for install_all and uninstall_all
// over a container.
template <typename R>
using range_element =
    std::remove_cvref_t<decltype(*std::begin(std::declval<R &>()))>;
} // namespace detail

// Stages installs and uninstalls, then commit() applies them in order under
// one suspend window. If a step fails, the steps already applied are undone
// newest first and commit() returns the failing step's error, or
// kRollbackFailed when an undo fails too. An uninstalled detour goes back to
// its old place in its chain. Staged handles must outlive commit(). A
// transaction dropped without commit() applies nothing.
class transaction {
public:
  transaction() = default;
  transaction(const transaction &) = delete;
  transaction &operator=(const transaction &) = delete;
  transaction(transaction &&) noexcept = default;
  transaction &operator=(transaction &&) noexcept = default;

  template <installable_hook H> transaction &install(H &hook) {
    steps_.push_back(detail::make_hook_step(hook, true));
    return *this;
  }

  template <installable_hook H> transaction &uninstall(H &hook) {
    steps_.push_back(detail::make_hook_step(hook, false));
    return *this;
  }

  // Clears the staged steps whatever the result.
  [[nodiscard]] detour_error commit() {
    const detour_error err = detail::commit(steps_);
    steps_.clear();
    return err;
  }

private:
  std::vector<detail::hook_step> steps_;
};

// A transaction of installs in argument order. Handles that were already
// installed stay installed if a later one fails.
template <installable_hook... Hs>
[[nodiscard]] detour_error install_all(Hs &...hs) {
  std::array<detail::hook_step, sizeof...(Hs)> steps{
      detail::make_hook_step(hs, true)...};
  return detail::commit(steps);
}

// A transaction of uninstalls, last to first so stacked hooks on one target
// come off the head. On failure the handles already removed are put back.
template <installable_hook... Hs>
[[nodiscard]] detour_error uninstall_all(Hs &...hs) {
  std::array<detail::hook_step, sizeof...(Hs)> steps{
      detail::make_hook_step(hs, false)...};
  detail::reverse_steps(steps);
  return detail::commit(steps);
}

template <typename R>
  requires installable_hook<detail::range_element<R>>
[[nodiscard]] detour_error install_all(R &hooks) {
  std::vector<detail::hook_step> steps;
  for (auto &h : hooks) {
    steps.push_back(detail::make_hook_step(h, true));
  }
  return detail::commit(steps);
}

template <typename R>
  requires installable_hook<detail::range_element<R>>
[[nodiscard]] detour_error uninstall_all(R &hooks) {
  std::vector<detail::hook_step> steps;
  for (auto &h : hooks) {
    steps.push_back(detail::make_hook_step(h, false));
  }
  detail::reverse_steps(steps);
  return detail::commit(steps);
}

} // namespace spud

// `func` is a hook template taking the original as its first parameter. The
// hook records the line that expands this macro when tracing is on.
#define SPUD_AUTO_HOOK(target, func)                                           \
  spud::create_detour<decltype(&func<void (*)(...)>)>(target, func)

// Installs a permanent hook (see try_install_permanent) the first time it
// runs and returns its trampoline, or null when that install failed. Nothing
// unpatches the target during static destruction.
#define SPUD_STATIC_DETOUR(addr, fn)                                           \
  (([=]() {                                                                    \
    static const auto spud_static_trampoline = [&] {                           \
      auto spud_static_hook = SPUD_AUTO_HOOK(addr, fn);                        \
      return spud_static_hook.try_install_permanent() ==                       \
                     spud::detour_error::kOk                                   \
                 ? spud_static_hook.trampoline()                               \
                 : nullptr;                                                    \
    }();                                                                       \
    return spud_static_trampoline;                                             \
  })())

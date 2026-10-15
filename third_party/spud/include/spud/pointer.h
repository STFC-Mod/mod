#pragma once

#include <spud/arch.h>
#include <spud/diagnostics.h>
#include <spud/error.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace spud {

// A GOT, IAT or symbol pointer slot found by find_import_slots. A lazily
// bound ELF slot still holds its PLT resolver stub until the first call.
// Calling through the stub would resolve the symbol and rebind the slot over
// the hook, so the finder resolves it up front: `lazy_stub` is that stub and
// `bound` the function it leads to. Both are null for a bound slot. Hook
// these with hook_pointer(slots, ...), which records `bound` as the
// original, rather than through the bare `slot`.
struct import_slot {
  void **slot = nullptr;
  void *lazy_stub = nullptr;
  void *bound = nullptr;
};

namespace detail {

// The untyped body of pointer_hook, kept out of line so each function type
// only instantiates a thin wrapper. `original_out` is the caller's function
// pointer variable, or null.
class slot_patch {
public:
  slot_patch(std::vector<void **> slots, void *replacement, void *original_out);
  slot_patch(std::span<const import_slot> imports, void *replacement,
             void *original_out);
  slot_patch(slot_patch &&other) noexcept;
  slot_patch &operator=(slot_patch &&other) noexcept;
  ~slot_patch();

  detour_error try_install();
  detour_error uninstall();
  void detach() {
    installed_ = false;
  }

  bool is_installed() const {
    return installed_;
  }
  detour_error last_error() const {
    return last_error_;
  }
  int last_os_error() const {
    return last_os_error_;
  }
  const std::vector<void *> &originals() const {
    return previous_;
  }

private:
  detour_error finish(detour_error error, int os_error = 0);
  void take(slot_patch &other);

  std::vector<void **> slots_;
  // Parallel to slots_ and only as long as the last lazy import slot:
  // {resolver stub, bound function}. Install records the bound function as
  // the original while the slot still holds the stub.
  std::vector<std::pair<void *, void *>> lazy_;
  void *replacement_ = nullptr;
  std::vector<void *> previous_;
  void *original_out_ = nullptr;
  bool installed_ = false;
  detour_error last_error_ = detour_error::kOk;
  int last_os_error_ = 0;
};

// The untyped body of local_vtable_hook.
class local_vtable_patch {
public:
  local_vtable_patch(void *object, size_t index, void *replacement,
                     void *original_out);
  local_vtable_patch(local_vtable_patch &&other) noexcept;
  local_vtable_patch &operator=(local_vtable_patch &&other) noexcept;
  ~local_vtable_patch();

  bool set_vtable_length(size_t length) {
    if (installed_) {
      return false;
    }
    length_ = length;
    return true;
  }
  detour_error try_install();
  detour_error uninstall();
  void detach();

  bool is_installed() const {
    return installed_;
  }
  detour_error last_error() const {
    return last_error_;
  }
  int last_os_error() const {
    return last_os_error_;
  }
  void *original() const {
    const auto &originals = slot_.originals();
    return originals.empty() ? nullptr : originals.front();
  }

private:
  detour_error finish(detour_error error, int os_error = 0);
  void release();
  void abandon_copy();
  void take(local_vtable_patch &other);

  void *object_ = nullptr;
  size_t index_ = 0;
  // Entries to copy from the address point; 0 measures with vtable_length.
  size_t length_ = 0;
  void *replacement_ = nullptr;
  void *original_out_ = nullptr;
  void **vptr_slot_ = nullptr;
  // Address point of copy_, where the object's vptr points while installed.
  void **copied_ = nullptr;
  void **original_vtable_ = nullptr;
  std::vector<void *> copy_;
  slot_patch slot_{std::vector<void **>{}, nullptr, nullptr};
  bool installed_ = false;
  detour_error last_error_ = detour_error::kOk;
  int last_os_error_ = 0;
};

template <typename Fn> void *erase_fn(Fn fn) {
  static_assert(sizeof(Fn) == sizeof(void *));
  return reinterpret_cast<void *>(fn);
}

// Member pointers to virtual functions encode a vtable byte offset; this
// reads the entry index and the this-adjustment out of one.
bool vtable_index_from_member(const void *member, size_t size, size_t *out,
                              ptrdiff_t *adj_out);
// The MS ABI half of that, built on every host so it can be tested anywhere:
// follows the code address to the x64 vcall thunk the compiler emitted.
bool decode_msvc_member(const void *member, size_t size, size_t *index,
                        ptrdiff_t *adj);

// &vtable[index] of the object's table, or null when the object has no
// table or `index` is not below vtable_length.
void **checked_vtable_slot(void *object, size_t index);

// A virtual member function as the free function its vtable slot holds:
// `this` becomes the first parameter. noexcept members take the same entry.
template <typename M> struct vtable_entry_of;
template <typename C, typename R, typename... A>
struct vtable_entry_of<R (C::*)(A...)> {
  using self = C *;
  using fn = R (*)(C *, A...);
};
template <typename C, typename R, typename... A>
struct vtable_entry_of<R (C::*)(A...) const> {
  using self = const C *;
  using fn = R (*)(const C *, A...);
};
template <typename C, typename R, typename... A>
struct vtable_entry_of<R (C::*)(A...) noexcept>
    : vtable_entry_of<R (C::*)(A...)> {};
template <typename C, typename R, typename... A>
struct vtable_entry_of<R (C::*)(A...) const noexcept>
    : vtable_entry_of<R (C::*)(A...) const> {};

template <typename M> using vtable_self_t = typename vtable_entry_of<M>::self;
template <typename M> using vtable_fn_t = typename vtable_entry_of<M>::fn;

// `object` may be any class derived from the method's own; converting it
// finds the right base subobject. `replacement` may be anything that
// converts to the slot's function pointer type, captureless lambdas
// included.
template <typename T, typename M, typename F>
concept vtable_hook_args = std::is_member_function_pointer_v<M> &&
                           std::is_convertible_v<T *, vtable_self_t<M>> &&
                           std::is_convertible_v<F, vtable_fn_t<M>>;

// A null object in the result means `method` is not a virtual function.
template <typename M>
std::pair<void *, size_t> resolve_virtual(vtable_self_t<M> object, M method) {
  size_t index = 0;
  ptrdiff_t adj = 0;
  if (object == nullptr ||
      !vtable_index_from_member(&method, sizeof(method), &index, &adj)) {
    return {nullptr, 0};
  }
  auto *self = const_cast<char *>(reinterpret_cast<const char *>(object));
  return {self + adj, index};
}

} // namespace detail

// Replaces function pointers in place. No trampoline, no prologue rewrite.
// Call original() or the optional out-pointer after a successful install.
template <typename Fn> class pointer_hook {
public:
  pointer_hook(Fn *slot, Fn replacement, Fn *original_out = nullptr)
      : patch_(slot != nullptr
                   ? std::vector<void **>{reinterpret_cast<void **>(slot)}
                   : std::vector<void **>{},
               detail::erase_fn(replacement), original_out) {}

  pointer_hook(const std::vector<Fn *> &slots, Fn replacement,
               Fn *original_out = nullptr)
      : patch_(erase_slots(slots), detail::erase_fn(replacement),
               original_out) {}

  pointer_hook(std::span<const import_slot> imports, Fn replacement,
               Fn *original_out = nullptr)
      : patch_(imports, detail::erase_fn(replacement), original_out) {}

  // Patches every slot, or none when a page cannot be made writable.
  // A slot that already holds the replacement is refused with
  // kTargetChanged, since the hook would become its own original.
  [[nodiscard]] detour_error try_install() {
    return patch_.try_install();
  }

  // Puts back only slots that still hold the replacement. One hooked again
  // since is left alone and reported as kTargetChanged.
  [[nodiscard]] detour_error uninstall() {
    return patch_.uninstall();
  }

  // Leaves the slots patched for good and drops ownership, so destroying the
  // handle no longer restores them. Installing again is refused with
  // kTargetChanged while a slot still holds the replacement.
  pointer_hook &detach() {
    patch_.detach();
    return *this;
  }

  bool is_installed() const {
    return patch_.is_installed();
  }
  detour_error last_error() const {
    return patch_.last_error();
  }
  // errno / GetLastError behind last_error(), or 0. See spud::last_os_error().
  int last_os_error() const {
    return patch_.last_os_error();
  }
  Fn original() const {
    const auto &originals = patch_.originals();
    return originals.empty() ? Fn{} : reinterpret_cast<Fn>(originals.front());
  }
  // The value each slot held before install, in slot order.
  std::vector<Fn> originals() const {
    std::vector<Fn> out;
    out.reserve(patch_.originals().size());
    for (void *original : patch_.originals()) {
      out.push_back(reinterpret_cast<Fn>(original));
    }
    return out;
  }

private:
  static std::vector<void **> erase_slots(const std::vector<Fn *> &slots) {
    std::vector<void **> out;
    out.reserve(slots.size());
    for (Fn *slot : slots) {
      out.push_back(reinterpret_cast<void **>(slot));
    }
    return out;
  }

  detail::slot_patch patch_;
};

// The slot decides the type, so a captureless lambda converts to it.
template <typename Fn>
[[nodiscard]] pointer_hook<Fn>
hook_pointer(Fn *slot, std::type_identity_t<Fn> replacement,
             std::type_identity_t<Fn> *original_out = nullptr) {
  return pointer_hook<Fn>(slot, replacement, original_out);
}

// Patches every slot find_import_slots returned. The replacement decides
// the type.
template <typename Fn>
[[nodiscard]] pointer_hook<Fn> hook_pointer(std::span<const import_slot> slots,
                                            Fn replacement,
                                            Fn *original_out = nullptr) {
  return pointer_hook<Fn>(slots, replacement, original_out);
}

// `object` starts with a vptr. `index` is an entry in that table as it sits
// in memory, not a compiler "nth virtual". There is no length on the table,
// so a bad index writes off the end. Prefer the member-pointer overload.
template <typename T, typename Fn>
[[nodiscard]] pointer_hook<Fn> hook_vtable(T *object, size_t index,
                                           Fn replacement,
                                           Fn *original_out = nullptr) {
  void **vtable =
      object != nullptr ? *reinterpret_cast<void ***>(object) : nullptr;
  if (vtable == nullptr) {
    return pointer_hook<Fn>(static_cast<Fn *>(nullptr), replacement,
                            original_out);
  }
  return pointer_hook<Fn>(reinterpret_cast<Fn *>(&vtable[index]), replacement,
                          original_out);
}

template <typename M>
  requires std::is_member_function_pointer_v<M>
[[nodiscard]] std::optional<size_t> vtable_index(M method) {
  size_t index = 0;
  if (!detail::vtable_index_from_member(&method, sizeof(method), &index,
                                        nullptr)) {
    return std::nullopt;
  }
  return index;
}

template <typename T, typename M, typename F>
  requires detail::vtable_hook_args<T, M, F>
[[nodiscard]] pointer_hook<detail::vtable_fn_t<M>>
hook_vtable(T *object, M method, F replacement,
            detail::vtable_fn_t<M> *original_out = nullptr) {
  using Fn = detail::vtable_fn_t<M>;
  const auto [target, index] = detail::resolve_virtual<M>(object, method);
  return pointer_hook<Fn>(
      reinterpret_cast<Fn *>(detail::checked_vtable_slot(target, index)),
      static_cast<Fn>(replacement), original_out);
}

// Counts the leading entries of `vtable` that point into executable memory,
// stopping at the first that does not or at an unreadable slot. A table
// that still has not ended after 8192 entries is reported as 0, since what
// is being walked is likely not a vtable.
[[nodiscard]] size_t vtable_length(void **vtable);

// Copies the vtable, points this object at the copy, and patches the copy.
// Other instances of the class keep the shared table.
template <typename Fn> class local_vtable_hook {
public:
  local_vtable_hook(void *object, size_t index, Fn replacement,
                    Fn *original_out = nullptr)
      : patch_(object, index, detail::erase_fn(replacement), original_out) {}

  // How many entries from the address point to copy, for a table whose end
  // vtable_length cannot find. Returns false once installed; 0 goes back to
  // measuring. The entries must be readable.
  bool set_vtable_length(size_t length) {
    return patch_.set_vtable_length(length);
  }

  [[nodiscard]] detour_error try_install() {
    return patch_.try_install();
  }
  [[nodiscard]] detour_error uninstall() {
    return patch_.uninstall();
  }

  // Forgets the object without touching it, for an object that is about to
  // be or already was destroyed: uninstalling then would write to freed
  // memory. The copied table is leaked, since the object may still use it.
  local_vtable_hook &detach() {
    patch_.detach();
    return *this;
  }

  bool is_installed() const {
    return patch_.is_installed();
  }
  detour_error last_error() const {
    return patch_.last_error();
  }
  // errno / GetLastError behind last_error(), or 0. See spud::last_os_error().
  int last_os_error() const {
    return patch_.last_os_error();
  }
  Fn original() const {
    return reinterpret_cast<Fn>(patch_.original());
  }

private:
  detail::local_vtable_patch patch_;
};

template <typename T, typename Fn>
[[nodiscard]] local_vtable_hook<Fn>
hook_vtable_local(T *object, size_t index, Fn replacement,
                  Fn *original_out = nullptr) {
  return local_vtable_hook<Fn>(object, index, replacement, original_out);
}

template <typename T, typename M, typename F>
  requires detail::vtable_hook_args<T, M, F>
[[nodiscard]] local_vtable_hook<detail::vtable_fn_t<M>>
hook_vtable_local(T *object, M method, F replacement,
                  detail::vtable_fn_t<M> *original_out = nullptr) {
  using Fn = detail::vtable_fn_t<M>;
  const auto [target, index] = detail::resolve_virtual<M>(object, method);
  return hook_vtable_local(target, index, static_cast<Fn>(replacement),
                           original_out);
}

// Import slots for `name` in the modules whose path `module` names (see
// guides/pointer), sorted by slot address. An empty module searches every
// loaded module, since an import hook usually has to catch every caller;
// find_in_module reads an empty module as the main executable instead.
[[nodiscard]] std::vector<import_slot> find_import_slots(std::string_view name);
[[nodiscard]] std::vector<import_slot>
find_import_slots(std::string_view module, std::string_view name);
// Slots in every module that currently hold `function`, plus on ELF and
// Mach-O those of the symbol it is defined as.
[[nodiscard]] std::vector<import_slot>
find_import_slots_for(const void *function);

template <typename Fn>
  requires std::is_pointer_v<Fn> &&
           std::is_function_v<std::remove_pointer_t<Fn>>
[[nodiscard]] std::vector<import_slot> find_import_slots(Fn function) {
  return find_import_slots_for(reinterpret_cast<const void *>(function));
}

// By name the replacement decides the type; name it to pass a lambda:
// hook_import<int (*)(int)>("abs", [](int n) { ... }).
template <typename Fn>
[[nodiscard]] pointer_hook<Fn>
hook_import(std::string_view module, std::string_view name, Fn replacement,
            Fn *original_out = nullptr) {
  return pointer_hook<Fn>(find_import_slots(module, name), replacement,
                          original_out);
}

template <typename Fn>
[[nodiscard]] pointer_hook<Fn>
hook_import(std::string_view name, Fn replacement, Fn *original_out = nullptr) {
  return hook_import(std::string_view{}, name, replacement, original_out);
}

template <typename Imported, typename Fn>
  requires std::is_pointer_v<Imported> &&
           std::is_function_v<std::remove_pointer_t<Imported>> &&
           std::is_pointer_v<Fn> &&
           std::is_function_v<std::remove_pointer_t<Fn>>
[[nodiscard]] pointer_hook<Fn> hook_import(Imported imported, Fn replacement,
                                           Fn *original_out = nullptr) {
  return pointer_hook<Fn>(
      find_import_slots_for(reinterpret_cast<const void *>(imported)),
      replacement, original_out);
}

// The imported function decides the type, so a captureless lambda works.
template <typename Imported, typename F>
  requires std::is_pointer_v<Imported> &&
           std::is_function_v<std::remove_pointer_t<Imported>> &&
           std::is_class_v<F> && std::is_convertible_v<F, Imported>
[[nodiscard]] pointer_hook<Imported>
hook_import(Imported imported, F replacement,
            Imported *original_out = nullptr) {
  return hook_import(imported, static_cast<Imported>(replacement),
                     original_out);
}

} // namespace spud

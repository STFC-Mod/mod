// Pointer, vtable and local vtable patches behind spud/pointer.h, and the
// member pointer and vtable layout reading they need.

#include <spud/memory.h>
#include <spud/memory/protection.h>
#include <spud/pointer.h>

#include "memory/readable.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>

namespace spud {
namespace {

void store_slot(void **slot, void *value) {
  *static_cast<void *volatile *>(slot) = value;
}

// Stores values[i] into each slots[i] whose page could be made writable and
// returns false if any could not. Slots are visited in address order with
// one protection change per mapping. A mapping that is already writable is
// stored to directly. Otherwise it keeps its bits and gains WRITE, so an IAT
// merged into .text stays executable for threads running there. Apple turns
// writable-and-executable ordinary code into a copy-on-write read-write
// mapping instead (see protection.h), so there such a page is briefly not
// executable. `written`, when given, receives the indices stored.
bool store_slots(std::span<void **const> slots, std::span<void *const> values,
                 std::vector<size_t> *written = nullptr) {
  std::vector<size_t> order(slots.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return reinterpret_cast<uintptr_t>(slots[a]) <
           reinterpret_cast<uintptr_t>(slots[b]);
  });
  bool all = true;
  for (size_t i = 0; i < order.size();) {
    const auto first = reinterpret_cast<uintptr_t>(slots[order[i]]);
    const auto region = query_memory(first);
    if (!region) {
      all = false;
      ++i;
      continue;
    }
    const uintptr_t region_end = region->start + region->size;
    size_t end = i + 1;
    while (end < order.size() &&
           reinterpret_cast<uintptr_t>(slots[order[end]]) + sizeof(void *) <=
               region_end) {
      ++end;
    }
    const uintptr_t last =
        reinterpret_cast<uintptr_t>(slots[order[end - 1]]) + sizeof(void *);
    std::optional<protection_scope> scope;
    if (!has_protection(region->protection, mem_protection::kWrite)) {
      const auto writable = region->protection | mem_protection::kReadWrite;
      scope.emplace(first, last - first, writable);
      if (!scope->ok()) {
        all = false;
        i = end;
        continue;
      }
    }
    for (; i < end; ++i) {
      store_slot(slots[order[i]], values[order[i]]);
      if (written != nullptr) {
        written->push_back(order[i]);
      }
    }
  }
  return all;
}

bool write_pointer(void **slot, void *value) {
  return store_slots({&slot, 1}, {&value, 1});
}

void set_original_out(void *original_out, void *value) {
  if (original_out != nullptr) {
    std::memcpy(original_out, &value, sizeof(value));
  }
}

} // namespace

namespace detail {

slot_patch::slot_patch(std::vector<void **> slots, void *replacement,
                       void *original_out)
    : slots_(std::move(slots)), replacement_(replacement),
      original_out_(original_out) {}

slot_patch::slot_patch(std::span<const import_slot> imports, void *replacement,
                       void *original_out)
    : replacement_(replacement), original_out_(original_out) {
  for (const import_slot &import : imports) {
    slots_.push_back(import.slot);
    if (import.lazy_stub != nullptr) {
      lazy_.resize(slots_.size());
      lazy_.back() = {import.lazy_stub, import.bound};
    }
  }
}

slot_patch::slot_patch(slot_patch &&other) noexcept {
  take(other);
}

slot_patch &slot_patch::operator=(slot_patch &&other) noexcept {
  if (this != &other) {
    (void)uninstall();
    take(other);
  }
  return *this;
}

slot_patch::~slot_patch() {
  (void)uninstall();
}

detour_error slot_patch::try_install() {
  clear_os_error();
  if (installed_) {
    return finish(detour_error::kOk);
  }
  if (slots_.empty()) {
    return finish(detour_error::kNotFound);
  }
#if SPUD_ARCH_ARM64E
  return finish(detour_error::kControlFlowEnforced);
#endif

  // A slot that already holds the replacement would record the hook as its
  // own original and recurse forever. That is the state after detach(), or
  // when another handle installed the same replacement there.
  std::vector<void *> previous;
  previous.reserve(slots_.size());
  for (size_t i = 0; i < slots_.size(); ++i) {
    void *value = *static_cast<void *volatile *>(slots_[i]);
    if (value == replacement_) {
      return finish(detour_error::kTargetChanged);
    }
    if (i < lazy_.size() && lazy_[i].first != nullptr &&
        value == lazy_[i].first) {
      value = lazy_[i].second;
    }
    previous.push_back(value);
  }
  previous_ = std::move(previous);
  // Another thread can enter the replacement as soon as the first slot
  // changes, so the out-pointer has to be valid before then. A failed
  // install leaves it naming the real original, which is still safe to
  // call for anyone who raced into the replacement before the rollback.
  set_original_out(original_out_, previous_.front());

  const std::vector<void *> replacements(slots_.size(), replacement_);
  std::vector<size_t> stored;
  stored.reserve(slots_.size());
  if (!store_slots(slots_, replacements, &stored)) {
    const int os_error = spud::last_os_error();
    std::vector<void **> undo;
    std::vector<void *> undo_values;
    for (size_t i : stored) {
      undo.push_back(slots_[i]);
      undo_values.push_back(previous_[i]);
    }
    // These pages were writable a moment ago, so the undo does not fail
    // short of another thread changing their protection.
    (void)store_slots(undo, undo_values);
    return finish(detour_error::kProtectionFailed, os_error);
  }

  installed_ = true;
  return finish(detour_error::kOk);
}

detour_error slot_patch::uninstall() {
  clear_os_error();
  if (!installed_) {
    return finish(detour_error::kOk);
  }
  // A slot that no longer holds the replacement was hooked again after
  // us, and that hook likely calls ours as its original. Writing our
  // original back would silently drop it, so the slot is left alone. A
  // slot that already holds our original was restored by an earlier,
  // partly failed uninstall. The check and the store are not atomic.
  std::vector<void **> ours;
  std::vector<void *> restore;
  bool foreign = false;
  for (size_t i = 0; i < slots_.size(); ++i) {
    void *current = *static_cast<void *volatile *>(slots_[i]);
    if (current == replacement_) {
      ours.push_back(slots_[i]);
      restore.push_back(previous_[i]);
    } else if (current != previous_[i]) {
      foreign = true;
    }
  }
  if (!store_slots(ours, restore)) {
    return finish(detour_error::kProtectionFailed, spud::last_os_error());
  }
  installed_ = false;
  return finish(foreign ? detour_error::kTargetChanged : detour_error::kOk);
}

detour_error slot_patch::finish(detour_error error, int os_error) {
  last_error_ = error;
  last_os_error_ = os_error;
  return error;
}

void slot_patch::take(slot_patch &other) {
  slots_ = std::exchange(other.slots_, {});
  lazy_ = std::exchange(other.lazy_, {});
  replacement_ = other.replacement_;
  previous_ = std::exchange(other.previous_, {});
  original_out_ = other.original_out_;
  installed_ = std::exchange(other.installed_, false);
  last_error_ = other.last_error_;
  last_os_error_ = other.last_os_error_;
}

local_vtable_patch::local_vtable_patch(void *object, size_t index,
                                       void *replacement, void *original_out)
    : object_(object), index_(index), replacement_(replacement),
      original_out_(original_out) {}

local_vtable_patch::local_vtable_patch(local_vtable_patch &&other) noexcept {
  take(other);
}

local_vtable_patch &
local_vtable_patch::operator=(local_vtable_patch &&other) noexcept {
  if (this != &other) {
    release();
    take(other);
  }
  return *this;
}

local_vtable_patch::~local_vtable_patch() {
  release();
}

detour_error local_vtable_patch::try_install() {
  clear_os_error();
  if (installed_) {
    return finish(detour_error::kOk);
  }
  if (object_ == nullptr) {
    return finish(detour_error::kNotFound);
  }
  vptr_slot_ = static_cast<void **>(object_);
  original_vtable_ = static_cast<void **>(*vptr_slot_);
  if (original_vtable_ == nullptr) {
    return finish(detour_error::kNotFound);
  }
  // The copy has to cover every entry the object's code can call, or those
  // calls read past it. A measured length that is too short is the
  // caller's to fix with set_vtable_length.
  const size_t n = length_ != 0 ? length_ : vtable_length(original_vtable_);
  if (index_ >= n || n > SIZE_MAX / sizeof(void *) ||
      detail::readable_bytes_from(reinterpret_cast<uintptr_t>(original_vtable_),
                                  n * sizeof(void *)) < n * sizeof(void *)) {
    return finish(detour_error::kNotFound);
  }
  // Code reads below the address point too: offset-to-top and RTTI on
  // Itanium, with virtual-base and vcall offsets further down, and the
  // complete object locator at [-1] on MSVC. None of it has a recorded
  // length, so copy a fixed window that stays inside the table's mapping.
  // 32 leaves room for vcall offsets of a virtual base with ~29 virtuals.
  constexpr size_t kMaxPrefix = 32;
  const auto table =
      query_memory(reinterpret_cast<uintptr_t>(original_vtable_));
  if (!table) {
    return finish(detour_error::kNotFound);
  }
  const size_t prefix =
      std::min(kMaxPrefix,
               (reinterpret_cast<uintptr_t>(original_vtable_) - table->start) /
                   sizeof(void *));
  copy_.assign(original_vtable_ - prefix, original_vtable_ + n);
  copied_ = copy_.data() + prefix;
  if (!write_pointer(vptr_slot_, copied_)) {
    copy_.clear();
    return finish(detour_error::kProtectionFailed, spud::last_os_error());
  }
  slot_ = slot_patch(std::vector<void **>{&copied_[index_]}, replacement_,
                     original_out_);
  if (const detour_error err = slot_.try_install(); err != detour_error::kOk) {
    if (write_pointer(vptr_slot_, original_vtable_)) {
      copy_.clear();
    } else {
      abandon_copy();
    }
    return finish(err, slot_.last_os_error());
  }
  installed_ = true;
  return finish(detour_error::kOk);
}

detour_error local_vtable_patch::uninstall() {
  clear_os_error();
  if (!installed_) {
    return finish(detour_error::kOk);
  }
  if (*static_cast<void *volatile *>(vptr_slot_) != copied_) {
    // Something else repointed the object since; leave it alone, and the
    // table with it in case that something chains to our copy.
    abandon_copy();
    return finish(detour_error::kTargetChanged);
  }
  if (!write_pointer(vptr_slot_, original_vtable_)) {
    // The object still points at the copy, so it must stay alive.
    return finish(detour_error::kProtectionFailed, spud::last_os_error());
  }
  // The copy is private to this object; once the vptr is back nothing
  // reads the patched slot, so its result does not matter.
  (void)slot_.uninstall();
  copy_.clear();
  installed_ = false;
  return finish(detour_error::kOk);
}

void local_vtable_patch::detach() {
  if (installed_) {
    abandon_copy();
  }
}

detour_error local_vtable_patch::finish(detour_error error, int os_error) {
  last_error_ = error;
  last_os_error_ = os_error;
  return error;
}

// The handle is going away. If the vptr cannot be put back, the object
// keeps calling through the copy, so it is leaked rather than freed.
void local_vtable_patch::release() {
  (void)uninstall();
  if (installed_) {
    abandon_copy();
  }
}

void local_vtable_patch::abandon_copy() {
  slot_.detach();
  (void)new std::vector<void *>(std::move(copy_));
  copy_ = {};
  installed_ = false;
}

void local_vtable_patch::take(local_vtable_patch &other) {
  object_ = std::exchange(other.object_, nullptr);
  index_ = other.index_;
  length_ = other.length_;
  replacement_ = other.replacement_;
  original_out_ = other.original_out_;
  vptr_slot_ = std::exchange(other.vptr_slot_, nullptr);
  copied_ = std::exchange(other.copied_, nullptr);
  original_vtable_ = other.original_vtable_;
  copy_ = std::exchange(other.copy_, {});
  slot_ = std::move(other.slot_);
  installed_ = std::exchange(other.installed_, false);
  last_error_ = other.last_error_;
  last_os_error_ = other.last_os_error_;
}

namespace {

int32_t read_i32(const uint8_t *p) {
  int32_t value = 0;
  std::memcpy(&value, p, sizeof(value));
  return value;
}

bool starts_with(const uint8_t *code, std::initializer_list<uint8_t> bytes) {
  return std::equal(bytes.begin(), bytes.end(), code);
}

// The x64 vcall thunks this reads, with d a vtable byte offset:
//   MSVC:         mov rax, [rcx]; jmp [rax+d]
//   clang-cl:     mov rax, [rcx]; mov rax, [rax+d]; jmp rax
//   clang-cl -O0: the same, with rcx spilled first (push rax;
//                 mov [rsp], rcx; mov rcx, [rsp]) and pop r10 before the jmp
// An incrementally linked image reaches the thunk through an E9 jump in its
// import thunk table. An add or sub on rcx, or a vptr loaded from
// [rcx+d8], adjusts `this` first. A non-virtual function whose body is a
// tail call through its own vptr looks the same; nothing tells them apart.
bool decode_vcall_thunk(const uint8_t *code, size_t *index, ptrdiff_t *adj) {
  if (code == nullptr) {
    return false;
  }
  for (int hops = 0; hops < 4 && code[0] == 0xE9; ++hops) {
    code += 5 + read_i32(code + 1);
  }
  const bool spilled =
      starts_with(code, {0x50, 0x48, 0x89, 0x0C, 0x24, 0x48, 0x8B, 0x0C, 0x24});
  if (spilled) {
    code += 9;
  }
  ptrdiff_t this_adj = 0;
  if (starts_with(code, {0x48, 0x83, 0xC1})) { // add rcx, imm8
    this_adj += static_cast<int8_t>(code[3]);
    code += 4;
  } else if (starts_with(code, {0x48, 0x81, 0xC1})) { // add rcx, imm32
    this_adj += read_i32(code + 3);
    code += 7;
  } else if (starts_with(code, {0x48, 0x83, 0xE9})) { // sub rcx, imm8
    this_adj -= static_cast<int8_t>(code[3]);
    code += 4;
  }
  if (starts_with(code, {0x48, 0x8B, 0x01})) { // mov rax, [rcx]
    code += 3;
  } else if (starts_with(code, {0x48, 0x8B, 0x41})) { // mov rax, [rcx+d8]
    this_adj += static_cast<int8_t>(code[3]);
    code += 4;
  } else {
    return false;
  }

  int64_t offset = -1;
  if (starts_with(code, {0xFF, 0x20})) { // jmp [rax]
    offset = 0;
  } else if (starts_with(code, {0xFF, 0x60})) { // jmp [rax+d8]
    offset = static_cast<int8_t>(code[2]);
  } else if (starts_with(code, {0xFF, 0xA0})) { // jmp [rax+d32]
    offset = read_i32(code + 2);
  } else {
    if (starts_with(code, {0x48, 0x8B, 0x00})) { // mov rax, [rax]
      offset = 0;
      code += 3;
    } else if (starts_with(code, {0x48, 0x8B, 0x40})) { // mov rax, [rax+d8]
      offset = static_cast<int8_t>(code[3]);
      code += 4;
    } else if (starts_with(code, {0x48, 0x8B, 0x80})) { // mov rax, [rax+d32]
      offset = read_i32(code + 3);
      code += 7;
    } else {
      return false;
    }
    if (spilled) {
      if (!starts_with(code, {0x41, 0x5A})) { // pop r10
        return false;
      }
      code += 2;
    }
    if (!starts_with(code, {0x48, 0xFF, 0xE0}) &&
        !starts_with(code, {0xFF, 0xE0})) { // jmp rax
      return false;
    }
  }
  if (offset < 0 || offset % static_cast<int64_t>(sizeof(void *)) != 0) {
    return false;
  }
  *index = static_cast<size_t>(offset) / sizeof(void *);
  *adj = this_adj;
  return true;
}

} // namespace

// An MS ABI member function pointer is the code address, then for classes
// with multiple inheritance a 32-bit this-adjustment, then for virtual
// inheritance a vbtable index. The 24-byte form for incomplete classes puts
// a vbptr offset between those two. A method reached through a virtual base
// needs the object's vbtable, which is not handled; the 24-byte form refuses
// it, but the 16-byte form cannot be told apart from multiple inheritance
// and is read as if it were.
bool decode_msvc_member(const void *member, size_t size, size_t *index,
                        ptrdiff_t *adj) {
  if (size < sizeof(void *)) {
    return false;
  }
  const uint8_t *code = nullptr;
  std::memcpy(&code, member, sizeof(code));
  if (!decode_vcall_thunk(code, index, adj)) {
    return false;
  }
  const auto *bytes = static_cast<const uint8_t *>(member);
  if (size >= 16) {
    *adj += read_i32(bytes + 8);
  }
  if (size >= 24 && read_i32(bytes + 16) != 0) {
    return false;
  }
  return true;
}

bool vtable_index_from_member(const void *member, size_t size, size_t *out,
                              ptrdiff_t *adj_out) {
  if (member == nullptr || out == nullptr) {
    return false;
  }
  size_t index = 0;
  ptrdiff_t adj = 0;
#if defined(_MSC_VER)
  if (!decode_msvc_member(member, size, &index, &adj)) {
    return false;
  }
#else
  // Itanium member function pointers are two words, {ptr, adj}. For a
  // virtual function the classic form stores the vtable byte offset + 1 in
  // ptr and the this-adjustment in adj. The ARM C++ ABI, used for AArch64 by
  // GCC and Clang alike, cannot spend ptr's low bit on that (Thumb code
  // addresses are odd), so it stores the plain offset in ptr and
  // 2 * this-adjustment + 1 in adj.
  if (size < 2 * sizeof(ptrdiff_t)) {
    return false;
  }
  ptrdiff_t ptr = 0;
  ptrdiff_t word1 = 0;
  std::memcpy(&ptr, member, sizeof(ptr));
  std::memcpy(&word1, static_cast<const char *>(member) + sizeof(ptrdiff_t),
              sizeof(word1));
#if SPUD_ARCH_ARM64
  if ((word1 & 1) == 0) {
    return false;
  }
  index = static_cast<size_t>(ptr) / sizeof(void *);
  adj = word1 >> 1;
#else
  if ((ptr & 1) == 0) {
    return false;
  }
  index = static_cast<size_t>(ptr - 1) / sizeof(void *);
  adj = word1;
#endif
#endif
  *out = index;
  if (adj_out != nullptr) {
    *adj_out = adj;
  }
  return true;
}

void **checked_vtable_slot(void *object, size_t index) {
  if (object == nullptr) {
    return nullptr;
  }
  auto **vtable = *static_cast<void ***>(object);
  if (vtable == nullptr || index >= vtable_length(vtable)) {
    return nullptr;
  }
  return &vtable[index];
}

} // namespace detail

size_t vtable_length(void **vtable) {
  if (vtable == nullptr) {
    return 0;
  }
  const auto covers = [](const std::optional<memory_region> &region,
                         uintptr_t address) {
    return region && address >= region->start &&
           address - region->start < region->size;
  };
  // Engine base classes reach several hundred virtuals. Past this the walk
  // is more likely running over some other array of code pointers.
  constexpr size_t kMax = 8192;
  std::optional<memory_region> table;
  std::optional<memory_region> code;
  size_t n = 0;
  for (; n < kMax; ++n) {
    const auto slot = reinterpret_cast<uintptr_t>(&vtable[n]);
    if (!covers(table, slot)) {
      table = query_memory(slot);
      if (!table || !has_protection(table->protection, mem_protection::kRead)) {
        break;
      }
    }
    const auto fn = reinterpret_cast<uintptr_t>(vtable[n]);
    if (fn == 0) {
      break;
    }
    if (!covers(code, fn)) {
      code = query_memory(fn);
      if (!code ||
          !has_protection(code->protection, mem_protection::kExecute)) {
        break;
      }
    }
  }
  return n < kMax ? n : 0;
}

} // namespace spud

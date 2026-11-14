// Linux: source module retention, FDE lookup through the process unwinder,
// and registration of translated records with libgcc or LLVM libunwind.
#include "memory/unwind.h"

#include <spud/arch.h>

#if SPUD_OS_LINUX
#include "memory/safe_patch.h"
#include "memory/unwind/dwarf.h"

#include <spud/diagnostics.h>

#include <cstring>
#include <string>

#include <dlfcn.h>
#include <link.h>

#if SPUD_UNWIND_SUPPORT
struct dwarf_eh_bases {
  void *text;
  void *data;
  void *function;
};
// Every unwinder that provides __register_frame (libgcc, libgcc_eh, LLVM
// libunwind) defines this lookup in the same object.
extern "C" const void *_Unwind_Find_FDE(void *, dwarf_eh_bases *);
extern "C" void __unw_add_dynamic_fde(uintptr_t) __attribute__((weak));
extern "C" void __register_frame(void *);
extern "C" void __deregister_frame(void *);
#endif

namespace spud::detail {

bool unwind_registration::retain_source_module(uintptr_t source,
                                               void *&module) {
  module = nullptr;
  if (source == 0)
    return true;
  struct image_search {
    uintptr_t address;
    std::string path;
  } image{source, {}};
  dl_iterate_phdr(
      [](dl_phdr_info *module, size_t, void *opaque) {
        auto &image = *static_cast<image_search *>(opaque);
        for (size_t i = 0; i < module->dlpi_phnum; ++i) {
          const auto &segment = module->dlpi_phdr[i];
          const uintptr_t begin = module->dlpi_addr + segment.p_vaddr;
          if (segment.p_type == PT_LOAD && image.address >= begin &&
              image.address - begin < segment.p_memsz) {
            image.path = module->dlpi_name;
            return 1;
          }
        }
        return 0;
      },
      &image);
  if (image.path.empty())
    return true;
  module = dlopen(image.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
  return module != nullptr;
}

void unwind_registration::close_source_module(void *module) {
  dlclose(module);
}

#if SPUD_UNWIND_SUPPORT
namespace dwarf {
namespace {

// A record the unwinder handed out, in place. Its module stays loaded while
// the caller prepares a hook on code in it.
std::span<const uint8_t> record_in_place(uintptr_t address) {
  uint32_t length = 0;
  std::memcpy(&length, reinterpret_cast<const void *>(address), 4);
  if (length < 4 || length > kMetadataLimit - 4)
    return {};
  return {reinterpret_cast<const uint8_t *>(address), size_t{length} + 4};
}

} // namespace

// [begin, end) of the FDE covering `pc`, parsed in place without a copy or
// a guarded read: the unwinder has just read the same bytes.
std::optional<std::pair<uintptr_t, uintptr_t>> covering_extent(uintptr_t pc) {
  dwarf_eh_bases bases{};
  const auto fde = reinterpret_cast<uintptr_t>(
      _Unwind_Find_FDE(reinterpret_cast<void *>(pc), &bases));
  if (fde == 0)
    return std::nullopt;
  const auto fde_bytes = record_in_place(fde);
  if (fde_bytes.size() < 8)
    return std::nullopt;
  uint32_t pointer = 0;
  std::memcpy(&pointer, fde_bytes.data() + 4, 4);
  const uintptr_t cie = fde + 4 - pointer;
  const auto cie_bytes = record_in_place(cie);
  frame f;
  f.text = reinterpret_cast<uintptr_t>(bases.text);
  f.data = reinterpret_cast<uintptr_t>(bases.data);
  if (pointer == 0 || cie_bytes.empty() ||
      !parse_extent(fde_bytes, fde, cie_bytes, cie, f) || f.length == 0 ||
      f.begin > UINTPTR_MAX - f.length ||
      // LLVM libunwind answers an uncovered PC with the cursor's previous
      // record.
      pc < f.begin || pc - f.begin >= f.length)
    return std::nullopt;
  return std::pair{f.begin, f.begin + f.length};
}

bool find_frame(uintptr_t source, size_t, size_t, frame &f, bool &found) {
  found = false;
  dwarf_eh_bases bases{};
  const auto fde = reinterpret_cast<uintptr_t>(
      _Unwind_Find_FDE(reinterpret_cast<void *>(source), &bases));
  if (fde == 0)
    return true;
  f.text = reinterpret_cast<uintptr_t>(bases.text);
  f.data = reinterpret_cast<uintptr_t>(bases.data);
  if (!read_fde(fde, f))
    return false;
  if (source < f.begin || source - f.begin >= f.length) {
    // LLVM libunwind answers an uncovered PC with the cursor's previous
    // record, here the preceding function's. The source has no frame rules.
    f = frame{};
    return true;
  }
  found = true;
  return true;
}

} // namespace dwarf

// One .eh_frame blob per allocation, and the keys it was registered under.
struct unwind_registration::platform {
  std::vector<uint8_t> blob;
  std::vector<void *> keys;
};

unwind_registration::unwind_registration() = default;

unwind_registration::~unwind_registration() {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  if (platform_ != nullptr)
    for (void *key : platform_->keys)
      __deregister_frame(key);
  if (source_module_ != nullptr)
    close_source_module(source_module_);
}

bool unwind_registration::prepare(uintptr_t allocation, size_t size,
                                  uintptr_t entry, uintptr_t source,
                                  size_t copied,
                                  const trampoline_buffer &trampoline,
                                  std::span<const size_t> instruction_offsets,
                                  size_t patch_size, source_view code) {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  if (!retain_source_module(source, source_module_))
    return note_failure("source_module_unavailable", source);
  dwarf::translation translated;
  if (!dwarf::translate(allocation, size, entry, source, copied, trampoline,
                        instruction_offsets, patch_size, code, translated))
    return false;
  source_bytes_ = std::move(translated.snapshots);
  platform_ = std::make_unique<platform>();
  platform_->blob = dwarf::encode(translated.records);
  return true;
}

bool unwind_registration::publish() {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  auto &blob = platform_->blob;
  // libgcc takes the whole terminated blob in one call. LLVM libunwind
  // takes one FDE per call.
  if (__unw_add_dynamic_fde == nullptr) {
    __register_frame(blob.data());
    platform_->keys.push_back(blob.data());
    return true;
  }
  for (size_t at = 0;;) {
    uint32_t length = 0, cie = 0;
    std::memcpy(&length, blob.data() + at, 4);
    if (length == 0)
      break;
    std::memcpy(&cie, blob.data() + at + 4, 4);
    if (cie != 0) {
      __register_frame(blob.data() + at);
      platform_->keys.push_back(blob.data() + at);
    }
    at += 4 + size_t{length};
  }
  return true;
}

std::optional<std::pair<uintptr_t, uintptr_t>>
function_extent(uintptr_t address) {
  const auto extent = dwarf::covering_extent(address);
  if (!extent || extent->first != address)
    return std::nullopt;
  return extent;
}
#endif

} // namespace spud::detail
#endif

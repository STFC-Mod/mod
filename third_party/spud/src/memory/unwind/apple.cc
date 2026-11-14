// Apple: source module retention, function bounds from libunwind and
// LC_FUNCTION_STARTS, frame lookup through libunwind with the compact-unwind
// fallback, and registration of translated records as dynamic images that
// libunwind's section lookup can find.
#include "memory/unwind.h"

#include "memory/readable.h"

#include <spud/arch.h>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace spud::detail {
namespace {

// The Mach-O layouts the function-starts reader needs, spelled out so it
// builds on every host and tests can feed it synthetic images.
struct macho_header {
  uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags,
      reserved;
};
struct macho_command {
  uint32_t cmd, cmdsize;
};
struct macho_segment {
  uint32_t cmd, cmdsize;
  char segname[16];
  uint64_t vmaddr, vmsize, fileoff, filesize;
  uint32_t maxprot, initprot, nsects, flags;
};
struct macho_linkedit_data {
  uint32_t cmd, cmdsize, dataoff, datasize;
};
constexpr uint32_t kMachMagic64 = 0xFEEDFACF;
constexpr uint32_t kSegment64 = 0x19;
constexpr uint32_t kFunctionStarts = 0x26;

// Copies a T out of the image; load commands are not reliably aligned.
template <class T> T load(uintptr_t at) {
  T value;
  std::memcpy(&value, reinterpret_cast<const void *>(at), sizeof(value));
  return value;
}

} // namespace

std::optional<std::pair<uintptr_t, uintptr_t>>
macho_function_extent(uintptr_t header, uintptr_t address) {
  // Every read is checked against the commands' own sizes, and the header,
  // commands and table against the mapping, so a malformed image refuses
  // rather than faults.
  if (readable_bytes_from(header, sizeof(macho_header)) < sizeof(macho_header))
    return std::nullopt;
  const auto mh = load<macho_header>(header);
  const uintptr_t commands = header + sizeof(mh);
  if (mh.magic != kMachMagic64 ||
      readable_bytes_from(commands, mh.sizeofcmds) < mh.sizeofcmds)
    return std::nullopt;
  std::optional<macho_segment> text, linkedit;
  std::optional<macho_linkedit_data> starts;
  for (uint32_t i = 0, at = 0; i < mh.ncmds; ++i) {
    if (mh.sizeofcmds - at < sizeof(macho_command))
      return std::nullopt;
    const auto lc = load<macho_command>(commands + at);
    if (lc.cmdsize < sizeof(lc) || lc.cmdsize > mh.sizeofcmds - at)
      return std::nullopt;
    if (lc.cmd == kSegment64 && lc.cmdsize >= sizeof(macho_segment)) {
      const auto segment = load<macho_segment>(commands + at);
      const std::string_view name(segment.segname,
                                  strnlen(segment.segname, 16));
      if (name == "__TEXT")
        text = segment;
      else if (name == "__LINKEDIT")
        linkedit = segment;
    } else if (lc.cmd == kFunctionStarts &&
               lc.cmdsize >= sizeof(macho_linkedit_data)) {
      starts = load<macho_linkedit_data>(commands + at);
    }
    at += lc.cmdsize;
  }
  // The header is the first byte of __TEXT, so it fixes the slide.
  if (!text || !linkedit || !starts || text->fileoff != 0 || address < header ||
      address - header >= text->vmsize || starts->dataoff < linkedit->fileoff)
    return std::nullopt;
  const uint64_t offset = starts->dataoff - linkedit->fileoff;
  const uint64_t mapped = std::min(linkedit->vmsize, linkedit->filesize);
  if (offset > mapped || starts->datasize > mapped - offset)
    return std::nullopt;
  const uintptr_t table = header - text->vmaddr + linkedit->vmaddr + offset;
  if (readable_bytes_from(table, starts->datasize) < starts->datasize)
    return std::nullopt;

  // ULEB128 deltas from the start of __TEXT to each function in address
  // order, ended by a zero delta or the table's end. The next start is an
  // upper bound: it can include the alignment padding after this function.
  const uint64_t target = address - header;
  const auto *p = reinterpret_cast<const uint8_t *>(table);
  const auto *end = p + starts->datasize;
  uint64_t start = 0;
  bool found = false;
  while (p != end) {
    uint64_t delta = 0;
    for (unsigned shift = 0;; shift += 7) {
      if (p == end || shift > 63)
        return std::nullopt;
      const uint8_t byte = *p++;
      // The tenth byte can only carry bit 63.
      if (shift == 63 && (byte & 0x7E) != 0)
        return std::nullopt;
      delta |= uint64_t{byte & 0x7Fu} << shift;
      if ((byte & 0x80) == 0)
        break;
    }
    if (delta == 0 || delta > text->vmsize - start)
      return std::nullopt;
    start += delta;
    if (found)
      return std::pair{address, header + static_cast<uintptr_t>(start)};
    if (start > target)
      return std::nullopt;
    found = start == target;
  }
  // Not listed, or the last function, whose end the table does not give.
  return std::nullopt;
}

} // namespace spud::detail

#if SPUD_OS_APPLE
#include "memory/safe_patch.h"
#include "memory/unwind/dwarf.h"

#include <spud/diagnostics.h>

#include <mutex>
#include <utility>

#include <dlfcn.h>
#include <mach-o/compact_unwind_encoding.h>
#include <mach-o/loader.h>

#if SPUD_UNWIND_SUPPORT
#include <libunwind.h>

// libunwind SPI, also used by LLVM's Mach-O JIT runtime.
struct unw_dynamic_unwind_sections {
  uintptr_t dso_base;
  uintptr_t dwarf_section;
  size_t dwarf_section_length;
  uintptr_t compact_unwind_section;
  size_t compact_unwind_section_length;
};
extern "C" int __unw_add_find_dynamic_unwind_sections(int (*)(
    uintptr_t, unw_dynamic_unwind_sections *)) __attribute__((weak_import));
extern "C" void __unw_remove_dynamic_fde(uintptr_t)
    __attribute__((weak_import));
extern "C" void __register_frame(void *);
extern "C" void __deregister_frame(void *);
#endif

namespace spud::detail {

bool unwind_registration::retain_source_module(uintptr_t source,
                                               void *&module) {
  module = nullptr;
  Dl_info image{};
  // The main executable is never unloaded.
  if (source == 0 || dladdr(reinterpret_cast<void *>(source), &image) == 0 ||
      static_cast<const mach_header *>(image.dli_fbase)->filetype == MH_EXECUTE)
    return true;
  module = dlopen(image.dli_fname, RTLD_NOW | RTLD_NOLOAD);
  return module != nullptr;
}

void unwind_registration::close_source_module(void *module) {
  dlclose(module);
}

#if SPUD_UNWIND_SUPPORT
namespace {

using dwarf::kArm64;

// A registered blob holds one CIE and FDE.
struct macos_record {
  uintptr_t begin;
  size_t length;
  std::span<const uint8_t> bytes;
};

// A Mach-O header the section lookup hands to libunwind for an allocation's
// records. ARM exception dispatch needs the image's ABI from it.
struct macos_image {
  // Generated code uses the ordinary ABI, including on an arm64e host.
  mach_header_64 header{MH_MAGIC_64,
                        kArm64 ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64,
                        kArm64 ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL,
                        MH_DYLIB,
                        0,
                        0,
                        0,
                        0};
  std::vector<macos_record> records;
  bool published = false;
};

struct macos_registry {
  std::mutex mutex;
  std::vector<macos_image *> images;
};

macos_registry &dynamic_images() {
  // The executable pool and its lookup callback outlive static hook handles.
  static auto *registry = new macos_registry;
  return *registry;
}

int find_macos_sections(uintptr_t pc, unw_dynamic_unwind_sections *info) {
  auto &registry = dynamic_images();
  const std::lock_guard lock(registry.mutex);
  for (const auto *image : registry.images) {
    for (const auto &record : image->records) {
      if (pc >= record.begin && pc - record.begin < record.length) {
        *info = {reinterpret_cast<uintptr_t>(&image->header),
                 reinterpret_cast<uintptr_t>(record.bytes.data()),
                 record.bytes.size(), 0, 0};
        return 1;
      }
    }
  }
  return 0;
}

// Asks libunwind for the entry covering `pc`. Inside the instruction: at a
// shared function boundary Apple's compact-index lookup can select the
// preceding entry.
bool proc_info(uintptr_t pc, unw_proc_info_t &info, int &status) {
  unw_context_t context;
  unw_cursor_t cursor;
  info = {};
  if (unw_getcontext(&context) != UNW_ESUCCESS ||
      unw_init_local(&cursor, &context) != UNW_ESUCCESS ||
      unw_set_reg(&cursor, UNW_REG_IP, pc + 1) != UNW_ESUCCESS)
    return false;
  status = unw_get_proc_info(&cursor, &info);
  return true;
}

} // namespace

namespace dwarf {

std::optional<std::pair<uintptr_t, uintptr_t>> covering_extent(uintptr_t pc) {
  unw_proc_info_t info;
  int status = 0;
  if (!proc_info(pc, info, status) || status != UNW_ESUCCESS ||
      pc < info.start_ip || pc >= info.end_ip)
    return std::nullopt;
  return std::pair{static_cast<uintptr_t>(info.start_ip),
                   static_cast<uintptr_t>(info.end_ip)};
}

bool find_frame(uintptr_t source, size_t copied, size_t patch, frame &f,
                bool &found) {
  found = false;
  unw_proc_info_t info;
  int status = 0;
  if (!proc_info(source, info, status))
    return false;
  if (status == UNW_ENOINFO)
    return true;
  if (status != UNW_ESUCCESS)
    return false;
  // An x86-64 indirect frameless record keeps its stack size in the SUB
  // immediate at a fixed offset from the record's start, and libunwind
  // reads it from memory for every frame in the record. The image's record
  // wins over anything registered later, so a landing over that immediate
  // breaks unwinding through the untouched body as well.
  if (!kArm64 && info.unwind_info == 0 &&
      (info.format & UNWIND_X86_64_MODE_MASK) == UNWIND_X86_64_MODE_STACK_IND) {
    const uintptr_t immediate =
        info.start_ip +
        ((info.format & UNWIND_X86_64_FRAMELESS_STACK_SIZE) >> 16);
    if (immediate < source + patch && immediate + 4 > source)
      return note_failure("stack_size_overwritten", immediate);
  }
  const uintptr_t fde = info.unwind_info;
  if (fde == 0) {
    // A compact entry, rebuilt as DWARF rows.
    f.begin = info.start_ip;
    f.length = info.end_ip - info.start_ip;
    f.personality = info.handler;
    f.lsda = info.lsda;
    if (source != f.begin && f.lsda == 0) {
      Dl_info symbol{};
      // The linker coalesces identical compact rules across functions.
      // Decode this symbol's prologue, rather than snapshotting unrelated
      // functions that another hook in the transaction can overwrite.
      if (source >= f.begin && source - f.begin < f.length &&
          dladdr(reinterpret_cast<void *>(source), &symbol) != 0 &&
          reinterpret_cast<uintptr_t>(symbol.dli_saddr) == source) {
        f.length -= source - f.begin;
        f.begin = source;
      }
    }
    bool calls = false;
    if (!copied_calls(f, source, copied, calls))
      return false;
    frame compact = f;
    if (snapshot_compact(source, info.format, compact) &&
        compact_to_dwarf(source, copied, info.format, compact)) {
      f = std::move(compact);
      found = true;
      return true;
    }
    // A copy without calls cannot unwind through the trampoline with the
    // source's rules in effect, so a leaf record describes it.
    return !calls;
  }
  if (!read_fde(fde, f) || source < f.begin || source - f.begin >= f.length)
    return false;
  found = true;
  return true;
}

} // namespace dwarf

// One blob per record, since a section lookup returns one FDE; and the
// dynamic image those lookups answer with.
struct unwind_registration::platform {
  std::vector<std::vector<uint8_t>> blobs;
  std::vector<void *> keys;
  macos_image *image = nullptr;
};

unwind_registration::unwind_registration() = default;

unwind_registration::~unwind_registration() {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  if (platform_ != nullptr) {
    if (auto *image = platform_->image) {
      if (image->published) {
        auto &registry = dynamic_images();
        {
          const std::lock_guard lock(registry.mutex);
          std::erase(registry.images, image);
        }
        // Section lookups cache by Mach-O header, while __register_frame
        // caches by FDE. Retire both groups, outside the registry lock.
        __unw_remove_dynamic_fde(reinterpret_cast<uintptr_t>(&image->header));
      }
      delete image;
    }
    for (void *key : platform_->keys)
      __deregister_frame(key);
  }
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
  auto state = std::make_unique<platform>();
  for (const auto &record : translated.records)
    state->blobs.push_back(dwarf::encode({&record, 1}));
  static const bool available =
      __unw_add_find_dynamic_unwind_sections != nullptr &&
      __unw_remove_dynamic_fde != nullptr &&
      __unw_add_find_dynamic_unwind_sections(find_macos_sections) == 0;
  if (available) {
    state->image = new macos_image;
    for (size_t i = 0; i < state->blobs.size(); ++i)
      state->image->records.push_back({translated.records[i].pc,
                                       translated.records[i].length,
                                       state->blobs[i]});
  }
  platform_ = std::move(state);
  // Intel libunwind can use a registered FDE directly. ARM exception
  // dispatch also needs the image's ABI header.
  return !kArm64 || available ||
         note_failure("dynamic_unwind_sections_unavailable");
}

bool unwind_registration::publish() {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  auto &state = *platform_;
  state.keys.reserve(state.blobs.size());
  for (auto &blob : state.blobs) {
    uint32_t cie_length = 0;
    std::memcpy(&cie_length, blob.data(), 4);
    void *key = blob.data() + cie_length + 4;
    __register_frame(key);
    state.keys.push_back(key);
  }
  if (state.image != nullptr) {
    auto &registry = dynamic_images();
    const std::lock_guard lock(registry.mutex);
    registry.images.push_back(state.image);
    state.image->published = true;
  }
  // Each record must now be the one libunwind finds for its range.
  for (size_t i = 0; i < state.blobs.size(); ++i) {
    const auto *fde = static_cast<const uint8_t *>(state.keys[i]);
    uintptr_t begin, length;
    std::memcpy(&begin, fde + 8, 8);
    std::memcpy(&length, fde + 16, 8);
    unw_proc_info_t info;
    int status = 0;
    // proc_info asks at pc + 1, which a 1-byte range does not contain.
    const uintptr_t pc = length > 1 ? begin : begin - 1;
    if (!proc_info(pc, info, status) || status != UNW_ESUCCESS ||
        info.unwind_info != reinterpret_cast<uintptr_t>(fde) ||
        info.start_ip != begin || info.end_ip != begin + length)
      return note_failure("frame_registration", begin);
  }
  return true;
}

std::optional<std::pair<uintptr_t, uintptr_t>>
function_extent(uintptr_t address) {
  // ld64 coalesces neighbouring functions with the same compact encoding
  // into one entry. It starts at the first of them, so a later one gets no
  // unwind extent, and the first gets one that runs over the others. The
  // image's function starts bound both.
  std::optional<std::pair<uintptr_t, uintptr_t>> listed;
  if (Dl_info image{}; dladdr(reinterpret_cast<void *>(address), &image) != 0 &&
                       image.dli_fbase != nullptr)
    listed = macho_function_extent(reinterpret_cast<uintptr_t>(image.dli_fbase),
                                   address);
  unw_proc_info_t info;
  int status = 0;
  if (!proc_info(address, info, status) || status != UNW_ESUCCESS ||
      info.start_ip != address || info.end_ip <= address)
    return listed;
  if (listed && listed->second < info.end_ip)
    return listed;
  return std::pair{static_cast<uintptr_t>(info.start_ip),
                   static_cast<uintptr_t>(info.end_ip)};
}
#endif

} // namespace spud::detail
#endif

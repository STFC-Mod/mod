// Pattern parsing, the scan driver over the SIMD kernels, and the per-OS
// walk over executable mappings for find_in_module and find_in_process.
#include <spud/arch.h>
#include <spud/signature.h>

#include "module_path.h"
#include "scan.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#if SPUD_OS_WIN
#include <windows.h>

#include <psapi.h>
#endif

#if SPUD_OS_MAC
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#include <unistd.h>
#endif

#if SPUD_OS_LINUX
#include <cstdio>
#include <link.h>
#include <unistd.h>
#endif

namespace spud {

compiled_signature compile_signature(std::string_view pattern) {
  compiled_signature compiled;
  const auto status =
      detail::parse_pattern(pattern, [&](uint8_t byte, bool fixed) {
        compiled.bytes_.push_back(byte);
        compiled.mask_.push_back(fixed ? uint8_t{0xFF} : uint8_t{0});
      });
  compiled.error_ = status.error;
  compiled.error_offset_ = status.offset;
  if (status.error != signature_error::kOk) {
    compiled.bytes_.clear();
    compiled.mask_.clear();
  }
  return compiled;
}

const std::error_category &signature_category() noexcept {
  struct category final : std::error_category {
    const char *name() const noexcept override {
      return "spud.signature";
    }
    std::string message(int value) const override {
      return std::string(to_string(static_cast<signature_error>(value)));
    }
  };
  static const category instance;
  return instance;
}

namespace {

#if SPUD_ARCH_X86_FAMILY
void run_cpuid(uint32_t eax, uint32_t ecx, uint32_t *abcd) {
#if defined(_MSC_VER)
  __cpuidex(reinterpret_cast<int *>(abcd), eax, ecx);
#else
  uint32_t ebx = 0;
  uint32_t edx = 0;
  __asm__("cpuid" : "+b"(ebx), "+a"(eax), "+c"(ecx), "=d"(edx));
  abcd[0] = eax;
  abcd[1] = ebx;
  abcd[2] = ecx;
  abcd[3] = edx;
#endif
}

uint32_t xgetbv0() {
#if defined(_MSC_VER)
  return static_cast<uint32_t>(_xgetbv(0));
#else
  uint32_t eax = 0;
  uint32_t edx = 0;
  __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
  (void)edx;
  return eax;
#endif
}

bool cpu_has_avx2() {
  static const bool cached = [] {
    uint32_t abcd[4] = {};
    run_cpuid(1, 0, abcd);
    const bool osxsave = (abcd[2] & (1u << 27)) != 0;
    const bool avx = (abcd[2] & (1u << 28)) != 0;
    if (!osxsave || !avx || (xgetbv0() & 0x6u) != 0x6u) {
      return false;
    }
    run_cpuid(7, 0, abcd);
    return (abcd[1] & (1u << 5)) != 0;
  }();
  return cached;
}
#endif

// How common each byte value is in x86-64 code, 0 for the rarest and 255
// for the most common (00, then 48, 24, 8B, FF, CC). Counted over the .text
// of MSVC code (the test corpus) and of GCC and clang code (libstdc++, libc,
// gdb, cmake, libLLVM, Qt, git), with MSVC and the ELF set weighted equally.
// Anchoring on the rarest fixed bytes keeps false candidates, and with them
// full compares, rare. On other code the ranking is merely less sharp.
constexpr uint8_t kByteRank[256] = {
    255, 242, 219, 207, 224, 217, 156, 171,
    240, 169, 160, 127, 172, 179, 126, 246, // 0_
    238, 146, 130, 115, 159, 229, 104, 133,
    210, 89,  60,  97,  144, 82,  189, 225, // 1_
    237, 90,  81,  75,  253, 173, 83,  49,
    232, 147, 25,  145, 77,  53,  174, 199, // 2_
    233, 201, 43,  176, 108, 153, 28,  47,
    205, 204, 42,  122, 119, 192, 50,  107, // 3_
    235, 223, 99,  135, 247, 195, 101, 113,
    254, 226, 69,  57,  248, 185, 61,  52, // 4_
    211, 34,  32,  102, 230, 170, 91,  215,
    181, 26,  16,  87,  136, 162, 109, 213, // 5_
    194, 22,  46,  111, 163, 51,  214, 100,
    167, 24,  17,  116, 121, 37,  94,  140, // 6_
    184, 27,  84,  96,  231, 203, 86,  151,
    168, 68,  41,  73,  186, 117, 93,  175, // 7_
    218, 158, 105, 243, 241, 236, 59,  76,
    183, 249, 15,  252, 221, 245, 48,  33, // 8_
    198, 10,  19,  31,  178, 103, 11,  14,
    142, 20,  3,   6,   63,  39,  4,   8, // 9_
    150, 5,   2,   12,  118, 21,  1,   7,
    131, 9,   40,  70,  66,  18,  0,   23, // A_
    152, 13,  80,  64,  98,  71,  196, 78,
    193, 177, 154, 45,  155, 123, 132, 74, // B_
    239, 209, 143, 234, 228, 212, 187, 216,
    202, 164, 85,  30,  250, 35,  62,  56, // C_
    188, 138, 148, 58,  54,  38,  65,  67,
    141, 44,  55,  92,  29,  36,  72,  134, // D_
    191, 129, 137, 106, 125, 95,  79,  120,
    244, 227, 149, 197, 208, 128, 124, 139, // E_
    190, 112, 114, 200, 110, 88,  166, 157,
    220, 182, 206, 161, 165, 180, 222, 251, // F_
};

using detail::match_fn;
using scan_kernel = size_t (*)(const detail::pattern_scan &, match_fn, void *);

scan_kernel pick_kernel(scan_backend backend,
                        [[maybe_unused]] const detail::pattern_scan &scan) {
#if SPUD_ARCH_X86_FAMILY
  // First anchors ranked below this are rare enough in code that memchr
  // over them, at about 60 GB/s, beats the two-anchor kernels at about 50.
  // Measured per byte over the test corpus and libstdc++: every byte ranked
  // below 16 but one was faster scalar, and from rank 22 the ranking stops
  // predicting it (61 and 69 are ASCII, common in data next to code).
  constexpr uint8_t kMemchrRank = 16;
  // SSE2 is part of x86-64; only AVX2 needs a CPU check.
  switch (backend) {
  case scan_backend::kAutomatic:
    if (kByteRank[scan.first_byte] < kMemchrRank) {
      return nullptr;
    }
    return cpu_has_avx2() ? detail::scan_anchors_avx2
                          : detail::scan_anchors_sse2;
  case scan_backend::kAvx2:
    return cpu_has_avx2() ? detail::scan_anchors_avx2 : nullptr;
  case scan_backend::kSse2:
    return detail::scan_anchors_sse2;
  default:
    return nullptr;
  }
#elif SPUD_ARCH_ARM_FAMILY
  return backend == scan_backend::kAutomatic || backend == scan_backend::kNeon
             ? detail::scan_anchors_neon
             : nullptr;
#else
  (void)backend;
  return nullptr;
#endif
}

constexpr size_t kNone = static_cast<size_t>(-1);

// The rarest fixed byte, then the rarest other fixed byte at least 4 bytes
// away. Neighbours are correlated in code (48 8B, 89 44 24), so a close
// pair of anchors filters little more than one; a near byte is used only
// when no far one exists. Anchors are compared whole, so only bytes with a
// 0xFF mask qualify; `first` is kNone when a hand-built view has none.
detail::pattern_scan prepare_scan(signature_view pattern,
                                  std::span<const uint8_t> buffer) {
  auto rarer = [&](size_t i, size_t best) {
    return best == kNone ||
           kByteRank[pattern.bytes[i]] < kByteRank[pattern.bytes[best]];
  };
  size_t first = kNone;
  for (size_t i = 0; i < pattern.size; ++i) {
    if (pattern.mask[i] == 0xFF && rarer(i, first)) {
      first = i;
    }
  }
  size_t distant = kNone;
  size_t nearby = kNone;
  for (size_t i = 0; i < pattern.size; ++i) {
    if (pattern.mask[i] != 0xFF || i == first) {
      continue;
    }
    const size_t distance = i > first ? i - first : first - i;
    size_t &pick = distance >= 4 ? distant : nearby;
    if (rarer(i, pick)) {
      pick = i;
    }
  }
  const size_t second = distant != kNone  ? distant
                        : nearby != kNone ? nearby
                                          : first;
  if (first == kNone) {
    return {.buffer = buffer.data(),
            .pattern = pattern,
            .last_start = buffer.size() - pattern.size,
            .first = kNone};
  }
  return {
      .buffer = buffer.data(),
      .pattern = pattern,
      .last_start = buffer.size() - pattern.size,
      .first = first,
      .second = second,
      .first_byte = pattern.bytes[first],
      .second_byte = pattern.bytes[second],
  };
}

// The scalar backend and the tail after a kernel: memchr for the rarest
// anchor, then the second anchor, then the full compare.
void scan_scalar(const detail::pattern_scan &scan, size_t offset,
                 match_fn on_match, void *ctx) {
  while (offset <= scan.last_start) {
    const auto *hit = static_cast<const uint8_t *>(
        std::memchr(scan.buffer + offset + scan.first, scan.first_byte,
                    scan.last_start - offset + 1));
    if (hit == nullptr) {
      return;
    }
    const auto start = static_cast<size_t>(hit - scan.buffer) - scan.first;
    if (scan.buffer[start + scan.second] == scan.second_byte &&
        detail::matches_at(scan, start) && !on_match(ctx, start)) {
      return;
    }
    offset = start + 1;
  }
}

// Reports every match of `pattern` in `buffer` to on_match, in order, until
// it returns false.
void scan_buffer(signature_view pattern, std::span<const uint8_t> buffer,
                 scan_backend backend, match_fn on_match, void *ctx) {
  // A view from a failed parse is empty; one with no fixed byte did not come
  // from the parser and would match everywhere.
  if (pattern.size == 0 || pattern.size > buffer.size() ||
      std::none_of(pattern.mask, pattern.mask + pattern.size,
                   [](uint8_t m) { return m != 0; })) {
    return;
  }
  const auto scan = prepare_scan(pattern, buffer);
  if (scan.first == kNone) {
    // Only partial masks, so nothing to anchor on: compare at every start.
    for (size_t start = 0; start <= scan.last_start; ++start) {
      if (detail::matches_at(scan, start) && !on_match(ctx, start)) {
        return;
      }
    }
    return;
  }
  size_t offset = 0;
  if (const scan_kernel kernel = pick_kernel(backend, scan)) {
    offset = kernel(scan, on_match, ctx);
    if (offset == detail::kScanStopped) {
      return;
    }
  }
  scan_scalar(scan, offset, on_match, ctx);
}

struct match_list {
  std::span<const uint8_t> buffer;
  size_t max_results;
  std::vector<signature_match> *out;

  static bool add(void *self, size_t start) {
    auto *list = static_cast<match_list *>(self);
    list->out->emplace_back(list->buffer, start);
    return list->out->size() < list->max_results;
  }
};

void collect_matches(signature_view pattern, std::span<const uint8_t> buffer,
                     const scan_options &options,
                     std::vector<signature_match> *out) {
  if (out->size() >= options.max_results) {
    return;
  }
  match_list list{buffer, options.max_results, out};
  scan_buffer(pattern, buffer, options.backend, match_list::add, &list);
}

// Patching code with mprotect splits one r-x mapping into several that touch
// end to end, and the loader may lay out adjacent executable segments the
// same way. Scan each contiguous run as one buffer so a pattern straddling
// the seam still matches.
void merge_touching(std::vector<std::span<const uint8_t>> *ranges) {
  std::sort(ranges->begin(), ranges->end(),
            [](const auto &a, const auto &b) { return a.data() < b.data(); });
  size_t out = 0;
  for (size_t i = 0; i < ranges->size(); ++i) {
    const auto range = (*ranges)[i];
    if (out != 0) {
      auto &last = (*ranges)[out - 1];
      if (last.data() + last.size() == range.data()) {
        last = {last.data(), last.size() + range.size()};
        continue;
      }
    }
    (*ranges)[out++] = range;
  }
  ranges->resize(out);
}

signature_matches scan_ranges(signature_view pattern,
                              std::vector<std::span<const uint8_t>> ranges,
                              const scan_options &options) {
  merge_touching(&ranges);
  std::vector<signature_match> matches;
  for (auto range : ranges) {
    collect_matches(pattern, range, options, &matches);
  }
  return signature_matches(std::move(matches));
}

#if !SPUD_OS_WIN
// An empty module is the main executable. Mappings without a path
// (anonymous code, trampolines) never match.
bool path_is_module(std::string_view path, std::string_view module,
                    std::string_view main_exe) {
  if (module.empty()) {
    return !main_exe.empty() && path == main_exe;
  }
  return detail::path_matches_module(path, module);
}
#endif

#if SPUD_OS_WIN
// The committed, readable and executable pages in [start, start + size).
// Section headers say what the image asked for, not what is mapped now: a
// packer can leave a section reserved, PAGE_NOACCESS or guarded until it
// unpacks, and reading such a page faults.
void add_accessible_code(uintptr_t start, size_t size,
                         std::vector<std::span<const uint8_t>> *sections) {
  constexpr DWORD kReadableCode =
      PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  const uintptr_t end = start + size;
  uintptr_t cursor = start;
  while (cursor < end) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info)) ==
        0) {
      return;
    }
    const auto region_end =
        reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (region_end <= cursor) {
      return;
    }
    const uintptr_t stop = region_end < end ? region_end : end;
    if (info.State == MEM_COMMIT && (info.Protect & kReadableCode) != 0 &&
        (info.Protect & PAGE_GUARD) == 0) {
      sections->emplace_back(reinterpret_cast<const uint8_t *>(cursor),
                             static_cast<size_t>(stop - cursor));
    }
    cursor = stop;
  }
}

void collect_pe_rx(uintptr_t module_handle,
                   std::vector<std::span<const uint8_t>> *sections) {
  const auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
  if (dos_header == nullptr || dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
    return;
  }
  const auto nt_headers =
      reinterpret_cast<PIMAGE_NT_HEADERS>(module_handle + dos_header->e_lfanew);
  if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
    return;
  }
  auto nt_section = IMAGE_FIRST_SECTION(nt_headers);
  for (size_t i = 0; i < nt_headers->FileHeader.NumberOfSections;
       i++, nt_section++) {
    const auto readable_exec =
        (nt_section->Characteristics & IMAGE_SCN_MEM_READ) != 0 &&
        (nt_section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    if (!readable_exec) {
      continue;
    }
    add_accessible_code(module_handle + nt_section->VirtualAddress,
                        nt_section->Misc.VirtualSize, sections);
  }
}

// EnumProcessModules reports the size it needed; a module can load between
// the two calls, so ask again until the list fits.
std::vector<HMODULE> process_modules() {
  std::vector<HMODULE> modules(256);
  for (;;) {
    const auto bytes = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), modules.data(), bytes,
                           &needed) == 0) {
      return {};
    }
    const bool fits = needed <= bytes;
    modules.resize(needed / sizeof(HMODULE));
    if (fits) {
      return modules;
    }
  }
}

std::vector<std::span<const uint8_t>> executable_ranges(std::string_view module,
                                                        bool all_modules) {
  std::vector<std::span<const uint8_t>> sections;
  if (!all_modules && module.empty()) {
    collect_pe_rx(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)),
                  &sections);
    return sections;
  }
  for (HMODULE handle : process_modules()) {
    if (!all_modules) {
      // GetModuleHandleA would append ".dll" to "Game" and needs the exact
      // file name; match names the same way the other platforms do.
      char path[4096];
      const DWORD n = GetModuleFileNameA(handle, path, sizeof(path));
      if (n == 0 || n >= sizeof(path) ||
          !detail::path_matches_module(std::string_view(path, n), module)) {
        continue;
      }
    }
    collect_pe_rx(reinterpret_cast<uintptr_t>(handle), &sections);
  }
  return sections;
}
#elif SPUD_OS_MAC
std::vector<std::span<const uint8_t>> executable_ranges(std::string_view module,
                                                        bool all_modules) {
  char exe_path[PROC_PIDPATHINFO_MAXSIZE] = {};
  std::string_view main_exe;
  if (proc_pidpath(getpid(), exe_path, sizeof(exe_path)) > 0) {
    main_exe = exe_path;
  }

  std::vector<std::span<const uint8_t>> sections;
  const pid_t pid = getpid();
  const mach_port_t task = mach_task_self();
  mach_vm_address_t address = 0;
  natural_t depth = 0;
  for (;;) {
    mach_vm_size_t size = 0;
    vm_region_submap_info_64 info{};
    // In-out: the call writes back how much of info it filled.
    mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
    if (mach_vm_region_recurse(
            task, &address, &size, &depth,
            reinterpret_cast<vm_region_recurse_info_t>(&info),
            &count) != KERN_SUCCESS) {
      break;
    }
    // The dyld shared cache is one submap at the top level; its code pages
    // and their protections are only visible one level down. The call sets
    // depth back when the walk leaves the submap.
    if (info.is_submap) {
      ++depth;
      continue;
    }
    const bool readable_exec = (info.protection & VM_PROT_READ) != 0 &&
                               (info.protection & VM_PROT_EXECUTE) != 0;
    bool wanted = readable_exec && all_modules;
    if (readable_exec && !all_modules) {
      char path[PROC_PIDPATHINFO_MAXSIZE] = {};
      const int n = proc_regionfilename(pid, static_cast<uint64_t>(address),
                                        path, sizeof(path));
      wanted = n > 0 &&
               path_is_module(std::string_view(path, static_cast<size_t>(n)),
                              module, main_exe);
    }
    if (wanted) {
      sections.emplace_back(reinterpret_cast<const uint8_t *>(address),
                            static_cast<size_t>(size));
    }
    address += size;
  }
  return sections;
}
#elif SPUD_OS_LINUX
std::string main_exe_path() {
  char exe_buf[4096];
  const ssize_t n = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
  return n > 0 ? std::string(exe_buf, static_cast<size_t>(n)) : std::string();
}

// One module's code from the loader's own list: its executable PT_LOAD
// segments. This is about 1 us against 60 us or more for reading and
// parsing /proc/self/maps.
std::vector<std::span<const uint8_t>> module_ranges(std::string_view module) {
  struct walk {
    std::string_view module;
    std::string main_exe;
    bool first = true;
    std::vector<std::span<const uint8_t>> ranges;
  } state{module, main_exe_path(), true, {}};
  dl_iterate_phdr(
      [](dl_phdr_info *info, size_t, void *ctx) {
        auto *w = static_cast<walk *>(ctx);
        // The main program comes first and the loader gives it no name;
        // use the path /proc/self/maps would show for it.
        const std::string_view name =
            w->first ? std::string_view(w->main_exe)
                     : std::string_view(
                           info->dlpi_name != nullptr ? info->dlpi_name : "");
        w->first = false;
        if (!path_is_module(name, w->module, w->main_exe)) {
          return 0;
        }
        for (size_t i = 0; i < info->dlpi_phnum; ++i) {
          const auto &ph = info->dlpi_phdr[i];
          if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X) != 0 &&
              (ph.p_flags & PF_R) != 0 && ph.p_memsz != 0) {
            w->ranges.emplace_back(
                reinterpret_cast<const uint8_t *>(info->dlpi_addr + ph.p_vaddr),
                static_cast<size_t>(ph.p_memsz));
          }
        }
        return 0;
      },
      &state);
  return std::move(state.ranges);
}

// Every r-x mapping, from /proc/self/maps: unlike the loader's list it also
// has anonymous code such as JIT output and trampolines.
std::vector<std::span<const uint8_t>> process_ranges() {
  std::ifstream maps_file("/proc/self/maps");
  if (!maps_file.is_open()) {
    return {};
  }

  std::vector<std::span<const uint8_t>> sections;
  std::string line;
  while (std::getline(maps_file, line)) {
    unsigned long addr_start = 0;
    unsigned long addr_end = 0;
    char perms_str[8] = {};
    if (std::sscanf(line.c_str(), "%lx-%lx %7s", &addr_start, &addr_end,
                    perms_str) < 3) {
      continue;
    }
    if (perms_str[0] != 'r' || perms_str[2] != 'x') {
      continue;
    }
    if (addr_end <= addr_start) {
      continue;
    }
    const auto start = reinterpret_cast<const uint8_t *>(addr_start);
    sections.emplace_back(start, static_cast<size_t>(addr_end - addr_start));
  }
  return sections;
}

std::vector<std::span<const uint8_t>> executable_ranges(std::string_view module,
                                                        bool all_modules) {
  return all_modules ? process_ranges() : module_ranges(module);
}
#endif

} // namespace

signature_matches find_matches(signature_view pattern,
                               std::span<const uint8_t> buffer,
                               scan_options options) {
  std::vector<signature_match> matches;
  collect_matches(pattern, buffer, options, &matches);
  return signature_matches(std::move(matches));
}

signature_matches find_matches(std::string_view pattern,
                               std::span<const uint8_t> buffer,
                               scan_options options) {
  return find_matches(compile_signature(pattern), buffer, options);
}

signature_match find_first(signature_view pattern,
                           std::span<const uint8_t> buffer,
                           scan_options options) {
  signature_match first;
  struct slot {
    std::span<const uint8_t> buffer;
    signature_match *out;
  } target{buffer, &first};
  scan_buffer(
      pattern, buffer, options.backend,
      [](void *ctx, size_t start) {
        auto *s = static_cast<slot *>(ctx);
        *s->out = signature_match(s->buffer, start);
        return false;
      },
      &target);
  return first;
}

signature_match find_first(std::string_view pattern,
                           std::span<const uint8_t> buffer,
                           scan_options options) {
  return find_first(compile_signature(pattern), buffer, options);
}

signature_matches find_in_module(signature_view pattern,
                                 std::string_view module,
                                 scan_options options) {
  return scan_ranges(pattern, executable_ranges(module, false), options);
}

signature_matches find_in_module(std::string_view pattern,
                                 std::string_view module,
                                 scan_options options) {
  return find_in_module(compile_signature(pattern), module, options);
}

std::vector<signature_match>
find_first_each(std::span<const signature_view> patterns,
                std::string_view module, scan_options options) {
  std::vector<signature_match> found(patterns.size());
  size_t longest = 0;
  for (const auto &pattern : patterns) {
    longest = std::max(longest, pattern.size);
  }
  if (longest == 0) {
    return found;
  }
  auto ranges = executable_ranges(module, false);
  merge_touching(&ranges);

  // Worker `first` of `stride` takes patterns first, first + stride, ...
  // Each writes only its own entries of `found`.
  auto scan_share = [&](size_t first, size_t stride) {
    // Half of a typical 512 KiB-2 MiB L2, so a chunk stays cached while
    // every pattern scans it. On libLLVM's 76 MB of code with 200 patterns,
    // 64 KiB to 4 MiB measured the same within noise: once the chunk is in
    // cache the kernels, not memory, set the pace.
    constexpr size_t kChunk = size_t{256} << 10;
    struct slot {
      std::span<const uint8_t> range;
      size_t base;
      signature_match *out;
    };
    size_t left = (patterns.size() - first + stride - 1) / stride;
    for (const auto range : ranges) {
      // Each window runs longest - 1 bytes into the next chunk, so every
      // start in the chunk is checked with its whole pattern. Starts past
      // the chunk may be checked twice, which is harmless: a pattern found
      // in one window is not scanned again.
      for (size_t chunk = 0; chunk < range.size() && left != 0;
           chunk += kChunk) {
        const size_t stop =
            std::min(range.size() - chunk, kChunk + longest - 1) + chunk;
        const auto window = range.subspan(chunk, stop - chunk);
        for (size_t i = first; i < patterns.size(); i += stride) {
          if (found[i]) {
            continue;
          }
          slot target{range, chunk, &found[i]};
          scan_buffer(
              patterns[i], window, options.backend,
              [](void *ctx, size_t start) {
                auto *s = static_cast<slot *>(ctx);
                *s->out = signature_match(s->range, s->base + start);
                return false;
              },
              &target);
          if (found[i]) {
            --left;
          }
        }
      }
    }
  };

  const size_t threads =
      std::clamp<size_t>(options.threads, 1, patterns.size());
  std::vector<std::thread> workers;
  workers.reserve(threads - 1);
  for (size_t t = 1; t < threads; ++t) {
    workers.emplace_back(scan_share, t, threads);
  }
  scan_share(0, threads);
  for (auto &worker : workers) {
    worker.join();
  }
  return found;
}

signature_matches find_in_process(signature_view pattern,
                                  scan_options options) {
  return scan_ranges(pattern, executable_ranges({}, true), options);
}

signature_matches find_in_process(std::string_view pattern,
                                  scan_options options) {
  return find_in_process(compile_signature(pattern), options);
}

} // namespace spud

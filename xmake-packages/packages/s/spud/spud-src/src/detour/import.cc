#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <spud/arch.h>
#include <spud/pointer.h>

#include "module_path.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#ifdef SPUD_OS_WIN
#include <windows.h>
#elif SPUD_OS_LINUX
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#elif SPUD_OS_APPLE
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#endif

namespace spud {
namespace {

// Compares without measuring sym_name first: nearly every symbol in a
// large library differs from the name in its first few bytes.
bool name_is(const char *sym_name, std::string_view name) {
  for (const char c : name) {
    if (*sym_name != c || c == '\0') {
      return false;
    }
    ++sym_name;
  }
  return *sym_name == '\0';
}

#if SPUD_ARCH_X86_64
// `jmp qword ptr [rip+disp32]`, the shape of a PLT entry or an import thunk.
void **slot_from_jmp_indirect(const void *function) {
  const auto *p = static_cast<const uint8_t *>(function);
  if (p == nullptr || p[0] != 0xFF || p[1] != 0x25) {
    return nullptr;
  }
  int32_t rel = 0;
  std::memcpy(&rel, p + 2, sizeof(rel));
  return reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(p) + 6 +
                                   static_cast<intptr_t>(rel));
}

// Follows up to `depth` rel32 jumps and one indirect jump to the function
// the slot currently names.
const void *follow_jmp_indirect(const void *function, int depth = 4) {
  if (function == nullptr || depth <= 0) {
    return function;
  }
  if (void **slot = slot_from_jmp_indirect(function)) {
    return *slot;
  }
  const auto *p = static_cast<const uint8_t *>(function);
  if (p[0] == 0xE9) {
    int32_t rel = 0;
    std::memcpy(&rel, p + 1, sizeof(rel));
    return follow_jmp_indirect(p + 5 + rel, depth - 1);
  }
  return function;
}
#endif

#ifdef SPUD_OS_LINUX
// A JUMP_SLOT found still pointing into its own object, to be resolved once
// dl_iterate_phdr has dropped the loader lock.
struct lazy_slot {
  size_t index;
  const char *name;
  const char *version;
  const char *object;
};

struct import_query {
  std::string_view module;
  std::string_view name;
  const void *by_value = nullptr;
  std::vector<import_slot> *out;
  std::vector<lazy_slot> lazy;
};

// The parts of one loaded object's dynamic section the walk needs.
struct elf_object {
  const char *path;
  ElfW(Addr) base;
  uintptr_t start;
  uintptr_t end;
  const ElfW(Sym) * symtab;
  const char *strtab;
  const ElfW(Half) * versym;
  const ElfW(Verneed) * verneed;
};

bool slot_wanted(const import_query &q, const char *sym_name, void **slot) {
  if (!q.name.empty() && name_is(sym_name, q.name)) {
    return true;
  }
  if (q.by_value != nullptr && *slot == q.by_value) {
    return true;
  }
  return false;
}

bool reloc_is_slot(ElfW(Word) type) {
#if SPUD_ARCH_X86_64
  return type == R_X86_64_JUMP_SLOT || type == R_X86_64_GLOB_DAT;
#elif SPUD_ARCH_ARM64
  return type == R_AARCH64_JUMP_SLOT || type == R_AARCH64_GLOB_DAT;
#else
  (void)type;
  return false;
#endif
}

bool reloc_is_lazy(ElfW(Word) type) {
#if SPUD_ARCH_X86_64
  return type == R_X86_64_JUMP_SLOT;
#elif SPUD_ARCH_ARM64
  return type == R_AARCH64_JUMP_SLOT;
#else
  (void)type;
  return false;
#endif
}

// The version the object asked for, e.g. GLIBC_2.2.5, so the resolve below
// picks the same definition the dynamic linker would.
const char *needed_version(const elf_object &o, size_t symi) {
  if (o.versym == nullptr || o.verneed == nullptr) {
    return nullptr;
  }
  const ElfW(Half) want = o.versym[symi] & 0x7fff;
  if (want <= 1) { // VER_NDX_LOCAL, VER_NDX_GLOBAL
    return nullptr;
  }
  const auto *need = o.verneed;
  for (;;) {
    const auto *aux = reinterpret_cast<const ElfW(Vernaux) *>(
        reinterpret_cast<const char *>(need) + need->vn_aux);
    for (ElfW(Half) k = 0; k < need->vn_cnt; ++k) {
      if ((aux->vna_other & 0x7fff) == want) {
        return o.strtab + aux->vna_name;
      }
      aux = reinterpret_cast<const ElfW(Vernaux) *>(
          reinterpret_cast<const char *>(aux) + aux->vna_next);
    }
    if (need->vn_next == 0) {
      return nullptr;
    }
    need = reinterpret_cast<const ElfW(Verneed) *>(
        reinterpret_cast<const char *>(need) + need->vn_next);
  }
}

template <typename Rel>
void collect_relocs(const Rel *rel, size_t count, const elf_object &o,
                    import_query &q) {
  if (rel == nullptr || o.symtab == nullptr || o.strtab == nullptr) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    const auto type = static_cast<ElfW(Word)>(ELF64_R_TYPE(rel[i].r_info));
    const auto symi = ELF64_R_SYM(rel[i].r_info);
    if (!reloc_is_slot(type) || symi == 0) {
      continue;
    }
    const char *sym_name = o.strtab + o.symtab[symi].st_name;
    auto **slot = reinterpret_cast<void **>(o.base + rel[i].r_offset);
    if (!slot_wanted(q, sym_name, slot)) {
      continue;
    }
    // An unbound lazy slot points at the object's own PLT. A slot already
    // bound to a definition inside the same object looks the same, and
    // resolving it again lands where the dynamic linker bound it.
    const auto value = reinterpret_cast<uintptr_t>(*slot);
    if (reloc_is_lazy(type) && value >= o.start && value < o.end) {
      q.lazy.push_back(
          {q.out->size(), sym_name, needed_version(o, symi), o.path});
    }
    q.out->push_back({slot});
  }
}

int visit_elf_object(dl_phdr_info *info, size_t, void *data) {
  auto *q = static_cast<import_query *>(data);
  const char *path = info->dlpi_name ? info->dlpi_name : "";
  if (!detail::path_matches_module(path, q->module)) {
    return 0;
  }

  const auto base = static_cast<ElfW(Addr)>(info->dlpi_addr);
  elf_object o{path, base, UINTPTR_MAX, 0, nullptr, nullptr, nullptr, nullptr};
  const ElfW(Phdr) *dynamic = nullptr;
  for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
    const ElfW(Phdr) &ph = info->dlpi_phdr[i];
    if (ph.p_type == PT_DYNAMIC) {
      dynamic = &ph;
    } else if (ph.p_type == PT_LOAD) {
      o.start = std::min<uintptr_t>(o.start, base + ph.p_vaddr);
      o.end = std::max<uintptr_t>(o.end, base + ph.p_vaddr + ph.p_memsz);
    }
  }
  if (dynamic == nullptr) {
    return 0;
  }

  auto dyn_ptr = [base](ElfW(Addr) p) -> uintptr_t {
    return p >= base ? static_cast<uintptr_t>(p)
                     : static_cast<uintptr_t>(base + p);
  };
  const auto *dyn =
      reinterpret_cast<const ElfW(Dyn) *>(dyn_ptr(dynamic->p_vaddr));

  const void *jmprel = nullptr;
  const void *rela = nullptr;
  const void *rel = nullptr;
  size_t jmprel_size = 0;
  size_t rela_size = 0;
  size_t rel_size = 0;
  // RELATIVE relocations come first in DT_RELA / DT_REL, and in a large
  // library there are hundreds of thousands of them; none names a symbol.
  size_t rela_relative = 0;
  size_t rel_relative = 0;
  ElfW(Sword) pltrel = DT_RELA;

  for (const auto *d = dyn; d->d_tag != DT_NULL; ++d) {
    switch (d->d_tag) {
    case DT_SYMTAB:
      o.symtab = reinterpret_cast<const ElfW(Sym) *>(dyn_ptr(d->d_un.d_ptr));
      break;
    case DT_STRTAB:
      o.strtab = reinterpret_cast<const char *>(dyn_ptr(d->d_un.d_ptr));
      break;
    case DT_VERSYM:
      o.versym = reinterpret_cast<const ElfW(Half) *>(dyn_ptr(d->d_un.d_ptr));
      break;
    case DT_VERNEED:
      o.verneed =
          reinterpret_cast<const ElfW(Verneed) *>(dyn_ptr(d->d_un.d_ptr));
      break;
    case DT_JMPREL:
      jmprel = reinterpret_cast<const void *>(dyn_ptr(d->d_un.d_ptr));
      break;
    case DT_PLTRELSZ:
      jmprel_size = d->d_un.d_val;
      break;
    case DT_PLTREL:
      pltrel = static_cast<ElfW(Sword)>(d->d_un.d_val);
      break;
    case DT_RELA:
      rela = reinterpret_cast<const void *>(dyn_ptr(d->d_un.d_ptr));
      break;
    case DT_RELASZ:
      rela_size = d->d_un.d_val;
      break;
    case DT_REL:
      rel = reinterpret_cast<const void *>(dyn_ptr(d->d_un.d_ptr));
      break;
    case DT_RELSZ:
      rel_size = d->d_un.d_val;
      break;
    case DT_RELACOUNT:
      rela_relative = d->d_un.d_val;
      break;
    case DT_RELCOUNT:
      rel_relative = d->d_un.d_val;
      break;
    default:
      break;
    }
  }

  if (jmprel != nullptr && jmprel_size != 0) {
    if (pltrel == DT_REL) {
      collect_relocs(static_cast<const ElfW(Rel) *>(jmprel),
                     jmprel_size / sizeof(ElfW(Rel)), o, *q);
    } else {
      collect_relocs(static_cast<const ElfW(Rela) *>(jmprel),
                     jmprel_size / sizeof(ElfW(Rela)), o, *q);
    }
  }
  if (rela != nullptr && rela_size != 0) {
    const size_t count = rela_size / sizeof(ElfW(Rela));
    const size_t skip = std::min(rela_relative, count);
    collect_relocs(static_cast<const ElfW(Rela) *>(rela) + skip, count - skip,
                   o, *q);
  }
  if (rel != nullptr && rel_size != 0) {
    const size_t count = rel_size / sizeof(ElfW(Rel));
    const size_t skip = std::min(rel_relative, count);
    collect_relocs(static_cast<const ElfW(Rel) *>(rel) + skip, count - skip, o,
                   *q);
  }
  return 0;
}

void *lookup_symbol(void *handle, const char *name, const char *version) {
#ifdef __GLIBC__
  if (version != nullptr) {
    return dlvsym(handle, name, version);
  }
#else
  (void)version;
#endif
  return dlsym(handle, name);
}

#ifdef __GLIBC__
// A non-PIE executable that takes the address of a function it imports
// defines the symbol itself as a "canonical PLT entry": an undefined dynamic
// symbol whose value is its PLT stub. dlsym hands that stub back, but it
// jumps through the executable's GOT slot, which may be the one being
// hooked. The dynamic linker skips such symbols when it binds a slot.
bool is_canonical_plt(void *address) {
  Dl_info info{};
  void *sym = nullptr;
  return dladdr1(address, &info, &sym, RTLD_DL_SYMENT) != 0 && sym != nullptr &&
         static_cast<const ElfW(Sym) *>(sym)->st_shndx == SHN_UNDEF;
}

// The first definition in load order that is not a canonical PLT entry,
// which for the initially loaded objects is the global scope order.
void *lookup_past_plt(const char *name, const char *version) {
  void *main = dlopen(nullptr, RTLD_LAZY | RTLD_NOLOAD);
  if (main == nullptr) {
    return nullptr;
  }
  link_map *map = nullptr;
  const bool listed = dlinfo(main, RTLD_DI_LINKMAP, &map) == 0;
  dlclose(main);
  void *found = nullptr;
  for (; listed && map != nullptr && found == nullptr; map = map->l_next) {
    if (map->l_name == nullptr || map->l_name[0] == '\0') {
      continue;
    }
    void *handle = dlopen(map->l_name, RTLD_LAZY | RTLD_NOLOAD);
    if (handle == nullptr) {
      continue;
    }
    void *symbol = lookup_symbol(handle, name, version);
    dlclose(handle);
    if (symbol != nullptr && !is_canonical_plt(symbol)) {
      found = symbol;
    }
  }
  return found;
}
#endif

// Resolves a lazy slot the way the dynamic linker would: the global scope
// first, then the object's own dependencies for one dlopen'd RTLD_LOCAL.
// DT_SYMBOLIC and RTLD_DEEPBIND objects can bind differently; neither is
// handled.
void resolve_lazy(const lazy_slot &lazy, import_slot *out) {
  void *bound = lookup_symbol(RTLD_DEFAULT, lazy.name, lazy.version);
#ifdef __GLIBC__
  if (bound != nullptr && is_canonical_plt(bound)) {
    bound = lookup_past_plt(lazy.name, lazy.version);
  }
#endif
  if (bound == nullptr) {
    void *self = dlopen(lazy.object[0] != '\0' ? lazy.object : nullptr,
                        RTLD_LAZY | RTLD_NOLOAD);
    if (self != nullptr) {
      bound = lookup_symbol(self, lazy.name, lazy.version);
      dlclose(self);
    }
  }
  if (bound != nullptr && bound != *out->slot) {
    out->lazy_stub = *out->slot;
    out->bound = bound;
  }
}
#endif

#ifdef SPUD_OS_WIN
void collect_pe_module(HMODULE mod, std::string_view module,
                       std::string_view name, const void *by_value,
                       std::vector<import_slot> *out) {
  if (!module.empty() &&
      !detail::path_matches_module(detail::module_file_name(mod), module)) {
    return;
  }
  const IMAGE_NT_HEADERS *nt = detail::image_nt_headers(mod);
  if (nt == nullptr) {
    return;
  }
  auto *base = reinterpret_cast<uint8_t *>(mod);
  const auto &dir =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (dir.VirtualAddress == 0) {
    return;
  }
  auto *desc =
      reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress);
  for (; desc->Name != 0; ++desc) {
    // Without OriginalFirstThunk the loader has already replaced the names
    // in FirstThunk with addresses. Reading those as IMAGE_IMPORT_BY_NAME
    // RVAs runs off the image, so such a descriptor only matches by value.
    const bool has_names = desc->OriginalFirstThunk != 0;
    auto *thunk = reinterpret_cast<IMAGE_THUNK_DATA *>(
        base + (has_names ? desc->OriginalFirstThunk : desc->FirstThunk));
    auto *iat = reinterpret_cast<IMAGE_THUNK_DATA *>(base + desc->FirstThunk);
    for (; thunk->u1.AddressOfData != 0; ++thunk, ++iat) {
      auto **slot = reinterpret_cast<void **>(&iat->u1.Function);
      const bool value_ok = by_value != nullptr && *slot == by_value;
      if (!has_names || IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) {
        if (value_ok) {
          out->push_back({slot});
        }
        continue;
      }
      auto *ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(
          base + thunk->u1.AddressOfData);
      const bool name_ok =
          !name.empty() &&
          name_is(reinterpret_cast<const char *>(ibn->Name), name);
      if (name_ok || value_ok) {
        out->push_back({slot});
      }
    }
  }
}
#endif

#ifdef SPUD_OS_APPLE
void collect_macho_image(const mach_header_64 *header, intptr_t slide,
                         std::string_view path, std::string_view module,
                         std::string_view name, const void *by_value,
                         std::vector<import_slot> *out) {
  if (!detail::path_matches_module(path, module)) {
    return;
  }
  const uint8_t *cursor =
      reinterpret_cast<const uint8_t *>(header) + sizeof(mach_header_64);
  const nlist_64 *symtab = nullptr;
  const char *strtab = nullptr;
  const uint32_t *indirect = nullptr;
  uint32_t nsyms = 0;

  const segment_command_64 *segments[16] = {};
  uint32_t nsegs = 0;

  auto file_to_mem = [&](uint32_t off) -> const uint8_t * {
    for (uint32_t i = 0; i < nsegs; ++i) {
      const auto *seg = segments[i];
      if (off >= seg->fileoff && off < seg->fileoff + seg->filesize) {
        return reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(slide) + seg->vmaddr + (off - seg->fileoff));
      }
    }
    return nullptr;
  };

  for (uint32_t i = 0; i < header->ncmds; ++i) {
    auto *cmd = reinterpret_cast<const load_command *>(cursor);
    if (cmd->cmd == LC_SEGMENT_64) {
      auto *seg = reinterpret_cast<const segment_command_64 *>(cmd);
      if (nsegs < 16) {
        segments[nsegs++] = seg;
      }
    }
    cursor += cmd->cmdsize;
  }

  cursor = reinterpret_cast<const uint8_t *>(header) + sizeof(mach_header_64);
  for (uint32_t i = 0; i < header->ncmds; ++i) {
    auto *cmd = reinterpret_cast<const load_command *>(cursor);
    if (cmd->cmd == LC_SYMTAB) {
      auto *sc = reinterpret_cast<const symtab_command *>(cmd);
      auto *sym_mem = file_to_mem(sc->symoff);
      auto *str_mem = file_to_mem(sc->stroff);
      if (sym_mem != nullptr && str_mem != nullptr) {
        symtab = reinterpret_cast<const nlist_64 *>(sym_mem);
        strtab = reinterpret_cast<const char *>(str_mem);
        nsyms = sc->nsyms;
      }
    } else if (cmd->cmd == LC_DYSYMTAB) {
      auto *dc = reinterpret_cast<const dysymtab_command *>(cmd);
      auto *ind_mem = file_to_mem(dc->indirectsymoff);
      if (ind_mem != nullptr) {
        indirect = reinterpret_cast<const uint32_t *>(ind_mem);
      }
    }
    cursor += cmd->cmdsize;
  }
  if (symtab == nullptr || strtab == nullptr || indirect == nullptr) {
    return;
  }

  cursor = reinterpret_cast<const uint8_t *>(header) + sizeof(mach_header_64);
  for (uint32_t i = 0; i < header->ncmds; ++i) {
    auto *cmd = reinterpret_cast<const load_command *>(cursor);
    if (cmd->cmd == LC_SEGMENT_64) {
      auto *seg = reinterpret_cast<const segment_command_64 *>(cmd);
      auto *sect = reinterpret_cast<const section_64 *>(seg + 1);
      for (uint32_t s = 0; s < seg->nsects; ++s, ++sect) {
        const uint32_t type = sect->flags & SECTION_TYPE;
        if (type != S_LAZY_SYMBOL_POINTERS &&
            type != S_NON_LAZY_SYMBOL_POINTERS) {
          continue;
        }
        const uint32_t count =
            static_cast<uint32_t>(sect->size / sizeof(void *));
        auto **slots = reinterpret_cast<void **>(sect->addr + slide);
        const uint32_t *idx = indirect + sect->reserved1;
        for (uint32_t p = 0; p < count; ++p) {
          const uint32_t si = idx[p];
          if (si == INDIRECT_SYMBOL_LOCAL || si == INDIRECT_SYMBOL_ABS ||
              (si & INDIRECT_SYMBOL_LOCAL) != 0 || si >= nsyms) {
            continue;
          }
          const char *sym_name = strtab + symtab[si].n_un.n_strx;
          if (sym_name[0] == '_') {
            ++sym_name;
          }
          const bool name_ok = !name.empty() && name_is(sym_name, name);
          const bool value_ok = by_value != nullptr && slots[p] == by_value;
          if (name_ok || value_ok) {
            out->push_back({&slots[p]});
          }
        }
      }
    }
    cursor += cmd->cmdsize;
  }
}
#endif

std::vector<import_slot> collect_import_slots(std::string_view module,
                                              std::string_view name,
                                              const void *by_value) {
  std::vector<import_slot> out;
  if (name.empty() && by_value == nullptr) {
    return out;
  }
  const void *value = by_value;
#if SPUD_ARCH_X86_64
  if (value != nullptr) {
    if (void **direct = slot_from_jmp_indirect(value)) {
      out.push_back({direct});
    }
    value = follow_jmp_indirect(value);
  }
#endif
#ifdef SPUD_OS_LINUX
  import_query q{module, name, value, &out, {}};
  dl_iterate_phdr(visit_elf_object, &q);
  for (const lazy_slot &lazy : q.lazy) {
    resolve_lazy(lazy, &out[lazy.index]);
  }
#elif defined(SPUD_OS_WIN)
  for (HMODULE mod : detail::process_modules()) {
    collect_pe_module(mod, module, name, value, &out);
  }
#elif defined(SPUD_OS_APPLE)
  const uint32_t images = _dyld_image_count();
  for (uint32_t i = 0; i < images; ++i) {
    auto *header =
        reinterpret_cast<const mach_header_64 *>(_dyld_get_image_header(i));
    if (header == nullptr || header->magic != MH_MAGIC_64) {
      continue;
    }
    collect_macho_image(header, _dyld_get_image_vmaddr_slide(i),
                        _dyld_get_image_name(i) ? _dyld_get_image_name(i) : "",
                        module, name, value, &out);
  }
#endif
  // The PLT shortcut above can find a slot the walk finds again, and only
  // the walk's copy knows whether the slot is still lazy. Dropping that
  // copy would record the resolver stub as the original.
  std::sort(out.begin(), out.end(),
            [](const import_slot &a, const import_slot &b) {
              return a.slot < b.slot;
            });
  size_t kept = 0;
  for (const import_slot &found : out) {
    if (kept > 0 && out[kept - 1].slot == found.slot) {
      if (out[kept - 1].lazy_stub == nullptr) {
        out[kept - 1] = found;
      }
      continue;
    }
    out[kept++] = found;
  }
  out.resize(kept);
  return out;
}

} // namespace

std::vector<import_slot> find_import_slots(std::string_view name) {
  return find_import_slots({}, name);
}

std::vector<import_slot> find_import_slots(std::string_view module,
                                           std::string_view name) {
  return collect_import_slots(module, name, nullptr);
}

std::vector<import_slot> find_import_slots_for(const void *function) {
  if (function == nullptr) {
    return {};
  }
  std::string_view name;
#if defined(SPUD_OS_LINUX) || defined(SPUD_OS_APPLE)
  // dli_sname points into the image's string table. On Apple, dladdr has
  // already dropped the Mach-O underscore.
  Dl_info info{};
  if (dladdr(function, &info) != 0 && info.dli_sname != nullptr) {
    name = info.dli_sname;
  }
#endif
  return collect_import_slots({}, name, function);
}

} // namespace spud

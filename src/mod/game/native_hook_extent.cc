#include "game/native_hook_extent.h"

#if __APPLE__
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace
{
// LC_FUNCTION_STARTS records ULEB128 deltas from __TEXT. Require an exact
// entry and a following entry: the last function has no proven extent here.
size_t FunctionExtent(const void* method)
{
  Dl_info info{};
  if (!method || !dladdr(method, &info) || !info.dli_fbase)
    return 0;
  const auto* header = static_cast<const mach_header_64*>(info.dli_fbase);
  if (header->magic != MH_MAGIC_64)
    return 0;
  const auto*                  cursor   = reinterpret_cast<const uint8_t*>(header + 1);
  const auto*                  end      = cursor + header->sizeofcmds;
  const segment_command_64*    text     = nullptr;
  const segment_command_64*    linkedit = nullptr;
  const linkedit_data_command* starts   = nullptr;
  for (uint32_t i = 0; i < header->ncmds; ++i) {
    if (size_t(end - cursor) < sizeof(load_command))
      return 0;
    const auto* command = reinterpret_cast<const load_command*>(cursor);
    if (command->cmdsize < sizeof(load_command) || command->cmdsize > size_t(end - cursor))
      return 0;
    if (command->cmd == LC_SEGMENT_64 && command->cmdsize >= sizeof(segment_command_64)) {
      const auto* segment = reinterpret_cast<const segment_command_64*>(command);
      if (std::strncmp(segment->segname, "__TEXT", 16) == 0)
        text = segment;
      if (std::strncmp(segment->segname, "__LINKEDIT", 16) == 0)
        linkedit = segment;
    }
    if (command->cmd == LC_FUNCTION_STARTS && command->cmdsize >= sizeof(linkedit_data_command))
      starts = reinterpret_cast<const linkedit_data_command*>(command);
    cursor += command->cmdsize;
  }
  if (!text || !linkedit || !starts || starts->dataoff < linkedit->fileoff)
    return 0;
  const uint64_t offset = starts->dataoff - linkedit->fileoff;
  if (offset > linkedit->filesize || starts->datasize > linkedit->filesize - offset || offset > linkedit->vmsize
      || starts->datasize > linkedit->vmsize - offset)
    return 0;
  const uintptr_t base   = reinterpret_cast<uintptr_t>(header);
  const uintptr_t target = reinterpret_cast<uintptr_t>(method);
  if (target < base || target - base >= text->vmsize)
    return 0;
  const auto slide = base - text->vmaddr;
  cursor           = reinterpret_cast<const uint8_t*>(slide + linkedit->vmaddr + offset);
  end              = cursor + starts->datasize;
  uint64_t address = 0;
  bool     found   = false;
  while (cursor < end) {
    uint64_t delta = 0;
    unsigned shift = 0;
    uint8_t  byte;
    do {
      if (cursor == end || shift >= 64)
        return 0;
      byte = *cursor++;
      if (shift == 63 && (byte & 0x7e))
        return 0;
      delta |= uint64_t(byte & 0x7f) << shift;
      shift += 7;
    } while (byte & 0x80);
    if (!delta || delta > text->vmsize - address)
      return 0;
    address += delta;
    if (found)
      return address - (target - base);
    if (address > target - base)
      return 0;
    found = address == target - base;
  }
  return 0;
}

} // namespace
#endif

bool native_hooks::MacHookFits(const void* method, std::size_t minimum_extent)
{
#if __APPLE__
  const auto extent = FunctionExtent(method);
  bool       fits   = false;
  if (extent >= std::max(minimum_extent, std::size_t{32})) {
    const auto plan = spud::inspect_detour(method, spud::Arch::kHost, extent);
    fits = plan.ok() && plan.resolved_target == reinterpret_cast<uintptr_t>(method) && plan.far_jump_size != 0
           && plan.far_required_patch_bytes >= plan.far_jump_size && plan.far_required_patch_bytes <= extent;
  }
  spdlog::info("[MacHookExtent] entry={} bytes={} accepted={}", method, extent, fits);
  return fits;
#else
  (void)method;
  (void)minimum_extent;
  return false;
#endif
}

#if defined(__APPLE__) && defined(__aarch64__)
const void* native_hooks::MacFollowDirectBranch(const void* method)
{
  if (!method) {
    return nullptr;
  }
  const auto     entry       = reinterpret_cast<uintptr_t>(method);
  const uint32_t instruction = *reinterpret_cast<const uint32_t*>(entry);
  if ((instruction & 0xFC000000u) != 0x14000000u) {
    return nullptr;
  }
  const uint32_t imm64    = static_cast<uint32_t>(instruction & 0x03FFFFFFu);
  const int32_t  imm26    = static_cast<int32_t>(imm64 | (instruction & 0x02000000u ? 0xFC000000u : 0u));
  const intptr_t offset   = static_cast<intptr_t>(imm26) * 4;
  const auto     resolved = entry + offset;
  Dl_info        info{};
  if (!dladdr(reinterpret_cast<const void*>(resolved), &info) || !info.dli_fbase
      || reinterpret_cast<uintptr_t>(info.dli_fbase) == resolved) {
    return nullptr;
  }
  return reinterpret_cast<const void*>(resolved);
}
#endif

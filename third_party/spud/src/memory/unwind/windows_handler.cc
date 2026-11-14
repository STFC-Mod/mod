// Windows language handlers: recognizes the Microsoft runtime's handlers
// and walks the scope tables they dispatch from (C scopes, FH3, compressed
// FH4), proving no handler path re-enters the overwritten bytes.
#include "memory/unwind/windows.h"

#include "memory/readable.h"

#include <spud/arch.h>
#include <spud/diagnostics.h>

#if SPUD_UNWIND_SUPPORT && SPUD_OS_WIN
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include <Zydis/Zydis.h>
#include <excpt.h>
#include <windows.h>

#if defined(_MSC_VER)
#define SPUD_OPTIONAL_HANDLER
#else
#define SPUD_OPTIONAL_HANDLER __attribute__((weak))
#endif
extern "C" {
EXCEPTION_DISPOSITION __cdecl
__CxxFrameHandler3(EXCEPTION_RECORD *, void *, CONTEXT *,
                   DISPATCHER_CONTEXT *) SPUD_OPTIONAL_HANDLER;
EXCEPTION_DISPOSITION __cdecl
__CxxFrameHandler4(EXCEPTION_RECORD *, void *, CONTEXT *,
                   DISPATCHER_CONTEXT *) SPUD_OPTIONAL_HANDLER;
EXCEPTION_DISPOSITION __cdecl
__GSHandlerCheck_EH(EXCEPTION_RECORD *, void *, CONTEXT *,
                    DISPATCHER_CONTEXT *) SPUD_OPTIONAL_HANDLER;
EXCEPTION_DISPOSITION __cdecl
__GSHandlerCheck_EH4(EXCEPTION_RECORD *, void *, CONTEXT *,
                     DISPATCHER_CONTEXT *) SPUD_OPTIONAL_HANDLER;
EXCEPTION_DISPOSITION __cdecl
__GSHandlerCheck_SEH(EXCEPTION_RECORD *, void *, CONTEXT *,
                     DISPATCHER_CONTEXT *) SPUD_OPTIONAL_HANDLER;
EXCEPTION_DISPOSITION __cdecl
__GSHandlerCheck(EXCEPTION_RECORD *, void *, CONTEXT *,
                 DISPATCHER_CONTEXT *) SPUD_OPTIONAL_HANDLER;
}
#undef SPUD_OPTIONAL_HANDLER

namespace spud::detail::xdata {
namespace {

// Language handlers can be reached through import thunks and jump aliases.
uintptr_t follow_thunks(uintptr_t target, snapshot_list &snapshots) {
  for (unsigned depth = 0; depth != 4; ++depth) {
    std::array<uint8_t, 6> bytes{};
    if (!read_code_bytes(target, bytes))
      return 0;
    int32_t displacement;
    if (bytes[0] == 0xF3 && bytes[1] == 0x0F && bytes[2] == 0x1E &&
        bytes[3] == 0xFA) {
      snapshots.emplace_back(
          target, std::vector<uint8_t>(bytes.begin(), bytes.begin() + 4));
      target += 4;
    } else if (bytes[0] == 0xE9) {
      std::memcpy(&displacement, bytes.data() + 1, 4);
      snapshots.emplace_back(
          target, std::vector<uint8_t>(bytes.begin(), bytes.begin() + 5));
      target += 5 + displacement;
    } else if (bytes[0] == 0xFF && bytes[1] == 0x25) {
      std::memcpy(&displacement, bytes.data() + 2, 4);
      const uintptr_t slot = target + 6 + displacement;
      std::vector<uint8_t> pointer;
      if (!read_snapshot(slot, sizeof(target), pointer, snapshots))
        return 0;
      snapshots.emplace_back(target,
                             std::vector<uint8_t>(bytes.begin(), bytes.end()));
      std::memcpy(&target, pointer.data(), sizeof(target));
    } else {
      return target;
    }
  }
  return 0;
}

// Code that landing paths run through, read once per preparation: the
// source function, then any unwind entry a path branches into, such as a
// separated cold part or a funclet. Each fragment keeps the offsets already
// decoded; decoded bytes are snapshotted as runs when preparation ends.
struct landing_paths {
  struct fragment {
    uintptr_t begin;
    size_t length;
    std::vector<uint8_t> code;
    std::vector<bool> visited;
    std::vector<bool> decoded;
  };
  // A tail call into another function walks that function too, up to these
  // bounds. Real functions have a handful of funclets and cold parts.
  static constexpr size_t kMaxFragments = 64;
  static constexpr size_t kMaxFragmentBytes = size_t{1} << 20;

  // The source function's extent, including chained parts after it.
  uintptr_t function_begin;
  uintptr_t function_end;
  uintptr_t source;
  size_t patch;
  std::vector<fragment> fragments;

  landing_paths(uintptr_t begin, uintptr_t end, uintptr_t source_address,
                size_t patch_size)
      : function_begin(begin), function_end(end), source(source_address),
        patch(patch_size) {
    // Funclet walks hold a fragment across nested walks.
    fragments.reserve(kMaxFragments);
    fragments.push_back({begin, end > begin ? end - begin : 0, {}, {}, {}});
  }

  bool in_patch(uintptr_t pc) const {
    return pc >= source && pc - source < patch;
  }

  // The fragment starting at `begin`, read on first use. false refuses an
  // oversized fragment or one past the bound.
  bool load(uintptr_t begin, size_t length, fragment *&found) {
    found = nullptr;
    for (auto &existing : fragments)
      if (existing.begin == begin)
        found = &existing;
    if (found == nullptr) {
      if (fragments.size() == kMaxFragments)
        return false;
      fragments.push_back({begin, length, {}, {}, {}});
      found = &fragments.back();
    }
    if (found->length > kMaxFragmentBytes)
      return false;
    if (found->visited.empty()) {
      found->code.resize(found->length);
      found->code.resize(read_code_prefix(found->begin, found->code));
      found->visited.assign(found->length, false);
      found->decoded.assign(found->length, false);
    }
    return true;
  }

  // The fragment covering `pc`, or nullptr when no unwind entry does.
  bool at(uintptr_t pc, fragment *&found) {
    found = nullptr;
    for (auto &existing : fragments)
      if (pc >= existing.begin && pc - existing.begin < existing.length)
        return load(existing.begin, existing.length, found);
    DWORD64 base = 0;
    const auto *function = RtlLookupFunctionEntry(pc, &base, nullptr);
    if (function == nullptr || function->EndAddress <= function->BeginAddress)
      return true;
    return load(base + function->BeginAddress,
                function->EndAddress - function->BeginAddress, found);
  }

  // Decodes the instruction at `pc` in `f` and marks its bytes for the
  // snapshot. Bytes past a short read fail the decode.
  bool decode(fragment &f, uintptr_t pc, ZydisDecoder &decoder,
              ZydisDecodedInstruction &instruction,
              ZydisDecodedOperand *operands = nullptr) {
    const size_t offset = pc - f.begin;
    const size_t available = offset < f.code.size()
                                 ? std::min<size_t>(f.code.size() - offset, 15)
                                 : 0;
    const auto *bytes = f.code.data() + offset;
    const bool decoded =
        operands != nullptr
            ? ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, bytes, available,
                                                  &instruction, operands))
            : ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(
                  &decoder, nullptr, bytes, available, &instruction));
    if (!decoded)
      return false;
    std::fill_n(f.decoded.begin() + offset, instruction.length, true);
    return true;
  }

  // Exception entries must not re-enter bytes that the installed stub
  // overwrites. Only the source function must prove every transfer; in
  // other entries a register jump is a tail call or a dispatch this check
  // used to trust outright, so it ends the path there. Code that no unwind
  // entry describes is trusted.
  bool walk(uintptr_t landing) {
    std::vector<uintptr_t> pending{landing};
    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                     ZYDIS_STACK_WIDTH_64);
    while (!pending.empty()) {
      const auto pc = pending.back();
      pending.pop_back();
      if (in_patch(pc))
        return note_failure("handler_path_enters_patch", pc);
      fragment *f = nullptr;
      if (!at(pc, f))
        return note_failure("handler_path_too_long", pc);
      if (f == nullptr)
        continue;
      const bool own = f == &fragments.front();
      const size_t offset = pc - f->begin;
      if (f->visited[offset])
        continue;
      f->visited[offset] = true;
      ZydisDecodedInstruction instruction;
      if (!decode(*f, pc, decoder, instruction))
        return note_failure("handler_path_undecodable", pc);
      const auto category = instruction.meta.category;
      if (category == ZYDIS_CATEGORY_COND_BR ||
          category == ZYDIS_CATEGORY_UNCOND_BR) {
        if (instruction.raw.imm[0].is_relative)
          pending.push_back(pc + instruction.length +
                            instruction.raw.imm[0].value.s);
        else if (own || category == ZYDIS_CATEGORY_COND_BR)
          return note_failure("handler_path_indirect", pc);
      }
      if (category != ZYDIS_CATEGORY_UNCOND_BR &&
          category != ZYDIS_CATEGORY_RET &&
          instruction.length < f->length - offset)
        pending.push_back(pc + instruction.length);
    }
    return true;
  }

  // Runs of decoded bytes. A gap of up to 256 bytes joins its neighbours,
  // which costs less at commit than another read, unless it holds patched
  // bytes.
  void snapshot(snapshot_list &snapshots) const {
    for (const auto &f : fragments) {
      size_t run = 0, end = 0;
      bool open = false;
      auto flush = [&] {
        if (open)
          snapshots.emplace_back(
              f.begin + run,
              std::vector<uint8_t>(f.code.begin() + run, f.code.begin() + end));
        open = false;
      };
      for (size_t at = 0; at < f.decoded.size(); ++at) {
        if (!f.decoded[at])
          continue;
        if (open && (at - end > 256 ||
                     (f.begin + end < source + patch && f.begin + at > source)))
          flush();
        if (!open) {
          run = at;
          open = true;
        }
        end = at + 1;
      }
      flush();
    }
  }
};
enum class continuation_kind { kNone, kAddress, kIndex };

bool check_funclet(uintptr_t landing, landing_paths &paths,
                   continuation_kind continuation, snapshot_list &snapshots) {
  DWORD64 base = 0;
  const auto *function = RtlLookupFunctionEntry(landing, &base, nullptr);
  // Small destructor methods can be native leaves with no frame record.
  // Their direct paths still have to terminate within the guarded window.
  size_t length = 16384;
  if (function != nullptr) {
    std::vector<uint8_t> descriptor;
    if (!read_snapshot(reinterpret_cast<uintptr_t>(function), sizeof(*function),
                       descriptor, snapshots))
      return false;
    RUNTIME_FUNCTION native;
    std::memcpy(&native, descriptor.data(), sizeof(native));
    if (base + native.BeginAddress != landing ||
        native.EndAddress <= native.BeginAddress)
      return false;
    length = native.EndAddress - native.BeginAddress;
  }
  landing_paths::fragment *fragment = nullptr;
  if (!paths.load(landing, length, fragment) || fragment->code.empty())
    return note_failure("handler_path_too_long", landing);
  const uintptr_t fragment_end =
      landing + (function != nullptr ? length : fragment->code.size());
  struct state {
    uintptr_t pc;
    std::optional<uintptr_t> continuation;
    bool operator==(const state &) const = default;
  };
  std::vector<state> pending{{landing, {}}}, visited;
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  while (!pending.empty()) {
    auto current = pending.back();
    pending.pop_back();
    if (paths.in_patch(current.pc))
      return note_failure("handler_path_enters_patch", current.pc);
    if (current.pc < landing || current.pc >= fragment_end) {
      if (continuation != continuation_kind::kNone || !paths.walk(current.pc))
        return false;
      continue;
    }
    if (std::find(visited.begin(), visited.end(), current) != visited.end())
      continue;
    if (visited.size() == 16384)
      return note_failure("handler_path_too_long", landing);
    visited.push_back(current);
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    if (!paths.decode(*fragment, current.pc, decoder, instruction, operands))
      return note_failure("handler_path_undecodable", current.pc);
    const auto next = current.pc + instruction.length;
    for (unsigned i = 0; i < instruction.operand_count; ++i) {
      if (operands[i].type == ZYDIS_OPERAND_TYPE_REGISTER &&
          (operands[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0 &&
          ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,
                                           operands[i].reg.value) ==
              ZYDIS_REGISTER_RAX)
        current.continuation.reset();
    }
    const auto category = instruction.meta.category;
    if (category == ZYDIS_CATEGORY_CALL)
      current.continuation.reset();
    if (instruction.mnemonic == ZYDIS_MNEMONIC_LEA &&
        operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
        operands[0].reg.value == ZYDIS_REGISTER_RAX &&
        operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
        operands[1].mem.base == ZYDIS_REGISTER_RIP)
      current.continuation = next + operands[1].mem.disp.value;
    if (category == ZYDIS_CATEGORY_RET) {
      // Catch funclets return the address at which their parent resumes.
      // The original funclet stays in place, so that address must stay safe.
      if (continuation == continuation_kind::kAddress &&
          (!current.continuation ||
           *current.continuation < paths.function_begin ||
           *current.continuation >= paths.function_end ||
           !paths.walk(*current.continuation)))
        return note_failure("handler_continuation_unproven", current.pc);
      continue;
    }
    if (category == ZYDIS_CATEGORY_COND_BR ||
        category == ZYDIS_CATEGORY_UNCOND_BR) {
      if (!instruction.raw.imm[0].is_relative)
        return note_failure("handler_path_indirect", current.pc);
      pending.push_back(
          {next + instruction.raw.imm[0].value.s, current.continuation});
    }
    if (category != ZYDIS_CATEGORY_UNCOND_BR) {
      if (next == fragment_end)
        return note_failure("handler_path_falls_off", current.pc);
      pending.push_back({next, current.continuation});
    }
  }
  return true;
}

// __CxxFrameHandler3's FuncInfo: the unwind map's cleanup actions, the IP
// to state map, and each try block's catch handlers.
bool check_fh3(const handler_source &frame, landing_paths &paths,
               snapshot_list &snapshots) {
  std::vector<uint8_t> bytes;
  if (!read_snapshot(reinterpret_cast<uintptr_t>(frame.handler_data), 4, bytes,
                     snapshots))
    return false;
  uint32_t rva;
  std::memcpy(&rva, bytes.data(), 4);
  if (!read_snapshot(frame.base + rva, 40, bytes, snapshots))
    return false;
  std::array<uint32_t, 10> info;
  std::memcpy(info.data(), bytes.data(), sizeof(info));
  if (info[0] != 0x19930522 || info[1] > 1024 || info[3] > 1024 ||
      info[5] > 1024 || info[8] != 0 || (info[9] & ~5u) != 0)
    return false;
  const auto state_valid = [&](int32_t state) {
    return state >= -1 && (state == -1 || uint32_t(state) < info[1]);
  };
  if (!read_snapshot(frame.base + info[2], size_t(info[1]) * 8, bytes,
                     snapshots))
    return false;
  for (size_t i = 0; i < info[1]; ++i) {
    int32_t previous;
    uint32_t handler;
    std::memcpy(&previous, bytes.data() + i * 8, 4);
    std::memcpy(&handler, bytes.data() + i * 8 + 4, 4);
    if (previous < -1 || previous >= int64_t(i) ||
        (handler != 0 && !check_funclet(frame.base + handler, paths,
                                        continuation_kind::kNone, snapshots)))
      return false;
  }
  if (!read_snapshot(frame.base + info[6], size_t(info[5]) * 8, bytes,
                     snapshots))
    return false;
  uint32_t previous_ip = 0;
  for (size_t i = 0; i < info[5]; ++i) {
    uint32_t ip;
    int32_t state;
    std::memcpy(&ip, bytes.data() + i * 8, 4);
    std::memcpy(&state, bytes.data() + i * 8 + 4, 4);
    if ((i != 0 && ip <= previous_ip) || !state_valid(state))
      return false;
    previous_ip = ip;
  }
  if (!read_snapshot(frame.base + info[4], size_t(info[3]) * 20, bytes,
                     snapshots))
    return false;
  size_t handlers = 0;
  for (size_t i = 0; i < info[3]; ++i) {
    std::array<int32_t, 5> scope;
    std::memcpy(scope.data(), bytes.data() + i * 20, 20);
    if (!state_valid(scope[0]) || !state_valid(scope[1]) ||
        !state_valid(scope[2]) || scope[0] < 0 || scope[0] > scope[1] ||
        scope[1] >= scope[2] || scope[3] <= 0 ||
        uint32_t(scope[3]) > 1024 - handlers)
      return false;
    handlers += scope[3];
    std::vector<uint8_t> catches;
    if (!read_snapshot(frame.base + uint32_t(scope[4]), size_t(scope[3]) * 20,
                       catches, snapshots))
      return false;
    for (size_t j = 0; j < size_t(scope[3]); ++j) {
      std::array<uint32_t, 5> handler;
      std::memcpy(handler.data(), catches.data() + j * 20, 20);
      if (handler[3] == 0 ||
          !check_funclet(frame.base + handler[3], paths,
                         continuation_kind::kAddress, snapshots))
        return false;
    }
  }
  return true;
}

// FH4 uses length-tagged integers rather than LEB128. Capture only consumed
// metadata even though the guarded read supplies a bounded decoding window.
struct fh4_reader {
  uintptr_t address;
  std::vector<uint8_t> bytes;
  size_t at = 0;
  bool valid = true;

  explicit fh4_reader(uintptr_t begin) : address(begin) {}
  uint32_t fixed(size_t width) {
    if (!valid || width > 4) {
      valid = false;
      return 0;
    }
    if (width > bytes.size() - at) {
      const auto previous = bytes.size();
      bytes.resize(std::min(size_t{65536}, previous + 256));
      const auto copied = read_code_prefix(address + previous,
                                           std::span(bytes).subspan(previous));
      bytes.resize(previous + copied);
      if (width > bytes.size() - at) {
        valid = false;
        return 0;
      }
    }
    uint32_t value = 0;
    std::memcpy(&value, bytes.data() + at, width);
    at += width;
    return value;
  }
  uint32_t packed() {
    const auto first = fixed(1);
    unsigned width = 1;
    while (width < 5 && (first & (1u << (width - 1))) != 0)
      ++width;
    if (width == 5)
      return fixed(4);
    uint32_t value = first;
    if (width != 1)
      value |= fixed(width - 1) << 8;
    return value >> width;
  }
  bool finish(snapshot_list &snapshots) {
    if (!valid)
      return false;
    bytes.resize(at);
    snapshots.emplace_back(address, std::move(bytes));
    return true;
  }
};

// __CxxFrameHandler4's compressed FuncInfo: the same maps as FH3, packed,
// with optional IP-map segments for separated code.
bool check_fh4(const handler_source &frame, landing_paths &paths,
               snapshot_list &snapshots) {
  std::vector<uint8_t> pointer;
  if (!read_snapshot(reinterpret_cast<uintptr_t>(frame.handler_data), 4,
                     pointer, snapshots))
    return false;
  uint32_t rva;
  std::memcpy(&rva, pointer.data(), 4);
  fh4_reader info(frame.base + rva);
  const auto header = info.fixed(1);
  if ((header & 0x80) != 0)
    return false;
  if ((header & 4) != 0)
    info.packed(); // Basic-block transformation flags.
  const auto unwind_map = (header & 8) != 0 ? info.fixed(4) : 0;
  const auto try_map = (header & 16) != 0 ? info.fixed(4) : 0;
  const auto ip_map = info.fixed(4);
  if ((header & 1) != 0)
    info.packed(); // Parent-frame displacement for a catch funclet.
  if (!info.finish(snapshots))
    return false;
  const auto begin = frame.base + frame.original.BeginAddress;
  uint32_t states = 0;
  if (unwind_map != 0) {
    fh4_reader unwind(frame.base + unwind_map);
    states = unwind.packed();
    if (states > 1024)
      return false;
    const auto first = unwind.at;
    std::vector<size_t> entries;
    for (uint32_t i = 0; i < states; ++i) {
      const auto at = unwind.at;
      const auto operation = unwind.packed();
      const auto distance = operation >> 2;
      if (distance == 0 ||
          (distance <= at - first && std::find(entries.begin(), entries.end(),
                                               at - distance) == entries.end()))
        return false;
      entries.push_back(at);
      const auto kind = operation & 3;
      if (kind != 0) {
        const auto action = unwind.fixed(4);
        if (kind == 1 || kind == 2)
          unwind.packed(); // Direct object or pointer-to-object displacement.
        if (action == 0 || !check_funclet(frame.base + action, paths,
                                          continuation_kind::kNone, snapshots))
          return false;
      }
    }
    if (!unwind.finish(snapshots))
      return false;
  }
  auto check_ip_map = [&](uint32_t map, uint32_t start) {
    if (map == 0)
      return true;
    fh4_reader ips(frame.base + map);
    const auto count = ips.packed();
    if (count > 1024)
      return false;
    uint64_t ip = start;
    for (uint32_t i = 0; i < count; ++i) {
      const auto delta = ips.packed();
      ip += delta;
      const auto state = ips.packed(); // Native state + 1.
      if (ip > UINT32_MAX || (i != 0 && delta == 0) || state > states)
        return false;
    }
    return ips.finish(snapshots);
  };
  if ((header & 2) != 0) {
    if (ip_map == 0)
      return false;
    fh4_reader segments(frame.base + ip_map);
    const auto count = segments.packed();
    if (count > 1024)
      return false;
    uint32_t previous = 0;
    for (uint32_t i = 0; i < count; ++i) {
      const auto start = segments.fixed(4), map = segments.fixed(4);
      if ((i != 0 && start <= previous) || !check_ip_map(map, start))
        return false;
      previous = start;
    }
    if (!segments.finish(snapshots))
      return false;
  } else if (!check_ip_map(ip_map, frame.original.BeginAddress)) {
    return false;
  }
  if (try_map == 0)
    return true;
  fh4_reader tries(frame.base + try_map);
  const auto count = tries.packed();
  if (count > 1024)
    return false;
  size_t total_handlers = 0;
  for (uint32_t i = 0; i < count; ++i) {
    const auto low = tries.packed(), high = tries.packed();
    const auto catch_high = tries.packed(), map = tries.fixed(4);
    if (low > high || high >= catch_high || catch_high >= states || map == 0)
      return false;
    fh4_reader catches(frame.base + map);
    const auto handlers = catches.packed();
    if (handlers == 0 || handlers > 1024 - total_handlers)
      return false;
    total_handlers += handlers;
    for (uint32_t j = 0; j < handlers; ++j) {
      const auto flags = catches.fixed(1);
      const auto continuations = (flags >> 4) & 3;
      if ((flags & 0xC0) != 0 || continuations == 3)
        return false;
      if ((flags & 1) != 0)
        catches.packed(); // Catch qualifiers.
      if ((flags & 2) != 0)
        catches.fixed(4); // Type descriptor RVA, retained in the source image.
      if ((flags & 4) != 0)
        catches.packed(); // Catch object displacement.
      const auto handler = catches.fixed(4);
      for (unsigned k = 0; k < continuations; ++k) {
        const auto continuation = (flags & 8) != 0
                                      ? frame.base + catches.fixed(4)
                                      : begin + catches.packed();
        if (continuation < begin || continuation >= paths.function_end ||
            !paths.walk(continuation))
          return false;
      }
      if (handler == 0 ||
          !check_funclet(frame.base + handler, paths,
                         continuations != 0 ? continuation_kind::kIndex
                                            : continuation_kind::kAddress,
                         snapshots))
        return false;
    }
    if (!catches.finish(snapshots))
      return false;
  }
  return tries.finish(snapshots);
}

// Whether `handler` is `name` as exported by a runtime that ships
// Microsoft's language handlers. A module can use another runtime's copy
// than this library links.
bool runtime_export(uintptr_t handler, const char *name,
                    snapshot_list &snapshots) {
  constexpr const wchar_t *runtimes[] = {
      L"ntdll.dll",        L"msvcrt.dll",          L"vcruntime140_1.dll",
      L"vcruntime140.dll", L"vcruntime140_1d.dll", L"vcruntime140d.dll",
      L"msvcr120.dll",     L"msvcr110.dll",        L"msvcr100.dll",
      L"msvcr90.dll",      L"msvcr80.dll"};
  for (const auto *runtime : runtimes) {
    const auto module = GetModuleHandleW(runtime);
    if (module == nullptr)
      continue;
    const auto target =
        reinterpret_cast<uintptr_t>(GetProcAddress(module, name));
    if (target != 0 && handler == follow_thunks(target, snapshots))
      return true;
  }
  return false;
}

// What a handler's data holds. The GS variants put a cookie record after
// the scope table, or hold only the cookie record.
enum class handler_kind {
  kUnknown,
  kGsCookie,
  kCScope,
  kGsCScope,
  kFh3,
  kGsFh3,
  kFh4,
  kGsFh4,
};

// Compares the resolved handler against the copies this library links and
// against runtime DLL exports. A handler statically linked into another
// image matches neither and stays unknown.
handler_kind classify(uintptr_t handler, snapshot_list &snapshots) {
  const auto is = [&](auto *linked) {
    return linked != nullptr &&
           handler ==
               follow_thunks(reinterpret_cast<uintptr_t>(linked), snapshots);
  };
  if (is(&__GSHandlerCheck))
    return handler_kind::kGsCookie;
  if (is(&__C_specific_handler) ||
      runtime_export(handler, "__C_specific_handler", snapshots))
    return handler_kind::kCScope;
  if (is(&__GSHandlerCheck_SEH))
    return handler_kind::kGsCScope;
  if (is(&__CxxFrameHandler3) ||
      runtime_export(handler, "__CxxFrameHandler3", snapshots))
    return handler_kind::kFh3;
  if (is(&__CxxFrameHandler4) ||
      runtime_export(handler, "__CxxFrameHandler4", snapshots))
    return handler_kind::kFh4;
  if (is(&__GSHandlerCheck_EH))
    return handler_kind::kGsFh3;
  if (is(&__GSHandlerCheck_EH4))
    return handler_kind::kGsFh4;
  return handler_kind::kUnknown;
}

// __C_specific_handler's scope table: a count, then begin, end, handler or
// filter, and target RVAs. Filters and __except targets must not reach the
// patch.
bool check_c_scopes(const handler_source &frame, landing_paths &paths,
                    snapshot_list &snapshots, size_t &length) {
  std::vector<uint8_t> scopes;
  const auto data = reinterpret_cast<uintptr_t>(frame.handler_data);
  if (!read_snapshot(data, 4, scopes, snapshots))
    return false;
  uint32_t count;
  std::memcpy(&count, scopes.data(), 4);
  if (count > 1024 || !read_snapshot(data, 4 + count * 16, scopes, snapshots))
    return false;
  for (size_t i = 0; i < count; ++i) {
    std::array<uint32_t, 4> scope;
    std::memcpy(scope.data(), scopes.data() + 4 + i * 16, 16);
    if (scope[0] >= scope[1])
      return false;
    // 0 and 1 are EXCEPTION_CONTINUE_SEARCH and EXECUTE_HANDLER constants.
    for (size_t j : {size_t{2}, size_t{3}}) {
      if (scope[j] > 1 && !paths.walk(frame.base + scope[j]))
        return false;
    }
  }
  length = 4 + count * 16;
  return true;
}

} // namespace

handler_check check_handler(const handler_source &frame, uintptr_t source_end,
                            size_t patch, snapshot_list &snapshots) {
  landing_paths paths(frame.base + frame.original.BeginAddress, source_end,
                      frame.source, patch);
  const auto data = reinterpret_cast<uintptr_t>(frame.handler_data);
  const auto handler =
      follow_thunks(reinterpret_cast<uintptr_t>(frame.handler), snapshots);
  if (handler == 0) {
    note_failure("unreadable_language_handler",
                 reinterpret_cast<uintptr_t>(frame.handler));
    return handler_check::kRefused;
  }
  const auto refuse = [&] {
    // Path walks record their own, more specific reason first.
    note_failure("unsupported_handler_table", data);
    return handler_check::kRefused;
  };
  auto snapshot_gs = [&](uintptr_t address) {
    std::vector<uint8_t> bytes;
    if (!read_snapshot(address, 4, bytes, snapshots))
      return false;
    // Bit 2 adds the aligned frame displacement and alignment mask.
    return (bytes[0] & 4) == 0 || read_snapshot(address, 12, bytes, snapshots);
  };
  const auto kind = classify(handler, snapshots);
  const bool gs = kind == handler_kind::kGsCScope ||
                  kind == handler_kind::kGsFh3 || kind == handler_kind::kGsFh4;
  size_t scopes = 0;
  switch (kind) {
  case handler_kind::kUnknown:
    return handler_check::kUnknown;
  case handler_kind::kGsCookie:
    if (!snapshot_gs(data))
      return refuse();
    break;
  case handler_kind::kCScope:
  case handler_kind::kGsCScope:
    // The GS variant puts the cookie record after the scope table.
    if (!check_c_scopes(frame, paths, snapshots, scopes) ||
        (gs && !snapshot_gs(data + scopes)))
      return refuse();
    break;
  case handler_kind::kFh3:
  case handler_kind::kGsFh3:
  case handler_kind::kFh4:
  case handler_kind::kGsFh4: {
    const bool fh3 = kind == handler_kind::kFh3 || kind == handler_kind::kGsFh3;
    // The GS variants put the cookie record after the FuncInfo RVA.
    if (!(fh3 ? check_fh3(frame, paths, snapshots)
              : check_fh4(frame, paths, snapshots)) ||
        (gs && !snapshot_gs(data + 4)))
      return refuse();
    break;
  }
  }
  paths.snapshot(snapshots);
  return handler_check::kSafe;
}

} // namespace spud::detail::xdata
#endif

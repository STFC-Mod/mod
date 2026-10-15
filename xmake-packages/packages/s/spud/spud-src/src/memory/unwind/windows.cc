// Windows x64: translates the source's unwind codes for copied code,
// registers the generated function table, and retains source modules.
// Language-handler checks live in windows_handler.cc.
#include "memory/unwind.h"

#include <spud/arch.h>

#if SPUD_OS_WIN
#include "detour/control_flow.h"
#include "detour/detour_impl.h"
#include "memory/readable.h"
#include "memory/safe_patch.h"
#include "memory/unwind/windows.h"

#include <spud/diagnostics.h>
#include <spud/utils.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Zydis/Zydis.h>
#include <windows.h>

namespace spud::detail {

bool unwind_registration::retain_source_module(uintptr_t source,
                                               void *&module) {
  module = nullptr;
  if (source == 0)
    return true;
  HMODULE image = nullptr;
  if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                         reinterpret_cast<LPCWSTR>(source), &image)) {
    module = image;
    return true;
  }
  // Code outside any image (another JIT) has nothing to retain; an image
  // that cannot be pinned could unload under the hook.
  MEMORY_BASIC_INFORMATION region{};
  return VirtualQuery(reinterpret_cast<void *>(source), &region,
                      sizeof(region)) != 0 &&
         region.Type != MEM_IMAGE;
}

void unwind_registration::close_source_module(void *module) {
  FreeLibrary(static_cast<HMODULE>(module));
}

} // namespace spud::detail

#if SPUD_UNWIND_SUPPORT
namespace spud::detail::xdata {

bool read_snapshot(uintptr_t address, size_t size, std::vector<uint8_t> &bytes,
                   snapshot_list &snapshots) {
  if (size > 65536 || address > UINTPTR_MAX - size)
    return false;
  for (const auto &[begin, existing] : snapshots) {
    if (begin == address && existing.size() >= size) {
      bytes.assign(existing.begin(), existing.begin() + size);
      return true;
    }
  }
  bytes.resize(size);
  if (!read_code_bytes(address, bytes))
    return false;
  snapshots.emplace_back(address, bytes);
  return true;
}

namespace {

// Where one copied instruction landed in the trampoline, relative to its
// entry, and its source interval. `call_return` is the generated return
// PC of an expanded call; `fixed` marks a relay veneer.
struct copied_instruction {
  size_t begin;
  size_t end;
  size_t source_begin;
  size_t source_end;
  size_t call_return;
  bool fixed = false;
};

// The function table registered for one allocation: RUNTIME_FUNCTIONs and
// their UNWIND_INFO, written into the prefix reserved before the
// allocation, plus what the handler thunk needs to dispatch to the source
// function's language handler.
struct function_table {
  uintptr_t allocation = 0;
  // Bytes reserved before the allocation for `bytes`.
  size_t prefix = kWindowsUnwindBytes;
  uintptr_t entry = 0;
  // The source's handler; null when the trampoline dispatches none.
  handler_source source;
  std::vector<copied_instruction> instructions;
  std::vector<RUNTIME_FUNCTION> functions;
  std::vector<uint8_t> bytes;
  bool published = false;
  ~function_table() {
    if (published)
      RtlDeleteFunctionTable(functions.data());
  }
};

// Every generated record with a handler names this thunk, through the stub
// at the table's base. It maps ControlPc back into the source function and
// calls the source's handler as if the exception were there.
EXCEPTION_DISPOSITION NTAPI
forward_to_source_handler(EXCEPTION_RECORD *exception, void *frame,
                          CONTEXT *context, DISPATCHER_CONTEXT *dispatcher) {
  function_table *binding;
  std::memcpy(&binding, dispatcher->HandlerData, sizeof(binding));
  DISPATCHER_CONTEXT original = *dispatcher;
  const auto offset = dispatcher->ControlPc - binding->entry;
  size_t source_offset = 0;
  for (const auto &instruction : binding->instructions) {
    if (offset < instruction.begin)
      break;
    // An expanded call returns inside its rewrite, before the literal skip.
    // Scope tables use the original instruction's return PC.
    source_offset = instruction.source_end;
    if (offset < instruction.end) {
      if (instruction.fixed) {
        source_offset = instruction.source_begin;
      } else {
        source_offset =
            instruction.call_return != 0 && offset >= instruction.call_return
                ? instruction.source_end
                : instruction.source_begin +
                      std::min(offset - instruction.begin,
                               instruction.source_end -
                                   instruction.source_begin - 1);
      }
      break;
    }
  }
  auto &source = binding->source;
  original.ControlPc = source.source + source_offset;
  original.ImageBase = source.base;
  original.FunctionEntry = &source.original;
  original.LanguageHandler = source.handler;
  original.HandlerData = source.handler_data;
  const auto disposition = source.handler(exception, frame, context, &original);
  dispatcher->ScopeIndex = original.ScopeIndex;
  return disposition;
}

// One native unwind code, re-based to a source offset. `frame_pointer`
// marks UWOP_SET_FPREG.
struct unwind_code {
  size_t offset;
  std::vector<uint8_t> codes;
  bool frame_pointer;
};

// The native unwind state at one source PC: the codes in effect there and
// whether it is inside the prologue or a recognised epilogue.
struct source_state {
  uintptr_t base = 0;
  RUNTIME_FUNCTION original{};
  uintptr_t end = 0;
  uint8_t flags = 0;
  uint8_t frame_register = 0;
  // End of the prologue of the entry covering the PC.
  uintptr_t prologue_end = 0;
  bool prologue = false;
  bool epilogue = false;
  bool known = false;
  PEXCEPTION_ROUTINE handler = nullptr;
  void *handler_data = nullptr;
  std::vector<unwind_code> operations;
  std::vector<size_t> boundaries;
};

// Native epilogue scanning depends on the bytes following ControlPc. A
// relocation can insert a branch or literal there, so encode the remaining
// restores explicitly and suppress scanning of the generated sequence.
bool read_epilogue(uintptr_t pc, uintptr_t end, uint8_t frame_register,
                   std::vector<unwind_code> &operations,
                   snapshot_list &snapshots) {
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  auto allocate = [&](uint64_t bytes) {
    if (bytes > UINT32_MAX || (bytes & 7) != 0)
      return false;
    if (bytes != 0) {
      std::vector<uint8_t> codes{0, 0x11};
      const uint32_t amount = static_cast<uint32_t>(bytes);
      const auto *data = reinterpret_cast<const uint8_t *>(&amount);
      codes.insert(codes.end(), data, data + sizeof(amount));
      operations.push_back({0, std::move(codes), false});
    }
    return true;
  };
  for (unsigned count = 0; pc < end && count != 32; ++count) {
    std::array<uint8_t, 15> bytes{};
    const auto available = read_code_prefix(
        pc, {bytes.data(), std::min(bytes.size(), size_t(end - pc))});
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, bytes.data(), available,
                                             &instruction, operands)))
      return false;
    const auto reg = [&](unsigned i) {
      return operands[i].type == ZYDIS_OPERAND_TYPE_REGISTER
                 ? operands[i].reg.value
                 : ZYDIS_REGISTER_NONE;
    };
    if (count == 0 && instruction.mnemonic == ZYDIS_MNEMONIC_ADD &&
        reg(0) == ZYDIS_REGISTER_RSP &&
        operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE) {
      if (operands[1].imm.value.s < 0 || !allocate(operands[1].imm.value.u))
        return false;
    } else if (count == 0 && instruction.mnemonic == ZYDIS_MNEMONIC_LEA &&
               reg(0) == ZYDIS_REGISTER_RSP && (frame_register & 15) != 0 &&
               operands[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
               operands[1].mem.base ==
                   ZydisRegisterEncode(ZYDIS_REGCLASS_GPR64,
                                       frame_register & 15) &&
               operands[1].mem.index == ZYDIS_REGISTER_NONE) {
      const int64_t amount =
          operands[1].mem.disp.value + int64_t(frame_register >> 4) * 16;
      if (amount < 0)
        return false;
      operations.push_back({0, {0, 3}, true});
      if (!allocate(static_cast<uint64_t>(amount)))
        return false;
    } else if (instruction.mnemonic == ZYDIS_MNEMONIC_POP &&
               operands[0].size == 64 &&
               ZydisRegisterGetClass(reg(0)) == ZYDIS_REGCLASS_GPR64) {
      const auto id = ZydisRegisterGetId(reg(0));
      if (id != 3 && id != 5 && id != 6 && id != 7 && id < 12)
        return false;
      operations.push_back({0, {0, static_cast<uint8_t>(id << 4)}, false});
    } else if (instruction.mnemonic == ZYDIS_MNEMONIC_RET &&
               instruction.operand_count_visible == 0) {
      snapshots.emplace_back(
          pc, std::vector<uint8_t>(bytes.begin(),
                                   bytes.begin() + instruction.length));
      return true;
    } else {
      // A transfer within this function can still need its frame. Accept
      // only the native memory tail-jump encoding here.
      if (instruction.mnemonic != ZYDIS_MNEMONIC_JMP ||
          operands[0].type != ZYDIS_OPERAND_TYPE_MEMORY ||
          instruction.raw.modrm.mod != 0 || instruction.raw.prefix_count > 1 ||
          (instruction.raw.prefix_count == 1 &&
           (instruction.raw.prefixes[0].value & 0xF0) != 0x40))
        return false;
      snapshots.emplace_back(
          pc, std::vector<uint8_t>(bytes.begin(),
                                   bytes.begin() + instruction.length));
      return true;
    }
    snapshots.emplace_back(
        pc, std::vector<uint8_t>(bytes.begin(),
                                 bytes.begin() + instruction.length));
    pc += instruction.length;
  }
  return false;
}

// A copied span can cross shrink-wrapped chained fragments. Resolve the
// native rule at each instruction, rather than applying the entry's record
// to every instruction that follows it.
bool read_source_state(uintptr_t pc, uintptr_t source, source_state &result,
                       snapshot_list &snapshots) {
  DWORD64 base = 0;
  const auto *function = RtlLookupFunctionEntry(pc, &base, nullptr);
  if (function == nullptr)
    return true;
  std::vector<uint8_t> descriptor;
  if (!read_snapshot(reinterpret_cast<uintptr_t>(function), sizeof(*function),
                     descriptor, snapshots))
    return false;
  RUNTIME_FUNCTION current;
  std::memcpy(&current, descriptor.data(), sizeof(current));
  result.base = base;
  result.known = true;
  std::vector<RUNTIME_FUNCTION> visited;
  int frame_register = -1;
  bool first = true;
  uintptr_t fragment_end = 0;
  unsigned fragment_version = 0;
  std::vector<std::pair<uintptr_t, uintptr_t>> epilogues;
  for (;;) {
    if (visited.size() == 64 || current.EndAddress <= current.BeginAddress ||
        base + current.BeginAddress < source ||
        std::any_of(
            visited.begin(), visited.end(),
            [&](const auto &seen) {
              return seen.BeginAddress == current.BeginAddress &&
                     seen.EndAddress == current.EndAddress &&
                     seen.UnwindData == current.UnwindData;
            }))
      return false;
    visited.push_back(current);
    result.end = std::max(result.end, uintptr_t(base + current.EndAddress));
    if ((current.UnwindData & 1) != 0) {
      if (!read_snapshot(base + (current.UnwindData & ~DWORD{1}),
                         sizeof(current), descriptor, snapshots))
        return false;
      std::memcpy(&current, descriptor.data(), sizeof(current));
      continue;
    }
    std::vector<uint8_t> native;
    const uintptr_t data = base + current.UnwindData;
    if (!read_snapshot(data, 4, native, snapshots))
      return false;
    if ((native[0] & 7) != 1 && (native[0] & 7) != 2)
      return note_failure("unsupported_unwind_version", data);
    const unsigned flags = native[0] >> 3;
    const unsigned fp = native[3] & 15;
    if (flags > 4 || native[1] > current.EndAddress - current.BeginAddress ||
        (fp == 0 && native[3] != 0) ||
        (fp != 0 && fp != 3 && fp != 5 && fp != 6 && fp != 7 && fp < 12) ||
        (frame_register >= 0 && native[3] != frame_register))
      return false;
    frame_register = native[3];
    if (first) {
      result.prologue_end = base + current.BeginAddress + native[1];
      result.prologue = pc < result.prologue_end;
      fragment_end = base + current.EndAddress;
      fragment_version = native[0] & 7;
      first = false;
    }
    const size_t tail = 4 + ((size_t(native[2]) + 1) & ~size_t{1}) * 2;
    if (!read_snapshot(data,
                       tail + (flags == 4   ? 12
                               : flags != 0 ? 4
                                            : 0),
                       native, snapshots))
      return false;
    unsigned epilog_size = 0, epilogs = 0, previous_offset = 256;
    bool ordinary_codes = false;
    for (size_t at = 0; at < native[2];) {
      const size_t pos = 4 + at * 2;
      const unsigned opcode = native[pos + 1] & 15;
      const unsigned info = native[pos + 1] >> 4;
      if (opcode == 6) {
        // Version 2 prefixes the ordinary prologue codes with one epilogue
        // size, optional tail-relative offsets, and an optional zero padding
        // entry. Generated records use version 1 and describe copied prologues.
        if ((native[0] & 7) != 2 || ordinary_codes)
          return false;
        unsigned offset = native[pos] + info * 256;
        if (at == 0) {
          epilog_size = native[pos];
          if (epilog_size == 0 || info > 1)
            return false;
          offset = info == 1 ? epilog_size : 0;
        }
        if (offset != 0) {
          const auto length = current.EndAddress - current.BeginAddress;
          if (offset < epilog_size || offset > length)
            return false;
          const uintptr_t begin = base + current.EndAddress - offset;
          std::vector<unwind_code> restores;
          if (!read_epilogue(begin, begin + epilog_size, native[3], restores,
                             snapshots))
            return false;
          epilogues.emplace_back(begin, begin + epilog_size);
          ++epilogs;
        }
        ++at;
        continue;
      }
      size_t slots = 1;
      ordinary_codes = true;
      switch (opcode) {
      case 0:
      case 2:
        break;
      case 1:
        if (info > 1)
          return false;
        slots += info == 0 ? 1 : 2;
        break;
      case 3:
        if (info != 0 || (native[3] & 15) == 0)
          return false;
        break;
      case 4:
      case 8:
        slots = 2;
        break;
      case 5:
      case 9:
        slots = 3;
        break;
      default:
        // Machine frames and the version 3 large-allocation forms.
        return note_failure("unsupported_unwind_code", data);
      }
      if (slots > native[2] - at || native[pos] > native[1] ||
          native[pos] > previous_offset)
        return false;
      if ((opcode == 0 || opcode == 4 || opcode == 5) && info != 3 &&
          info != 5 && info != 6 && info != 7 && info < 12)
        return false;
      if ((opcode == 8 || opcode == 9) && info < 6)
        return false;
      previous_offset = native[pos];
      if ((flags & UNW_FLAG_CHAININFO) != 0 && opcode != 4 && opcode != 5 &&
          opcode != 8 && opcode != 9)
        return false;
      result.boundaries.push_back(base + current.BeginAddress + native[pos] -
                                  source);
      if (base + current.BeginAddress + native[pos] <= pc) {
        result.operations.push_back(
            {base + current.BeginAddress + native[pos] - source,
             {native.begin() + pos, native.begin() + pos + slots * 2},
             opcode == 3});
        result.operations.back().codes[0] = 0;
      }
      at += slots;
    }
    if (epilog_size != 0 && epilogs == 0)
      return false;

    if (flags == UNW_FLAG_CHAININFO) {
      std::memcpy(&current, native.data() + tail, sizeof(current));
      continue;
    }
    if (base + current.BeginAddress != source)
      return false; // A fragment entered with its parent's frame is not an
                    // ordinary callable entry.
    result.original = current;
    result.flags = static_cast<uint8_t>(flags);
    result.frame_register = native[3];
    if (flags != 0) {
      uint32_t rva;
      std::memcpy(&rva, native.data() + tail, 4);
      result.handler = reinterpret_cast<PEXCEPTION_ROUTINE>(base + rva);
      result.handler_data = reinterpret_cast<void *>(data + tail + 4);
    }
    if (!result.prologue && (fragment_version == 1 ||
                             std::any_of(epilogues.begin(), epilogues.end(),
                                         [&](const auto &epilogue) {
                                           return pc >= epilogue.first &&
                                                  pc < epilogue.second;
                                         }))) {
      std::vector<unwind_code> restores;
      if (read_epilogue(pc, fragment_end, result.frame_register, restores,
                        snapshots)) {
        result.operations = std::move(restores);
        result.epilogue = true;
      }
    }
    return true;
  }
}

// A generated range in one source state. Prologue ranges carry a synthetic
// prologue length so native dispatch runs no handler and no epilogue scan.
struct interval {
  size_t begin, end, source, state;
  bool prologue;
};

// Writes a function-table entry per generated interval, split where a
// temporary spill changes SP, and the unwind records they share.
bool add_records(function_table &table, size_t size, uintptr_t entry,
                 std::span<const source_state> states,
                 std::span<const interval> intervals,
                 std::span<const stack_adjustment> stack) {
  const uintptr_t image = table.allocation - table.prefix;
  std::vector<std::pair<std::vector<uint8_t>, DWORD>> records;
  auto add = [&](size_t begin, size_t end, size_t source_offset, int64_t spill,
                 bool prologue, size_t state_index = 0) {
    const auto &state = states[state_index];
    const auto &operations = state.operations;
    if (end <= begin || (prologue && end - begin > 255) || spill < 0 ||
        spill > UINT32_MAX || (spill & 7) != 0)
      return false;
    std::vector<uint8_t> record{
        static_cast<uint8_t>(
            1 | (table.source.handler != nullptr ? (state.flags << 3) : 0)),
        static_cast<uint8_t>(prologue ? end - begin : 0), 0, 0};
    if (spill != 0) {
      const uint32_t bytes = static_cast<uint32_t>(spill);
      record.insert(record.end(), {0, 0x11});
      const auto *value = reinterpret_cast<const uint8_t *>(&bytes);
      record.insert(record.end(), value, value + 4);
    }
    const bool frame_pointer = std::any_of(
        operations.begin(), operations.end(), [&](const auto &operation) {
          return operation.frame_pointer && operation.offset <= source_offset;
        });
    for (const auto &operation : operations) {
      if (operation.offset > source_offset)
        continue;
      auto codes = operation.codes;
      const unsigned opcode = codes[1] & 15;
      if (spill != 0 && !frame_pointer &&
          (opcode == 4 || opcode == 5 || opcode == 8 || opcode == 9)) {
        uint32_t displacement = 0;
        if (codes.size() == 4) {
          uint16_t units;
          std::memcpy(&units, codes.data() + 2, 2);
          displacement = uint32_t(units) * (opcode == 4 ? 8 : 16);
        } else {
          std::memcpy(&displacement, codes.data() + 2, 4);
        }
        if (uint64_t(displacement) + spill > UINT32_MAX)
          return false;
        displacement += static_cast<uint32_t>(spill);
        // SAVE offsets use the initial frame base, before ALLOC unwinds the
        // temporary spill. FAR avoids overflow in the scaled short form.
        codes[1] =
            static_cast<uint8_t>((codes[1] & 0xF0) | (opcode <= 5 ? 5 : 9));
        codes.resize(6);
        std::memcpy(codes.data() + 2, &displacement, 4);
      }
      record.insert(record.end(), codes.begin(), codes.end());
      if (operation.frame_pointer)
        record[3] = state.frame_register;
    }
    const size_t slots = (record.size() - 4) / 2;
    if (slots > 255)
      return false;
    record[2] = static_cast<uint8_t>(slots);
    if ((slots & 1) != 0)
      record.insert(record.end(), {0, 0});
    if (table.source.handler != nullptr) {
      const uint32_t stub_rva = 0;
      const auto *value = reinterpret_cast<const uint8_t *>(&stub_rva);
      record.insert(record.end(), value, value + 4);
      const auto *binding = &table;
      value = reinterpret_cast<const uint8_t *>(&binding);
      record.insert(record.end(), value, value + sizeof(binding));
    }
    DWORD rva = 0;
    for (const auto &[existing, offset] : records)
      if (existing == record) {
        rva = offset;
        break;
      }
    if (rva == 0) {
      if (record.size() > table.prefix - table.bytes.size())
        return false;
      rva = static_cast<DWORD>(table.bytes.size());
      table.bytes.insert(table.bytes.end(), record.begin(), record.end());
      records.emplace_back(std::move(record), rva);
    }
    const auto offset = entry - image;
    if (offset + end > UINT32_MAX)
      return false;
    if (!prologue && !table.functions.empty() &&
        table.functions.back().UnwindData == rva &&
        table.functions.back().EndAddress == offset + begin) {
      table.functions.back().EndAddress = static_cast<DWORD>(offset + end);
      return true;
    }
    table.functions.push_back({static_cast<DWORD>(offset + begin),
                               static_cast<DWORD>(offset + end), rva});
    return true;
  };
  if (table.source.source == 0) {
    entry = table.allocation;
    if (!add(0, size, 0, 0, false))
      return false;
  } else {
    if (entry < table.allocation || entry - table.allocation > size)
      return false;
    if (entry != table.allocation) {
      table.functions.push_back({static_cast<DWORD>(table.prefix),
                                 static_cast<DWORD>(entry - image), 0});
      table.bytes.insert(table.bytes.end(), {1, 0, 0, 0});
      table.functions.back().UnwindData =
          static_cast<DWORD>(table.bytes.size() - 4);
    }
    for (const auto &interval : intervals) {
      size_t begin = interval.begin;
      int64_t spill = 0;
      for (const auto &adjustment : stack) {
        if (adjustment.offset <= begin) {
          spill = adjustment.displacement;
          continue;
        }
        if (adjustment.offset >= interval.end)
          break;
        if (!add(begin, adjustment.offset, interval.source, spill,
                 interval.prologue, interval.state))
          return false;
        begin = adjustment.offset;
        spill = adjustment.displacement;
      }
      if (!add(begin, interval.end, interval.source, spill, interval.prologue,
               interval.state))
        return false;
    }
  }
  return !table.functions.empty();
}

// Fills `table` for a trampoline at `entry` whose first `copied` bytes
// came from `source`, or with one frameless record when `source` is 0.
bool prepare_function_table(
    function_table &table, size_t size, uintptr_t entry, uintptr_t source,
    size_t copied, std::span<const std::pair<uintptr_t, uintptr_t>> offsets,
    std::span<const unwind_range> ranges,
    std::span<const stack_adjustment> stack, size_t patch,
    std::span<const uint8_t> generated, source_view source_code,
    snapshot_list &snapshots) {
  if (size == 0 || size > UINT32_MAX - table.prefix)
    return note_failure("unwind_allocation_too_large", table.allocation);
  table.entry = entry;
  // The handler stub at the table's base: endbr64, then jmp [rip] to the
  // thunk's absolute address.
  table.bytes.resize(32, 0);
  const uint8_t stub[] = {0xF3, 0x0F, 0x1E, 0xFA, 0xFF, 0x25, 0, 0, 0, 0};
  std::memcpy(table.bytes.data(), stub, sizeof(stub));
  const auto thunk = reinterpret_cast<uintptr_t>(&forward_to_source_handler);
  std::memcpy(table.bytes.data() + 10, &thunk, sizeof(thunk));
  std::vector<source_state> states(1);
  std::vector<interval> intervals;
  if (source == 0)
    return add_records(table, size, entry, states, intervals, stack) ||
           note_failure("unwind_records_too_large", table.allocation);

  // Each copied instruction's native state, which must come from the same
  // function as the source's own. The PC after the copy may also be past
  // the function's last entry.
  auto read_state = [&](uintptr_t pc, source_state &state,
                        bool may_end = false) {
    if (!read_source_state(pc, source, state, snapshots))
      return note_failure("unsupported_unwind_info", pc);
    const auto &initial = states[0];
    if ((state.known != initial.known && !(may_end && !state.known)) ||
        (state.known &&
         (state.base != initial.base ||
          state.original.BeginAddress != initial.original.BeginAddress ||
          state.original.UnwindData != initial.original.UnwindData)))
      return note_failure("copy_leaves_function", pc);
    return true;
  };
  if (!read_source_state(source, source, states[0], snapshots))
    return note_failure("unsupported_unwind_info", source);
  table.source = {states[0].base, states[0].original, source, states[0].handler,
                  states[0].handler_data};
  const uintptr_t prologue_end = states[0].prologue_end;
  uintptr_t source_end = states[0].end;

  const bool fixed = ranges.size() == 1 &&
                     ranges[0].frame_state == unwind_range::state::kFixed;
  // Through the relocator's view: a landing can cover the source now.
  std::vector<uint8_t> code(copied);
  if (copied > 16384 || source_code.read(source, code) != copied)
    return note_failure("unreadable_source", source);
  snapshots.emplace_back(source, code);
  if (ranges.size() != 1 || ranges[0].begin != 0 ||
      (!fixed && ranges[0].source_offset != 0))
    return note_failure("unsupported_trampoline_layout", entry);
  ZydisDecoder decoder;
  ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
  if (fixed) {
    // A relay veneer stands for one source JMP and keeps its frame state.
    const auto offset = ranges[0].source_offset;
    ZydisDecodedInstruction instruction;
    if (offset >= copied || ranges[0].end > generated.size() ||
        !ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(
            &decoder, nullptr, code.data() + offset, copied - offset,
            &instruction)) ||
        instruction.meta.category != ZYDIS_CATEGORY_UNCOND_BR)
      return note_failure("unsupported_trampoline_layout", entry);
    source_state branch;
    if (!read_state(source + offset, branch))
      return false;
    source_end = std::max(source_end, branch.end);
    states.push_back(std::move(branch));
    // Keep the source's handler visibility. A source epilogue needs a
    // synthetic prologue to prevent a second native tail-jump scan; an
    // interior prefixed JMP must still dispatch its original scopes.
    intervals.push_back({0, ranges[0].end, offset, 1,
                         states.back().prologue || states.back().epilogue});
    table.instructions.push_back(
        {0, ranges[0].end, offset, offset + instruction.length, 0, true});
  } else {
    for (size_t at = 0; at < copied;) {
      ZydisDecodedInstruction instruction;
      ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
      if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(
              &decoder, code.data() + at, copied - at, &instruction, operands)))
        return note_failure("undecodable_copy", source + at);
      const size_t next = at + instruction.length;
      const auto category = instruction.meta.category;
      const bool call = category == ZYDIS_CATEGORY_CALL;
      source_state state;
      if (!read_state(source + at, state))
        return false;
      source_end = std::max(source_end, state.end);
      const size_t state_index = states.size();
      states.push_back(std::move(state));
      const auto &current = states.back();
      // Outside the prologue and epilogue the native codes describe a
      // settled frame. A copied instruction that changes it has no record.
      if (!current.operations.empty() && !current.prologue &&
          !current.epilogue) {
        bool changes_frame = category == ZYDIS_CATEGORY_RET ||
                             instruction.mnemonic == ZYDIS_MNEMONIC_POP ||
                             instruction.mnemonic == ZYDIS_MNEMONIC_LEAVE;
        for (unsigned i = 0; i < instruction.operand_count && !call; ++i)
          changes_frame |=
              operands[i].type == ZYDIS_OPERAND_TYPE_REGISTER &&
              ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64,
                                               operands[i].reg.value) ==
                  ZYDIS_REGISTER_RSP &&
              (operands[i].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0;
        if (changes_frame)
          return note_failure("copied_frame_change", source + at);
      }
      const size_t begin = relocated_location_at(offsets, at),
                   end = relocated_location_at(offsets, next);
      if (end <= begin || end > generated.size())
        return note_failure("unsupported_trampoline_layout", source + at);
      size_t call_return = 0;
      if (call) {
        for (size_t pc = begin; pc < end;) {
          ZydisDecodedInstruction emitted;
          if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr,
                                                          generated.data() + pc,
                                                          end - pc, &emitted)))
            return note_failure("unsupported_call_expansion", source + at);
          if (emitted.meta.category == ZYDIS_CATEGORY_CALL) {
            int64_t displacement = 0;
            for (const auto &adjustment : stack) {
              if (adjustment.offset > pc)
                break;
              displacement = adjustment.displacement;
            }
            // The return-slot rewrite restores SP before transferring to
            // the callee. Exception dispatch must observe that source SP.
            if (displacement != 0)
              return note_failure("unsupported_call_expansion", source + at);
            call_return = pc + emitted.length;
            break; // Bytes after the call can include its absolute literal.
          }
          pc += emitted.length;
        }
        if (call_return == 0)
          return note_failure("unsupported_call_expansion", source + at);
      }
      table.instructions.push_back({begin, end, at, next, call_return});
      // Native epilogue detection mistakes a relocated branch for a tail
      // return. Suppress it only at branch PCs; call return PCs keep
      // handlers.
      if (call && end - begin == 17 && generated[begin] == 0xFF &&
          generated[begin + 1] == 0x15 && generated[begin + 6] == 0x90) {
        intervals.push_back({begin, begin + 7, at, state_index, false});
        intervals.push_back({begin + 7, end, at, state_index, true});
      } else if ((category == ZYDIS_CATEGORY_UNCOND_BR ||
                  category == ZYDIS_CATEGORY_COND_BR) &&
                 generated[begin] == 0x90 && end - begin > 1) {
        intervals.push_back(
            {begin, begin + 1, at, state_index, current.prologue});
        intervals.push_back({begin + 1, end, at, state_index, true});
      } else {
        intervals.push_back({begin, end, at, state_index,
                             current.prologue || current.epilogue ||
                                 category == ZYDIS_CATEGORY_UNCOND_BR ||
                                 category == ZYDIS_CATEGORY_COND_BR});
      }
      at = next;
    }
    // Operation offsets must land at instruction ends, including partial
    // frames.
    for (const auto &state : states)
      for (const auto boundary : state.boundaries)
        if (boundary <= copied &&
            std::none_of(
                table.instructions.begin(), table.instructions.end(),
                [&](const auto &i) { return i.source_end == boundary; }))
          return note_failure("unwind_code_inside_instruction",
                              source + boundary);
    // The return jump: a NOP that a copied call's return address can land
    // on, in the state after the copy, then the jump in a prologue range.
    const size_t end = relocated_location_at(offsets, copied);
    if (end >= generated.size() || generated[end] != 0x90 ||
        ranges[0].end > generated.size() || ranges[0].end <= end + 1)
      return note_failure("unsupported_trampoline_layout", entry);
    source_state final;
    if (!read_state(source + copied, final, true))
      return false;
    const size_t final_index = states.size();
    states.push_back(std::move(final));
    intervals.push_back({end, end + 1, copied, final_index, false});
    intervals.push_back({end + 1, ranges[0].end, copied, final_index, true});
  }

  if (table.source.handler != nullptr) {
    switch (check_handler(table.source, source_end, patch, snapshots)) {
    case handler_check::kSafe:
      break;
    case handler_check::kRefused:
      return false;
    case handler_check::kUnknown: {
      // Windows calls no language handler for a frame whose ControlPc is in
      // its prologue. When every trampoline PC maps to a source prologue PC,
      // the native function would not call the handler there either, so the
      // trampoline records omit it rather than run code spud cannot read.
      // The original function keeps its handler: a handler resumes in the
      // body with the frame built, never inside the prologue, so it cannot
      // reach overwritten prologue bytes.
      const bool prologue_only =
          source + patch <= prologue_end &&
          std::all_of(states.begin(), states.end(),
                      [](const auto &state) { return state.prologue; });
      if (!prologue_only)
        return note_failure("unknown_language_handler",
                            reinterpret_cast<uintptr_t>(table.source.handler));
      table.source.handler = nullptr;
      table.source.handler_data = nullptr;
      break;
    }
    }
  }
  return add_records(table, size, entry, states, intervals, stack) ||
         note_failure("unwind_records_too_large", entry);
}

} // namespace
} // namespace spud::detail::xdata

namespace spud::detail {

struct unwind_registration::platform : xdata::function_table {};

unwind_registration::unwind_registration() = default;

unwind_registration::~unwind_registration() {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  platform_.reset();
  if (source_module_ != nullptr)
    close_source_module(source_module_);
}

bool unwind_registration::prepare(uintptr_t allocation, size_t size,
                                  uintptr_t entry, uintptr_t source,
                                  size_t copied,
                                  const trampoline_buffer &trampoline,
                                  std::span<const size_t>, size_t patch_size,
                                  source_view code) {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  if (!retain_source_module(source, source_module_))
    return note_failure("source_module_unavailable", source);
  auto table = std::make_unique<platform>();
  table->allocation = allocation;
  // The handler stub slot and one frameless record. Only leaf allocations
  // are prepared without a source.
  static_assert(kWindowsLeafUnwindBytes >= 32 + 4 &&
                kWindowsLeafUnwindBytes % 16 == 0);
  if (source == 0)
    table->prefix = kWindowsLeafUnwindBytes;
  // Windows decodes the emitted code from the entry.
  const auto generated = std::span(trampoline.data).subspan(trampoline.start);
  if (!xdata::prepare_function_table(
          *table, size, entry, source, copied, trampoline.relocation_offsets,
          trampoline.unwind_ranges, trampoline.stack_adjustments,
          patch_size != 0 ? patch_size : copied, generated, code,
          source_bytes_))
    return false;
  source_bytes_ = merge_snapshots(std::move(source_bytes_));
  platform_ = std::move(table);
  return true;
}

bool unwind_registration::publish() {
  SPUD_ASSERT_OUTSIDE_SUSPEND_WINDOW();
  auto &table = *platform_;
  const auto base = table.allocation - table.prefix;
  std::memcpy(reinterpret_cast<void *>(base), table.bytes.data(),
              table.bytes.size());
  if (!flush_cache(reinterpret_cast<void *>(base), table.bytes.size()))
    return note_failure("unwind_flush", base);
  // CFG must accept the handler stub as an indirect call target.
  if (table.source.handler != nullptr &&
      !register_call_target(reinterpret_cast<void *>(base), 18))
    return note_failure("handler_stub_call_target", base);
  if (!RtlAddFunctionTable(table.functions.data(),
                           static_cast<DWORD>(table.functions.size()), base))
    return note_failure("function_table_registration", base);
  table.published = true;
  return true;
}

std::optional<std::pair<uintptr_t, uintptr_t>>
function_extent(uintptr_t address) {
  DWORD64 base = 0;
  const auto *function = RtlLookupFunctionEntry(address, &base, nullptr);
  if (function == nullptr || base + function->BeginAddress != address ||
      function->EndAddress <= function->BeginAddress)
    return std::nullopt;
  uintptr_t end = base + function->EndAddress;
  // Shrink wrapping continues a function in chained entries placed right
  // after it, and their code can branch back. Real chains are short.
  for (int i = 0; i < 16; ++i) {
    DWORD64 next_base = 0;
    const auto *next = RtlLookupFunctionEntry(end, &next_base, nullptr);
    uint8_t header = 0;
    if (next == nullptr || next_base + next->BeginAddress != end ||
        next->EndAddress <= next->BeginAddress || (next->UnwindData & 1) != 0 ||
        !read_code_bytes(next_base + next->UnwindData, {&header, 1}) ||
        ((header >> 3) & UNW_FLAG_CHAININFO) == 0)
      break;
    end = next_base + next->EndAddress;
  }
  return std::pair{address, end};
}

} // namespace spud::detail
#endif
#endif

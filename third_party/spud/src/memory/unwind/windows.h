// Windows x64 unwind data (.pdata/.xdata) shared by the record translator
// in windows.cc and the language-handler checks in windows_handler.cc.
#pragma once

#include <spud/arch.h>

#if SPUD_UNWIND_SUPPORT && SPUD_OS_WIN
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <windows.h>

namespace spud::detail::xdata {

// Source bytes a translation read, by address; commit rereads them.
using snapshot_list = std::vector<std::pair<uintptr_t, std::vector<uint8_t>>>;

// Reads `size` bytes at `address` into `bytes` and snapshots them, reusing
// an earlier snapshot of the same range. Bounded at 64 KiB.
bool read_snapshot(uintptr_t address, size_t size, std::vector<uint8_t> &bytes,
                   snapshot_list &snapshots);

// The source function's language handler as its primary unwind info names
// it, and where the hook overwrites it.
struct handler_source {
  uintptr_t base = 0;
  RUNTIME_FUNCTION original{};
  uintptr_t source = 0;
  PEXCEPTION_ROUTINE handler = nullptr;
  void *handler_data = nullptr;
};

enum class handler_check {
  // A known handler whose scopes, funclets and continuations avoid the
  // overwritten bytes.
  kSafe,
  // A handler spud does not recognize, so its tables cannot be read.
  kUnknown,
  // Known, but its tables are unsupported or reach the overwritten bytes.
  kRefused,
};

// Recognizes `frame.handler` and walks every path its tables can transfer
// to. `source_end` bounds the source function including chained parts;
// `patch` is the overwritten prefix. Records a reason on kRefused.
handler_check check_handler(const handler_source &frame, uintptr_t source_end,
                            size_t patch, snapshot_list &snapshots);

} // namespace spud::detail::xdata
#endif

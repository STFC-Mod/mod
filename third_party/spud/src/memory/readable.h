#pragma once

#include <spud/memory.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace spud::detail {

// Test-only (Linux): ignores PROCMAP_QUERY, so lookups parse
// /proc/self/maps as they do on kernels before 6.11.
extern std::atomic<bool> g_disable_procmap_for_tests;

// query_memory without touching the calling thread's OS error, for spud's
// own lookups, which must not clear or invent a code mid-operation.
std::optional<memory_region> find_region(uintptr_t address);

// Readable bytes from address, up to limit, continuing through adjacent
// readable mappings with the same execute permission. A code page the process
// wrote to can sit in its own mapping while its neighbours stay in the
// original one.
size_t readable_bytes_from(uintptr_t address, size_t limit);

// Reads through the OS, so a stale mapping or an MTE allocation tag cannot
// turn decoding an untrusted address into a process fault.
bool read_code_bytes(uintptr_t address, std::span<uint8_t> out);
size_t read_code_prefix(uintptr_t address, std::span<uint8_t> out);
// Whether indirect branches into the page need a BTI landing: the smaps `bt`
// flag on Linux arm64, or true there when the hardware has BTI and the
// attributes cannot be read. Always false elsewhere: macOS has no interface
// for guarded user pages, and spud has no Windows ARM64 backend.
bool bti_guarded_page(uintptr_t address);

#if SPUD_OS_APPLE
// Whether [address, address + size) is in one MAP_JIT mapping, including a
// pool mapping Rosetta reports without the JIT flag. Such memory is written
// through the thread's JIT permission, never by changing its protection.
// Rosetta lowers a page it has translated to RX and handles the write fault
// itself; a protection change that puts RX back makes the RX look
// deliberate, and Rosetta then leaves every later store to fault.
bool jit_mapping(uintptr_t address, size_t size);
#endif

// Address ranges in [lo, hi) with nothing mapped, in address order. The
// executable pool places near allocations at their edges. Allocates, so
// call it outside the suspend window.
std::vector<std::pair<uintptr_t, uintptr_t>> unmapped_ranges(uintptr_t lo,
                                                             uintptr_t hi);

} // namespace spud::detail

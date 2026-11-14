#pragma once
#include <cstddef>

namespace native_hooks
{
// Validates the entry in the loaded image, before SPUD changes any bytes.
// Unsupported images/architectures fail closed.
// Small, exact-entry callbacks may request a lower floor; the host decoder
// still requires the complete SPUD overwrite window without an early exit.
bool MacHookFits(const void* method, std::size_t minimum_extent = 64);
#if defined(__APPLE__) && defined(__aarch64__)
const void* MacFollowDirectBranch(const void* method);
#endif
} // namespace native_hooks

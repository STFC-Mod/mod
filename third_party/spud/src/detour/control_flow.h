#pragma once

#include <cstddef>

namespace spud::detail {

// Windows CFG: mark generated executable bytes as valid indirect-call
// targets. No-op when CFG is off or on other platforms.
bool register_call_target(void *ptr, size_t size);

} // namespace spud::detail

#pragma once
// Logging is an external boundary, not part of the catalog under test.
namespace spdlog
{
template <typename... Args> void error(const char*, Args&&...) {}
template <typename... Args> void info(const char*, Args&&...) {}
} // namespace spdlog

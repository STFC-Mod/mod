#pragma once

#include "str_utils.h"

#include <spdlog/spdlog.h>

#if _WIN32
#include <winrt/Windows.Foundation.h>
#endif

namespace ErrorMsg
{
static void SyncMsg(const char* section, const std::string& msg)
{
  spdlog::error("Failed to send {} sync data: {}", section, msg);
}

static void SyncRuntime(const char* section, const std::runtime_error& e)
{
  spdlog::error("Runtime error sending {} sync data: {}", section, e.what());
}

static void SyncException(const char* section, const std::exception& e)
{
  spdlog::error("Exception sending {} sync data: {}", section, e.what());
}

#if _WIN32
static void SyncWinRT(const char* section, winrt::hresult_error const& ex)
{
  spdlog::error("WINRT Error sending {} sync data: {}", section, winrt::to_string(ex.message()));
}
#endif
};

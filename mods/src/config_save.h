#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.h>

// Synchronous whole-document output for startup, not a runtime setting editor.
// Throws on failure; the caller owns reporting. Does not guarantee power-loss durability.
void SaveConfigDocument(const toml::table& config, const std::filesystem::path& path, std::string_view header = {});
// Startup outputs must not prevent a readable configuration from loading.
// Returns the checked writer's error for the caller to report.
bool SaveStartupConfigDocument(const toml::table& config, const std::filesystem::path& path, std::string_view header,
                               std::string& error);

std::string ReadConfigText(const std::filesystem::path& path);
// Preserves the supplied text. With expected text, false means an external edit
// was detected before replacement. This is not a filesystem compare-and-swap.
bool ReplaceConfigText(const std::filesystem::path& path, std::string_view text,
                       std::optional<std::string_view> expected = std::nullopt);

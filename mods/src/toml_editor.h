#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <toml++/toml.h>
#include <variant>

namespace config_edit
{
using Value = std::variant<bool, std::string, double, std::int64_t>;
struct Request {
  std::string          section, key;
  std::optional<Value> expected; // Missing is distinct from a configured default.
  Value                desired;
};
enum class Outcome { Prepared, Saved, AlreadySaved, Conflict, InvalidDocument, Unsupported, IoError, Cancelled };
struct Prepared {
  Outcome     outcome;
  std::string text; // Nonempty only when an edit has been prepared.
};

// Own on a single worker. Cache represents disk text, not live game configuration.
class TomlEditor
{
public:
  Prepared Prepare(const std::string& text, const Request& request);
  Outcome  Save(const std::filesystem::path& path, const Request& request);

private:
  std::string                cached_text_;
  std::optional<toml::table> cached_table_;
};
} // namespace config_edit

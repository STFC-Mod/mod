#include "toml_editor.h"
#include "config_save.h"
#include "toml_source_editor.h"

#include <cmath>
#include <stdexcept>

namespace config_edit
{
namespace
{
  std::string Encode(const Value& value)
  {
    if (const auto* text = std::get_if<std::string>(&value)) {
      try {
        source::ValidateUtf8(*text);
      } catch (const source::Failure&) {
        throw std::invalid_argument("invalid desired UTF-8 string");
      }
    }
    return std::visit([](const auto& item) { return source::Render(toml::value(item)); }, value);
  }
  std::optional<Value> ReadValue(const toml::node* node)
  {
    if (!node)
      return std::nullopt;
    if (node->is_boolean())
      return Value{node->as_boolean()->get()};
    if (node->is_string())
      return Value{node->as_string()->get()};
    if (node->is_floating_point())
      return Value{node->as_floating_point()->get()};
    if (node->is_integer())
      return Value{node->as_integer()->get()};
    throw std::invalid_argument("unsupported setting type");
  }
} // namespace
Prepared TomlEditor::Prepare(const std::string& text, const Request& request)
{
  try {
    if (const auto* value = std::get_if<double>(&request.desired); value && !std::isfinite(*value))
      return {Outcome::Unsupported, {}};
    if (!cached_table_ || cached_text_ != text) {
      auto parsed = source::Parse(text);
      cached_table_.reset(); // Never pair new bytes with stale regions if allocation fails.
      cached_text_  = text;
      cached_table_ = std::move(parsed);
    }
    const auto& document = *cached_table_;
    const auto* parent   = document.get_as<toml::table>(request.section);
    if (document.contains(request.section) && !parent)
      return {Outcome::Unsupported, {}};
    const auto current = ReadValue(parent ? parent->get(request.key) : nullptr);
    if (current && *current == request.desired)
      return {Outcome::AlreadySaved, {}};
    if (current != request.expected)
      return {Outcome::Conflict, {}};
    auto result = source::SetValue(text, {request.section, request.key}, Encode(request.desired));
    return {Outcome::Prepared, std::move(result)};
  } catch (const source::Failure& error) {
    const std::string_view code(error.what());
    return {code == "InvalidDocument" || code == "InvalidUtf8" || code == "DuplicateTarget" ? Outcome::InvalidDocument
                                                                                            : Outcome::Unsupported,
            {}};
  } catch (const std::invalid_argument&) {
    return {Outcome::Unsupported, {}};
  }
}
Outcome TomlEditor::Save(const std::filesystem::path& path, const Request& request)
{
  try {
    const auto original = ReadConfigText(path);
    auto       edit     = Prepare(original, request);
    if (edit.outcome != Outcome::Prepared)
      return edit.outcome;
    if (!ReplaceConfigText(path, edit.text, original))
      return Outcome::Conflict;
    cached_table_.reset();
    cached_text_.clear();
    return Outcome::Saved;
  } catch (const std::exception&) {
    return Outcome::IoError;
  }
}
} // namespace config_edit

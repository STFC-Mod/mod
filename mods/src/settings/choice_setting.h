#pragma once
#include "value_view.h"
#include <vector>

namespace mod_settings
{
class ChoiceSetting
{
public:
  ChoiceSetting(ValueDefinition<int> definition, std::vector<std::string> labels)
      : labels_(std::move(labels))
      , state_(Checked(std::move(definition), labels_.size()))
  {
    for (const auto& label : labels_)
      if (label.empty())
        throw std::invalid_argument("choice label");
  }
  ValueSetting<int>& state()
  { return state_; }
  const std::vector<std::string>& labels() const
  { return labels_; }
  std::string item_id(int index) const
  { return state_.id() + "." + std::to_string(index); }

private:
  static ValueDefinition<int> Checked(ValueDefinition<int> definition, std::size_t count)
  {
    if (count < 2 || count > 16 || definition.id.empty() || definition.label.empty() || !definition.read
        || !definition.write)
      throw std::invalid_argument("choice setting definition");
    auto read       = std::move(definition.read);
    auto write      = std::move(definition.write);
    definition.read = [read = std::move(read), count] {
      auto result = read();
      if (result.known() && (*result.value < 0 || static_cast<std::size_t>(*result.value) >= count))
        return ValueReadResult<int>{};
      return result;
    };
    definition.write = [write = std::move(write), count](int value, std::uint64_t generation) {
      if (value < 0 || static_cast<std::size_t>(value) >= count)
        return ApplyResult::Rejected;
      return write(value, generation);
    };
    return definition;
  }
  std::vector<std::string> labels_;
  ValueSetting<int>        state_;
};
} // namespace mod_settings

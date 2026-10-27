#pragma once
#include "value_view.h"
#include <vector>

namespace mod_settings
{
enum class ChoiceMode { Single, Multiple };

class ChoiceSetting
{
public:
  ChoiceSetting(ValueDefinition<int> definition, std::vector<std::string> labels, ChoiceMode mode = ChoiceMode::Single)
      : labels_(std::move(labels))
      , mode_(mode)
      , state_(Checked(std::move(definition), labels_.size(), mode))
  {
    for (const auto& label : labels_)
      if (label.empty())
        throw std::invalid_argument("choice label");
  }
  ValueSetting<int>& state()
  { return state_; }
  const std::vector<std::string>& labels() const
  { return labels_; }
  bool multiple() const
  { return mode_ == ChoiceMode::Multiple; }
  bool Contains(int selected, int index) const
  {
    return index >= 0 && index < static_cast<int>(labels_.size())
           && (multiple() ? (selected & (1 << index)) != 0 : selected == index);
  }
  std::string SelectionLabel(int selected) const
  {
    if (!multiple())
      return labels_.at(selected);
    std::string text;
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i)
      if (Contains(selected, i)) {
        if (!text.empty())
          text += ", ";
        text += labels_[i];
      }
    return text.empty() ? "None" : text;
  }
  std::string title_id() const
  { return state_.id() + ".title"; }
  std::string item_id(int index) const
  { return state_.id() + "." + std::to_string(index); }

private:
  static ValueDefinition<int> Checked(ValueDefinition<int> definition, std::size_t count, ChoiceMode mode)
  {
    if (count < 2 || count > 16 || definition.id.empty() || definition.label.empty() || !definition.read
        || !definition.write)
      throw std::invalid_argument("choice setting definition");
    const auto limit = mode == ChoiceMode::Multiple ? (1 << count) : static_cast<int>(count);
    auto       read  = std::move(definition.read);
    auto       write = std::move(definition.write);
    definition.read  = [read = std::move(read), limit] {
      auto result = read();
      if (result.known() && (*result.value < 0 || *result.value >= limit))
        return ValueReadResult<int>{};
      return result;
    };
    definition.write = [write = std::move(write), limit](int value, std::uint64_t generation) {
      if (value < 0 || value >= limit)
        return ApplyResult::Rejected;
      return write(value, generation);
    };
    return definition;
  }
  std::vector<std::string> labels_;
  ChoiceMode               mode_;
  ValueSetting<int>        state_;
};
} // namespace mod_settings

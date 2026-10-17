#pragma once
#include "value_view.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace mod_settings
{
enum class SliderLabel { Value, Percentage };

class SliderSetting
{
public:
  SliderSetting(ValueDefinition<float> definition, float minimum, float maximum, float step,
                std::function<bool()> enabled, SliderLabel label = SliderLabel::Percentage,
                std::uint8_t displayDecimals = 2, std::string disabledReason = {})
      : state_(Checked(std::move(definition), minimum, maximum, enabled))
      , minimum_(minimum)
      , maximum_(maximum)
      , step_(step)
      , enabled_(std::move(enabled))
      , label_(label)
      , displayDecimals_(displayDecimals)
      , disabledReason_(std::move(disabledReason))
  {
    if (!std::isfinite(step) || step <= 0)
      throw std::invalid_argument("slider step");
  }
  ValueSetting<float>& state()
  { return state_; }
  float minimum() const
  { return minimum_; }
  float maximum() const
  { return maximum_; }
  SliderLabel label() const
  { return label_; }
  float DisplayValue(float value) const
  {
    // Presentation only: never quantize a loaded preference or change its step.
    const auto scale = std::pow(10.0, displayDecimals_);
    return static_cast<float>(std::round(static_cast<double>(value) * scale) / scale);
  }
  bool enabled() const
  { return enabled_(); }
  const std::string& disabledReason() const
  { return disabledReason_; }
  float Snap(float value) const
  {
    return std::clamp(
        static_cast<float>(minimum_ + std::round((static_cast<double>(value) - minimum_) / step_) * step_), minimum_,
        maximum_);
  }

private:
  static ValueDefinition<float> Checked(ValueDefinition<float> definition, float minimum, float maximum,
                                        const std::function<bool()>& enabled)
  {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum || !enabled || !definition.read
        || !definition.write)
      throw std::invalid_argument("slider definition");
    auto read       = std::move(definition.read);
    auto write      = std::move(definition.write);
    definition.read = [read = std::move(read), minimum, maximum] {
      auto value = read();
      if (value.known()) {
        if (!std::isfinite(*value.value))
          return ValueReadResult<float>{Availability::Unavailable, {}, 0, UnavailableReason::InvalidValue};
        if (*value.value < minimum || *value.value > maximum)
          return ValueReadResult<float>{Availability::Unavailable, {}, 0, UnavailableReason::OutsideRange};
      }
      return value;
    };
    definition.write = [write = std::move(write), minimum, maximum, enabled](float value, std::uint64_t generation) {
      if (!enabled() || !std::isfinite(value) || value < minimum || value > maximum)
        return ApplyResult::Rejected;
      return write(value, generation);
    };
    return definition;
  }
  ValueSetting<float>   state_;
  float                 minimum_, maximum_, step_;
  std::function<bool()> enabled_;
  SliderLabel           label_;
  std::uint8_t          displayDecimals_;
  // Feature-owned wording; the shared native widget does not know the dependency.
  std::string disabledReason_;
};
} // namespace mod_settings

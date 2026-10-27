#pragma once
#include "boolean_view.h"
#include "choice_setting.h"
#include "slider_setting.h"
#include <string_view>

namespace mod_settings
{
// Both native widgets expose a bool click. Selection rows retain the complete
// integer snapshot so changing between two other choices still makes a row stale.
class NativeViewState
{
public:
  explicit NativeViewState(BooleanSetting& setting)
      : boolean_(std::in_place, setting)
  {
  }
  NativeViewState(ChoiceSetting& setting, int index)
      : choice_(std::in_place, setting.state())
      , group_(&setting)
      , index_(index)
  {
    if (index < 0 || index >= static_cast<int>(setting.labels().size()))
      throw std::invalid_argument("choice row index");
  }
  explicit NativeViewState(SliderSetting& setting)
      : slider_(std::in_place, setting.state())
      , sliderSetting_(&setting)
  {
  }
  void Bind()
  {
    if (boolean_)
      boolean_->Bind();
    else if (choice_)
      choice_->Bind();
    else
      slider_->Bind();
  }
  void Unbind()
  {
    if (boolean_)
      boolean_->Unbind();
    else if (choice_)
      choice_->Unbind();
    else
      slider_->Unbind();
  }
  void Invalidate()
  {
    if (boolean_)
      boolean_->Invalidate();
    else if (choice_)
      choice_->Invalidate();
    else
      slider_->Invalidate();
  }
  bool failed() const
  { return boolean_ ? boolean_->failed() : choice_ ? choice_->failed() : slider_->failed(); }
  bool known() const
  { return slider_ ? slider_->value().has_value() : value().has_value(); }
  bool enabled() const
  { return known() && (!sliderSetting_ || sliderSetting_->enabled()); }
  std::string_view disabledReason() const
  { return sliderSetting_ ? std::string_view(sliderSetting_->disabledReason()) : std::string_view{}; }
  std::string_view unavailableReason() const
  {
    const auto reason = boolean_  ? boolean_->unavailableReason()
                        : choice_ ? choice_->unavailableReason()
                                  : slider_->unavailableReason();
    switch (reason) {
      case UnavailableReason::OutsideRange:
        return "Outside slider range; use Edit";
      case UnavailableReason::InvalidValue:
        return "Invalid value; edit TOML";
      default:
        return "Reopen to retry";
    }
  }
  float number() const
  { return slider_ ? slider_->value().value_or(sliderSetting_->minimum()) : 0.0f; }
  float displayNumber() const
  { return sliderSetting_ ? sliderSetting_->DisplayValue(number()) : 0.0f; }
  std::optional<bool> value() const
  {
    if (boolean_)
      return boolean_->value();
    if (!choice_)
      return std::nullopt;
    auto selected = choice_->value();
    return selected ? std::optional<bool>(group_->Contains(*selected, index_)) : std::nullopt;
  }
  int selected() const
  {
    // Native selection rows compare one index. For a multi-select, each row
    // independently presents its own index when checked, or -1 when unchecked.
    return choice_ ? (group_->multiple() ? (value().value_or(false) ? index_ : -1) : choice_->value().value_or(-1))
                   : -1;
  }
  Outcome Request(bool desired)
  {
    if (boolean_)
      return boolean_->Request(desired).outcome;
    if (!choice_)
      return Outcome::Rejected;
    if (!group_->multiple())
      return desired ? choice_->Request(index_).outcome : Outcome::Unchanged;
    const auto selected = choice_->value();
    if (!selected)
      return Outcome::Rejected;
    const auto bit = 1 << index_;
    return choice_->Request(desired ? *selected | bit : *selected & ~bit).outcome;
  }
  Outcome Request(float desired)
  {
    if (!slider_ || !enabled() || !std::isfinite(desired) || desired < sliderSetting_->minimum()
        || desired > sliderSetting_->maximum())
      return Outcome::Rejected;
    return slider_->Request(sliderSetting_->Snap(desired)).outcome;
  }
  std::string id() const
  { return boolean_ ? boolean_->setting().id() : choice_ ? group_->item_id(index_) : slider_->setting().id(); }
  std::string label() const
  {
    return boolean_ ? boolean_->setting().label() : choice_ ? group_->labels().at(index_) : slider_->setting().label();
  }
  std::string status_text() const
  {
    return boolean_  ? boolean_->setting().status_text()
           : choice_ ? choice_->setting().status_text()
                     : slider_->setting().status_text();
  }
  std::string presentation_label() const
  {
    auto text = label();
    if (!known())
      text += " — " + std::string(unavailableReason());
    else if (failed())
      text += " — Retry";
    else if (!enabled() && !disabledReason().empty())
      text += " — " + std::string(disabledReason());
    const auto status = status_text();
    if (!status.empty())
      text += "\n<size=75%>" + status + "</size>";
    return text;
  }
  class RenderScope
  {
  public:
    explicit RenderScope(NativeViewState& state)
    {
      if (state.boolean_)
        boolean_.emplace(state.boolean_->setting());
      else if (state.choice_)
        choice_.emplace(state.choice_->setting());
      else
        slider_.emplace(state.slider_->setting());
    }

  private:
    std::optional<BooleanSetting::RenderScope>      boolean_;
    std::optional<ValueSetting<int>::RenderScope>   choice_;
    std::optional<ValueSetting<float>::RenderScope> slider_;
  };

private:
  std::optional<BooleanView>      boolean_;
  std::optional<ValueView<int>>   choice_;
  std::optional<ValueView<float>> slider_;
  SliderSetting*                  sliderSetting_ = nullptr;
  ChoiceSetting*                  group_         = nullptr;
  int                             index_         = 0;
};
} // namespace mod_settings

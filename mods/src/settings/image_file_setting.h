#pragma once
#include "action_setting.h"
#include "image_file_picker.h"
#include "value_settings.h"
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <utility>

namespace mod_settings
{
// The UI thread owns the setting and polls a picker that owns only its result.
// Reset also cancels pending intent when the current value already is default.
class ImageFileSetting
{
public:
  ImageFileSetting(std::shared_ptr<ValueSetting<std::string>> setting, ActionSetting& action)
      : setting_(std::move(setting))
  {
    const auto read = action.read;
    action.read     = [this, read](std::size_t index) {
      auto result    = read(index);
      result.button  = "Choose file";
      result.enabled = result.enabled && !picker_.valid();
      if (!status_.empty())
        result.value += "\n" + status_;
      return result;
    };
    action.invoke = [this](std::size_t) {
      if (picker_.valid())
        return;
      observed_ = setting_->Observe();
      dirty_    = true;
      if (!observed_.state.known())
        return;
      try {
        picker_    = OpenImageFilePicker();
        requested_ = generation_;
        status_    = "Choosing an image...";
      } catch (...) {
        status_ = "Could not open the file picker";
      }
    };
    const auto reset = action.reset;
    action.reset     = [this, reset] {
      ++generation_;
      reset();
    };
    const auto prepare   = action.prepare_reset;
    action.prepare_reset = [this, prepare] {
      auto       plan  = prepare();
      const auto apply = plan.apply;
      plan.apply       = [this, apply] {
        ++generation_;
        return apply && apply();
      };
      return plan;
    };
  }
  bool Poll()
  {
    const bool changed = std::exchange(dirty_, false);
    if (!picker_.valid() || picker_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return changed;
    try {
      const auto path = picker_.get();
      if (path.empty())
        status_ = "Selection canceled";
      else if (requested_ != generation_)
        status_ = "Selection discarded after reset";
      else {
        std::error_code error;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(path), error) || error)
          status_ = "Selected image is unavailable";
        else {
          const auto result = setting_->SetFromUser(path, observed_).outcome;
          status_           = result == Outcome::AppliedVerified || result == Outcome::Unchanged
                                  ? std::string{}
                                  : "Setting changed; choose the image again";
        }
      }
    } catch (...) {
      status_ = "Could not select an image";
    }
    return true;
  }

private:
  std::shared_ptr<ValueSetting<std::string>> setting_;
  ValueSnapshot<std::string>                 observed_;
  std::future<std::string>                   picker_;
  std::string                                status_;
  bool                                       dirty_      = false;
  std::uint64_t                              generation_ = 0, requested_ = 0;
};
} // namespace mod_settings

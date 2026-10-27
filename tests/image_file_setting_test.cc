#include "settings/image_file_setting.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace mod_settings;
std::promise<std::string> picked;
std::future<std::string>  OpenImageFilePicker()
{
  picked = std::promise<std::string>{};
  return picked.get_future();
}
void Check(bool condition)
{
  if (!condition)
    throw std::runtime_error("image picker fixture failed");
}
int main(int argc, char** argv)
{
  Check(argc == 2);
  const auto directory = std::filesystem::path(argv[1]);
  std::filesystem::create_directories(directory);
  const auto image = directory / "loading.png";
  std::ofstream(image) << "existing path boundary fixture";
  std::string                  requested, active;
  int                          writes = 0;
  ValueDefinition<std::string> definition{"image", "Loading image path",
                                          [&] { return ValueReadResult<std::string>::Known(requested, 1); },
                                          [&](std::string value, std::uint64_t) {
                                            requested = std::move(value);
                                            return ApplyResult::Applied;
                                          }};
  definition.default_value = "";
  definition.persist       = [&](const auto&) { ++writes; };
  definition.active_value  = [&] { return std::optional(active); };
  auto          setting    = std::make_shared<ValueSetting<std::string>>(std::move(definition));
  ActionSetting action;
  action.read          = [&](auto) { return ActionSetting::Presentation{"Loading image path", "Edit", requested}; };
  action.reset         = [&] { setting->Reset(setting->Observe()); };
  action.prepare_reset = [&] { return PrepareReset(*setting); };
  ImageFileSetting picker(setting, action);
  action.invoke(0);
  Check(!action.Read(0).actionable());
  Check(picker.Poll()); // Initial choosing status is presented without waiting.
  picked.set_value(image.string());
  Check(picker.Poll() && requested == image.string() && active.empty() && writes == 1);
  Check(action.Read(0).actionable());
  action.invoke(0);
  picked.set_value("");
  picker.Poll();
  Check(requested == image.string() && writes == 1); // Cancel never clears a preference.
  action.invoke(0);
  setting->SetFromUser("other.png", setting->Observe());
  const auto afterEdit = writes;
  picked.set_value(image.string());
  picker.Poll();
  Check(requested == "other.png" && writes == afterEdit); // Stale picker cannot overwrite a later edit.
  action.reset();
  action.invoke(0);
  auto reset = action.prepare_reset();
  Check(reset.current() && reset.apply()); // Even an unchanged-default reset cancels pending intent.
  const auto afterReset = writes;
  picked.set_value(image.string());
  picker.Poll();
  Check(requested.empty() && writes == afterReset);
  action.invoke(0);
  picked.set_value((directory / "missing.png").string());
  picker.Poll();
  Check(requested.empty() && writes == afterReset);
  action.invoke(0);
  picked.set_exception(std::make_exception_ptr(std::runtime_error("dialog failure")));
  picker.Poll();
  Check(action.Read(0).actionable() && writes == afterReset);
  std::filesystem::remove(image);
  std::cout
      << "Image picker: async admission, cancel, stale edit/reset, missing file, failure and restart timing passed\n";
}

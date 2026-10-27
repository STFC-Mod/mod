#include "settings/persisted_setting.h"
#include <cassert>
#include <iostream>
#include <map>

namespace
{
using Key = std::pair<std::string, std::string>;
std::map<Key, unsigned>           registrations;
std::map<Key, config_edit::Value> values;
unsigned                          saves = 0;
} // namespace
namespace runtime_config
{
bool RegisterSetting(const char* section, const char* key) noexcept
{
  ++registrations[{section, key}];
  return true;
}
void SaveSetting(const char* section, const char* key, config_edit::Value value, std::chrono::milliseconds) noexcept
{
  values.insert_or_assign(Key{section, key}, std::move(value));
  ++saves;
}
mod_settings::SaveStatus GetSaveStatus(const char* section, const char* key, const config_edit::Value& current) noexcept
{
  const auto found = values.find(Key{section, key});
  return found != values.end()
             ? mod_settings::SaveStatus{found->second == current ? mod_settings::SaveState::Saved
                                                                 : mod_settings::SaveState::SessionOnly,
                                        saves}
             : mod_settings::SaveStatus{};
}
} // namespace runtime_config

int main()
{
  using namespace mod_settings;
  bool               current = true;
  ValueSetting<bool> setting(PersistedDefinition<bool>(
      {"feature", "Allow feature", [&] { return ValueReadResult<bool>::Known(current, 1); },
       [&](bool value, std::uint64_t) {
         current = value;
         return ApplyResult::Applied;
       }},
      {"ui", "disable_feature", "Description"}, true, [](bool allow) -> config_edit::Value { return !allow; }));
  assert(registrations[Key("ui", "disable_feature")] == 1);
  assert(values.empty() && saves == 0); // Construction registers; it never writes.
  setting.Observe();
  assert(values.empty());
  assert(setting.SetFromUser(false, setting.Observe()).outcome == Outcome::AppliedVerified);
  assert(std::get<bool>(values.at(Key("ui", "disable_feature"))));
  assert(setting.metadata().TomlKey() == "ui.disable_feature");
  assert(setting.save_status().state == SaveState::Saved);
  current = true; // A native mutation before this controller observes is not the acknowledged value.
  assert(setting.save_status().state == SaveState::SessionOnly);
  current = false;
  assert(setting.Reset(setting.Observe()).outcome == Outcome::AppliedVerified);
  assert(!std::get<bool>(values.at(Key("ui", "disable_feature"))));

  int               number = 2;
  ValueSetting<int> numeric(
      PersistedDefinition<int>({"number", "Number", [&] { return ValueReadResult<int>::Known(number, 1); },
                                [&](int value, std::uint64_t) {
                                  number = value;
                                  return ApplyResult::Applied;
                                }},
                               {"control", "number"}, 2, std::chrono::milliseconds(150)));
  assert(numeric.SetFromUser(3, numeric.Observe()).outcome == Outcome::AppliedVerified);
  auto mirrorDefinition =
      PersistedDefinition<int>({"mirror", "Number mirror", [&] { return ValueReadResult<int>::Known(number, 1); },
                                [&](int value, std::uint64_t) {
                                  number = value;
                                  return ApplyResult::Applied;
                                }},
                               {"control", "number"}, 2);
  ValueSetting<int> mirror(std::move(mirrorDefinition));
  assert(mirror.save_status().state == SaveState::Saved);
  number = 4;
  assert(numeric.save_status().state == SaveState::SessionOnly && mirror.save_status().state == SaveState::SessionOnly);
  assert(numeric.status_text() == "Applied for this session; not saved");
  assert(std::get<std::int64_t>(values.at(Key("control", "number"))) == 3);
  assert(registrations.size() == 2); // No independent permitted-key whitelist.
  bool invalid = false;
  try {
    PersistedDefinition<int>({}, {}, 0);
  } catch (const std::invalid_argument&) {
    invalid = true;
  }
  assert(invalid);
  std::cout << "Persisted definition registration/serialization fixtures passed\n";
}

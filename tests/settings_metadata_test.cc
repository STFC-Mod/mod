#include "settings/native_view_state.h"
#include "settings/value_settings.h"
#include <cassert>
#include <iostream>

using namespace mod_settings;

int main()
{
  bool                  requested = false, active = false, available = true, reject = false, uncertain = false;
  unsigned              writes = 0, saves = 0;
  SaveStatus            disk;
  ValueDefinition<bool> definition{
      "test.feature", "Feature",
      [&] { return available ? ValueReadResult<bool>::Known(requested, 1) : ValueReadResult<bool>{}; },
      [&](bool value, std::uint64_t generation) {
        assert(generation == 1);
        ++writes;
        if (reject)
          return ApplyResult::Rejected;
        requested = value;
        if (uncertain)
          available = false;
        return ApplyResult::Applied;
      }};
  definition.metadata      = {"ui", "feature", "A description", "", ApplyTiming::Restart};
  definition.default_value = false;
  definition.persist       = [&](const bool& value) {
    assert(value == requested);
    disk = {SaveState::Pending, ++saves};
  };
  definition.save_status  = [&](bool) { return disk; };
  definition.active_value = [&] { return std::optional<bool>(active); };
  ValueSetting<bool> setting(std::move(definition));
  assert(setting.metadata().TomlKey() == "ui.feature");
  assert(setting.default_value() == false);
  const auto before = setting.Observe();
  setting.Observe();
  assert(writes == 0 && saves == 0 && !setting.restart_pending());
  {
    ValueSetting<bool>::RenderScope rendering(setting);
    assert(setting.Reset(before).outcome == Outcome::Suppressed);
    assert(setting.RetrySave(before).outcome == Outcome::Suppressed);
  }
  assert(saves == 0);
  auto changed = setting.SetFromUser(true, before);
  assert(changed.outcome == Outcome::AppliedVerified && writes == 1 && saves == 1);
  assert(setting.restart_pending() && setting.save_status().state == SaveState::Pending);
  assert(setting.status_text() == "Saving...; restart required");
  assert(setting.Reset(before).outcome == Outcome::Conflict && saves == 1);
  disk.state = SaveState::Conflict;
  assert(setting.save_status().state == SaveState::Conflict);
  assert(setting.status_text() == "Not saved; file changed externally; restart required");
  assert(setting.RetrySave(changed.snapshot).outcome == Outcome::Unchanged);
  assert(writes == 1 && saves == 2 && setting.save_status().state == SaveState::Pending);
  auto reset = setting.Reset(setting.Observe());
  assert(reset.outcome == Outcome::AppliedVerified && !requested && !setting.restart_pending());
  assert(writes == 2 && saves == 3);
  reject = true;
  assert(setting.SetFromUser(true, setting.Observe()).outcome == Outcome::Rejected);
  assert(saves == 3);
  reject    = false;
  uncertain = true;
  assert(setting.SetFromUser(true, setting.Observe()).outcome == Outcome::Unverified);
  assert(saves == 3);                    // Unverified publication must never schedule a save.
  assert(setting.status_text().empty()); // Do not advertise an old save as applied.
  available = true;
  setting.InvalidateSession();
  assert(setting.RetrySave(reset.snapshot).outcome == Outcome::Conflict && saves == 3);

  bool                  value = false;
  ValueDefinition<bool> throwing{"throw", "Throw", [&] { return ValueReadResult<bool>::Known(value, 1); },
                                 [&](bool desired, std::uint64_t) {
                                   value = desired;
                                   return ApplyResult::Applied;
                                 }};
  throwing.persist = [](const bool&) { throw std::runtime_error("persistence"); };
  ValueSetting<bool> second(std::move(throwing));
  assert(second.SetFromUser(true, second.Observe()).outcome == Outcome::AppliedVerified);
  assert(second.save_status().state == SaveState::Unavailable);
  value = false; // A later native change is not the value whose serializer failed.
  assert(second.save_status().state == SaveState::None);
  int                  selected = 0;
  SaveStatus           choiceDisk{SaveState::Pending, 1};
  ValueDefinition<int> choiceDefinition{"choice", "Mode", [&] { return ValueReadResult<int>::Known(selected, 1); },
                                        [&](int desired, std::uint64_t) {
                                          selected = desired;
                                          return ApplyResult::Applied;
                                        }};
  choiceDefinition.save_status = [&](int) { return choiceDisk; };
  ChoiceSetting   choice(std::move(choiceDefinition), {"First", "Second"});
  NativeViewState styled(choice, 0);
  styled.Bind();
  for (const auto status : {SaveState::Pending, SaveState::Saved, SaveState::Conflict, SaveState::Failed,
                            SaveState::Unavailable, SaveState::SessionOnly}) {
    choiceDisk.state = status;
    const auto label = styled.presentation_label();
    assert(label.find(choice.state().status_text()) != std::string::npos);
    assert(label.starts_with("First\n<size=75%>")); // Styled and ordinary adapters use this same text.
  }
  std::cout << "Setting metadata/reset/retry/persistence-readback fixtures passed\n";
}

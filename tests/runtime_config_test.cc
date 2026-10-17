#define CONFIG_RUNTIME_TEST "runtime_config_fixture.h"
#include "../mods/src/patches/parts/runtime_config.cc"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <string_view>
#include <thread>

namespace
{
config_edit::RuntimeConfigWriter fixture;
unsigned                         resumes = 0;
void                             Reset()
{
  fixture                  = {};
  writer                   = &fixture;
  available                = true;
  owner                    = CurrentThreadToken();
  forcing                  = false;
  persistence_unavailable  = false;
  reported_save_failure    = false;
  reported_status_sequence = 0;
  adapter_status_sequence  = 0;
  unavailable_settings.clear();
  save_status_changed     = nullptr;
  fixture_update_callback = nullptr;
  draining = stopped = resume = false;
  vote                        = 0;
  resumes                     = 0;
  request_quit                = [](int) { ++resumes; };
}
} // namespace
int main(int argc, char** argv)
{
  Reset();
#if _WIN32
  if (argc == 2) {
    const std::string_view mode(argv[1]);
    fixture.work         = mode != "idle";
    fixture.block_cancel = mode == "deadline";
    if (mode != "missing-handle")
      fixture.handle = CreateEventW(nullptr, TRUE, mode == "finished", nullptr);
    runtime_config::ForceClose();
    Sleep(10000); // Parent kills this fixture if the independent deadline fails.
    return 9;
  }
#endif
  unsigned         notices     = 0;
  static unsigned* noticeCount = &notices;
  assert(runtime_config::SetSaveStatusObserver([] { ++*noticeCount; }));
  fixture.failures = true;
  std::thread foreignNotice([] { Update(); });
  foreignNotice.join();
  assert(notices == 0 && runtime_config::HasSaveFailures());
  Update();
  Update();
  assert(notices == 1); // One UI callback on a transition, never on each frame.
  fixture.failures = false;
  Update();
  assert(notices == 2 && !runtime_config::HasSaveFailures());
  available               = false;
  writer                  = nullptr; // Configure/Install never supplied the normal update path.
  fixture_update_callback = nullptr;
  assert(runtime_config::SetSaveStatusObserver(save_status_changed));
  assert(fixture_update_callback);
  runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("warp"), {});
  fixture_update_callback();
  assert(notices == 3 && runtime_config::HasSaveFailures());
  available = true;
  writer    = &fixture;
  runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("jump"), {});
  Update();
  assert(notices == 4 && !runtime_config::HasSaveFailures()); // A successful retry clears this key only.
  Reset();
  fixture.failures = true; // Transient thread-start failure is tracked by its key.
  runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("warp"), {});
  assert(runtime_config::HasSaveFailures() && !persistence_unavailable);
  fixture.failures = false;
  runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("jump"), {});
  assert(!runtime_config::HasSaveFailures());
  Reset();
  available = false;
  runtime_config::SaveSetting("ui", "first", true, {});
  runtime_config::SaveSetting("ui", "second", true, {});
  assert(runtime_config::GetSaveStatus("ui", "first", true).state == mod_settings::SaveState::Unavailable);
  assert(runtime_config::GetSaveStatus("ui", "first", false).state == mod_settings::SaveState::SessionOnly);
  available = true;
  runtime_config::SaveSetting("ui", "first", true, {});
  assert(runtime_config::HasSaveFailures()); // Retrying A cannot conceal unavailable B.
  assert(runtime_config::GetSaveStatus("ui", "second", true).state == mod_settings::SaveState::Unavailable);
  runtime_config::SaveSetting("ui", "second", true, {});
  assert(!runtime_config::HasSaveFailures());
  Reset();
  runtime_config::SaveSetting("ui", "mode", std::string("warp"), {});
  assert(runtime_config::GetSaveStatus("ui", "mode", std::string("warp")).state == mod_settings::SaveState::Saved);
  assert(runtime_config::GetSaveStatus("ui", "mode", std::string("jump")).state
         == mod_settings::SaveState::SessionOnly);
  fixture.work = true;
  assert(runtime_config::GetSaveStatus("ui", "mode", std::string("warp")).state == mod_settings::SaveState::Pending);
  assert(runtime_config::GetSaveStatus("ui", "mode", std::string("jump")).state
         == mod_settings::SaveState::SessionOnly);
  fixture.work     = false;
  fixture.failures = true;
  assert(runtime_config::GetSaveStatus("ui", "mode", std::string("warp")).state == mod_settings::SaveState::Failed);
  assert(runtime_config::GetSaveStatus("ui", "mode", std::string("jump")).state
         == mod_settings::SaveState::SessionOnly);
  Reset();
  assert(runtime_config::RegisterSetting("ui", "auto_confirm_instant_warp"));
  assert(!runtime_config::RegisterSetting("", "invalid"));
  assert(WantsQuit([] { return true; }));
  assert(fixture.stopped && stopped && !draining);
  runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("warp"), {});
  assert(fixture.submissions == 0);
  Update();
  assert(resumes == 0);

  Reset();
  fixture.work = true;
  assert(!WantsQuit([] { return false; }));
  assert(!fixture.stopped && !draining);

  Reset();
  fixture.work = true;
  assert(!WantsQuit([] { return true; }));
  assert(fixture.stopped && draining);
  Update();
  assert(resumes == 0);
  fixture.work = false; // Disk work done is not yet native worker termination.
  Update();
  assert(resumes == 0);
  fixture.finished = true;
  std::thread foreign([] {
    Update();
    runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("jump"), {});
  });
  foreign.join();
  assert(resumes == 0 && fixture.submissions == 0);
  request_quit = [](int) {
    ++resumes;
    assert(!WantsQuit([] { return false; })); // A genuine resumed veto is final.
  };
  Update();
  Update();
  assert(resumes == 1);
  runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("warp"), {});
  assert(fixture.submissions == 1 && !stopped && !draining && !fixture.stopped);
  assert(WantsQuit([] { return true; })); // A later explicit quit still works.

  Reset();
  fixture.work = true;
  assert(!WantsQuit([] {
    assert(!WantsQuit([] { return false; }));
    return true; // Older outer vote must not replace the later veto.
  }));
  fixture.finished = true;
  Update();
  assert(resumes == 0);

  runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("jump"), {});
  assert(fixture.submissions == 1 && !draining && !fixture.stopped);

  Reset();
  assert(!WantsQuit([] { return false; }));
  runtime_config::SaveSetting("ui", "auto_confirm_instant_warp", std::string("warp"), {});
  assert(fixture.submissions == 1); // Veto leaves ordinary save admission open.
  std::puts("Native adapter idle/drain/veto/owner fixtures passed");
}

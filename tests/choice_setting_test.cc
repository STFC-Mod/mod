#include "settings/native_boolean_callback.h"
#include "settings/native_view_state.h"
#include "settings/page_catalog.h"
#include <cassert>
#include <iostream>

using namespace mod_settings;
int stored = 0;
int GetInt(Il2CppObject*, const MethodInfo*)
{ return stored; }
void SetInt(Il2CppObject*, int value, const MethodInfo*)
{ stored = value; }
int main()
{
  int             selected = 1, writes = 0;
  ChoiceSetting   setting({"mode", "Mode", [&] { return ValueReadResult<int>::Known(selected, 1); },
                           [&](int value, std::uint64_t) {
                           ++writes;
                           selected = value;
                           return ApplyResult::Applied;
                           }},
                          {"Normal", "Warp", "Jump"});
  NativeViewState normal(setting, 0), jump(setting, 2);
  PageCatalog     catalog("root", "Settings");
  assert(catalog.AddPage("navigation", "Navigation", "root") == Registration::Added);
  assert(catalog.AddChoice("navigation", setting) == Registration::Added);
  assert(catalog.AddChoice("navigation", setting) == Registration::Duplicate);
  const auto plan = catalog.Build();
  assert(plan.size() == 2 && std::get<ChoiceSetting*>(plan[1].items[0]) == &setting);
  assert(catalog.AddChoice("navigation", setting) == Registration::Frozen);
  normal.Bind();
  jump.Bind();
  assert(normal.value() == false && jump.value() == false);
  assert(normal.Request(true) == Outcome::AppliedVerified && selected == 0 && writes == 1);
  // Jump was false before and after. Its whole-value snapshot must still conflict.
  assert(jump.Request(true) == Outcome::Conflict && writes == 1);
  jump.Bind();
  {
    NativeViewState::RenderScope scope(jump);
    assert(jump.Request(true) == Outcome::Suppressed && writes == 1);
  }
  assert(jump.Request(true) == Outcome::AppliedVerified && selected == 2 && writes == 2);
  assert(jump.Request(false) == Outcome::Unchanged && selected == 2 && writes == 2);
  assert(jump.Request(true) == Outcome::Unchanged && writes == 2);
  jump.Unbind();
  assert(jump.Request(true) == Outcome::Rejected && writes == 2);
  auto snapshot = setting.state().Observe();
  assert(setting.state().SetFromUser(3, snapshot).outcome == Outcome::Rejected && writes == 2);
  selected = -1;
  normal.Bind();
  assert(!normal.value() && normal.Request(true) == Outcome::Rejected && writes == 2);

  // Audio has ten built-ins (including Off) plus a retained custom-file slot.
  int           audio_value = 10;
  ChoiceSetting audio(
      {"audio", "Sound", [&] { return ValueReadResult<int>::Known(audio_value, 1); },
       [&](int value, std::uint64_t) {
         audio_value = value;
         return ApplyResult::Applied;
       }},
      {"Off", "Default", "Info", "Success", "Warning", "Alarm", "Arrival", "Soft", "Ping", "Repair", "Custom"});
  PageCatalog audio_pages("audio_root", "Audio");
  assert(audio_pages.AddPage("alert", "Alert", "audio_root") == Registration::Added);
  assert(audio_pages.AddChoice("alert", audio) == Registration::Added);
  assert(audio_pages.Build().at(1).ControlRows() == 11);
  NativeViewState custom(audio, 10), off(audio, 0);
  custom.Bind();
  off.Bind();
  assert(custom.value() == true && off.value() == false);
  assert(off.Request(true) == Outcome::AppliedVerified && audio_value == 0);
  custom.Bind();
  assert(custom.Request(true) == Outcome::AppliedVerified && audio_value == 10);
  assert(audio.state().SetFromUser(11, audio.state().Observe()).outcome == Outcome::Rejected);

  Il2CppType integer{}, nothing{};
  integer.type = IL2CPP_TYPE_I4;
  nothing.type = IL2CPP_TYPE_VOID;
  const Il2CppType* parameters[]{&integer};
  MethodInfo        getter{}, setter{};
  getter.return_type      = &integer;
  setter.return_type      = &nothing;
  setter.parameters       = parameters;
  setter.parameters_count = 1;
  NativeCallback<int>       get;
  NativeCallback<void, int> set;
  assert(get.Initialize(&getter, GetInt) && set.Initialize(&setter, SetInt));
  int   input = 2, output = -1;
  void* args[]{&input};
  set.method()->invoker_method(nullptr, set.method(), nullptr, args, nullptr);
  get.method()->invoker_method(nullptr, get.method(), nullptr, nullptr, &output);
  assert(output == 2);
  std::cout << "PASS choice setting: whole-value conflicts, readback, guards, range and Int32 callbacks\n";
}

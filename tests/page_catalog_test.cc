#include "settings/boolean_view.h"
#include "settings/page_catalog.h"
#include <cassert>
#include <iostream>

using namespace mod_settings;
int main()
{
  unsigned       reads = 0, writes = 0;
  bool           value = false;
  BooleanSetting setting({"mod.example.enabled", "Example",
                          [&] {
                            ++reads;
                            return ReadResult::Known(value, 1);
                          },
                          [&](bool desired, std::uint64_t) {
                            ++writes;
                            value = desired;
                            return ApplyResult::Applied;
                          }});
  PageCatalog    catalog("mod.settings", "Mod Settings");
  assert(catalog.AddPage("group.a", "Group A", "mod.settings") == Registration::Added);
  assert(catalog.AddPage("group.b", "Group B", "mod.settings") == Registration::Added);
  assert(catalog.AddPage("nested", "Nested", "group.a") == Registration::Added);
  assert(catalog.AddPage("empty", "Empty", "mod.settings") == Registration::Added);
  assert(catalog.AddPage("orphan", "Orphan", "missing") == Registration::Invalid);
  assert(catalog.AddPage("group.a", "Duplicate", "mod.settings") == Registration::Duplicate);
  assert(catalog.AddBoolean("nested", setting) == Registration::Added);
  assert(catalog.AddBoolean("nested", setting) == Registration::Duplicate);
  assert(catalog.AddBoolean("group.b", setting) == Registration::Added);
  BooleanSetting collision({"mod.example.enabled", "Other", [] { return ReadResult::Known(true, 1); },
                            [](bool, std::uint64_t) { return ApplyResult::Applied; }});
  assert(catalog.AddBoolean("group.a", collision) == Registration::Invalid);
  auto first = catalog.Build();
  assert(reads == 0 && writes == 0);
  assert(first.size() == 4 && first[0].id == "mod.settings");
  assert(first[3].parent == "group.a");
  first.clear(); // Destroy the old presentation plan before another visit.
  auto second = catalog.Build();
  assert(second.size() == 4 && std::get<BooleanSetting*>(second[3].items[0]) == &setting);
  assert(catalog.AddPage("late", "Late", "mod.settings") == Registration::Frozen);
  assert(catalog.AddBoolean("group.a", setting) == Registration::Frozen);

  BooleanView view(*std::get<BooleanSetting*>(second[3].items[0]));
  view.Bind();
  assert(view.Request(true).outcome == Outcome::AppliedVerified);
  assert(value && writes == 1);
  view.Unbind();
  auto        third = catalog.Build();
  BooleanView mirror(*std::get<BooleanSetting*>(third[2].items[0]));
  mirror.Bind();
  assert(mirror.value() == true && writes == 1);
  assert(view.Request(false).outcome == Outcome::Rejected); // Old page cannot write.

  PageCatalog empty("root", "Empty");
  assert(empty.AddPage("unused", "Unused", "root") == Registration::Added);
  assert(empty.AddHeading("unused", "lonely", "Heading without controls") == Registration::Added);
  assert(empty.Build().empty());
  PageCatalog    malformed("malformed", "Malformed controls");
  BooleanSetting missingId({"", "Label", {}, {}}), missingLabel({"id", "", {}, {}});
  assert(malformed.AddBoolean("malformed", missingId) == Registration::Invalid);
  assert(malformed.AddBoolean("malformed", missingLabel) == Registration::Invalid);
  for (bool emptyId : {true, false}) {
    SliderSetting invalid({emptyId ? "" : "slider", emptyId ? "Label" : "",
                           [] { return ValueReadResult<float>::Known(0.5f, 1); },
                           [](float, std::uint64_t) { return ApplyResult::Applied; }},
                          0, 1, 0.01f, [] { return true; });
    assert(malformed.AddSlider("malformed", invalid) == Registration::Invalid);
  }
  assert(malformed.Build().empty());
  ChoiceSetting player({"player", "Player detail", [] { return ValueReadResult<int>::Known(0, 1); },
                        [](int, std::uint64_t) { return ApplyResult::Applied; }},
                       {"Native", "Expanded", "Compact", "Threshold"});
  ChoiceSetting other({"other", "Other detail", [] { return ValueReadResult<int>::Known(0, 1); },
                       [](int, std::uint64_t) { return ApplyResult::Applied; }},
                      {"Native", "Expanded", "Compact", "Threshold"});
  SliderSetting playerSlider({"player.zoom", "Threshold", [] { return ValueReadResult<float>::Known(0.5f, 1); },
                              [](float, std::uint64_t) { return ApplyResult::Applied; }},
                             0, 1, 0.01f, [] { return true; });
  SliderSetting otherSlider({"other.zoom", "Threshold", [] { return ValueReadResult<float>::Known(0.5f, 1); },
                             [](float, std::uint64_t) { return ApplyResult::Applied; }},
                            0, 1, 0.01f, [] { return true; });
  // Command registration/building is presentation-only: no invocation or read.
  int           commandCalls = 0;
  std::size_t   commandCount = 2;
  ActionSetting command{"record", "Record",
                        [&](std::size_t) {
                          ++commandCalls;
                          return ActionSetting::Presentation{};
                        },
                        [&](std::size_t) { ++commandCalls; },
                        [&] {
                          ++commandCalls;
                          return commandCount;
                        }};
  PageCatalog   commands("commands", "Commands");
  assert(commands.AddAction("commands", command) == Registration::Added);
  assert(commands.AddAction("commands", command) == Registration::Duplicate);
  const auto commandPlan = commands.Build();
  assert(commandPlan.size() == 1 && commandPlan.front().ControlRows() == 1 && commandCalls == 0);
  assert(commands.AddAction("commands", command) == Registration::Frozen);
  // Registration counts definitions without evaluating dynamic rows. Repeated
  // native identities survive shrink/regrowth and reject noncanonical suffixes.
  assert(command.item_index(command.item_id(42)) == 42);
  for (const auto* id : {"record", "record.row.", "record.row.-1", "record.row.01", "record.row.1x", "other.row.1",
                         "record.row.999999999999999999999999999999999999"})
    assert(!command.item_index(id));
  command.read = [&](std::size_t index) {
    ++commandCalls;
    return ActionSetting::Presentation{std::to_string(index), "Change", "", true, true};
  };
  assert(command.Read(1).actionable());
  commandCount          = 1;
  const auto beforeRead = commandCalls;
  assert(!command.Read(1).visible && commandCalls == beforeRead + 1); // Count only; no stale reader call.
  assert(!command.Read(1).actionable());
  commandCount = 2;
  assert(command.Read(1).label == "1");
  auto presentation    = command.Read(0);
  presentation.visible = false;
  assert(!presentation.actionable());
  presentation.visible = true;
  presentation.button.clear();
  assert(!presentation.actionable()); // Information rows do not expose a blank button.
  presentation.button  = "Change";
  presentation.enabled = false;
  assert(!presentation.actionable());
  assert(commandPlan[0].PositionFor(command.id()) == 0);
  assert(commandPlan[0].PositionFor("unknown") == commandPlan[0].items.size());
  PageCatalog combined("labels", "Fleet Labels");
  assert(combined.AddHeading("labels", "player.heading", "Player", true) == Registration::Added);
  assert(combined.AddChoice("labels", player) == Registration::Added);
  assert(combined.AddSlider("labels", playerSlider) == Registration::Added);
  assert(combined.AddHeading("labels", "other.heading", "Non-player", true) == Registration::Added);
  assert(combined.AddChoice("labels", other) == Registration::Added);
  assert(combined.AddSlider("labels", otherSlider) == Registration::Added);
  const auto ordered = combined.Build();
  assert(ordered.size() == 1 && ordered[0].ControlRows() == 10 && ordered[0].items.size() == 6);
  assert(std::get<PageCatalog::Heading>(ordered[0].items[0]).label == "Player");
  assert(std::get<ChoiceSetting*>(ordered[0].items[1]) == &player);
  assert(std::get<SliderSetting*>(ordered[0].items[2]) == &playerSlider);
  assert(std::get<PageCatalog::Heading>(ordered[0].items[3]).label == "Non-player");
  assert(std::get<ChoiceSetting*>(ordered[0].items[4]) == &other);
  assert(std::get<SliderSetting*>(ordered[0].items[5]) == &otherSlider);
  assert(ordered[0].SectionFor("player")->id == "player.heading");
  assert(ordered[0].SectionFor("player.zoom")->id == "player.heading");
  assert(ordered[0].SectionFor("other")->id == "other.heading");
  assert(ordered[0].SectionFor("other.zoom")->id == "other.heading");
  assert(!ordered[0].SectionFor("player.heading")); // Headers always remain visible.
  assert(!ordered[0].SectionFor("unknown"));
  PageCatalog boundaries("boundaries", "Section boundaries");
  assert(boundaries.AddHeading("boundaries", "boundaries", "Collision", true) == Registration::Invalid);
  assert(boundaries.AddChoice("boundaries", player) == Registration::Added);
  assert(boundaries.AddHeading("boundaries", "collapsible", "Collapsible", true) == Registration::Added);
  assert(boundaries.AddPage("collapsible", "Collision", "boundaries") == Registration::Invalid);
  assert(boundaries.AddSlider("boundaries", playerSlider) == Registration::Added);
  assert(boundaries.AddHeading("boundaries", "plain", "Plain") == Registration::Added);
  assert(boundaries.AddSlider("boundaries", otherSlider) == Registration::Added);
  const auto bounded = boundaries.Build();
  assert(!bounded[0].SectionFor("player")); // Controls before a heading are unaffected.
  assert(bounded[0].SectionFor("player.zoom")->id == "collapsible");
  assert(!bounded[0].SectionFor("other.zoom")); // A plain heading ends a collapsible section.
  bool           masterOn = false, masterAvailable = true;
  int            visibilityReads = 0;
  BooleanSetting master({"master",
                         "Master",
                         [&] {
                           ++visibilityReads;
                           return masterAvailable ? ReadResult::Known(masterOn, 1) : ReadResult{};
                         },
                         {}});
  BooleanSetting dependent({"dependent", "Dependent", [] { return ReadResult::Known(true, 1); }, {}});
  PageCatalog    conditional("conditional", "Conditional sections");
  assert(conditional.AddBoolean("conditional", master) == Registration::Added);
  assert(conditional.AddBoolean("conditional", dependent, [&] {
    const auto state = master.Observe().state;
    return state.known() && *state.value;
  }) == Registration::Added);
  assert(conditional.AddHeading("conditional", "targets", "Targets", false, [&] {
    const auto state = master.Observe().state;
    return state.known() && *state.value;
  }) == Registration::Added);
  assert(conditional.AddBoolean("conditional", setting) == Registration::Added);
  assert(conditional.AddHeading("conditional", "later", "Later section") == Registration::Added);
  assert(conditional.AddSlider("conditional", playerSlider) == Registration::Added);
  const auto  conditionalPlan = conditional.Build();
  const auto& page            = conditionalPlan.front();
  assert(visibilityReads == 0); // Building retains hidden controls without reading their dependency.
  assert(page.HasConditionalSections() && !bounded[0].HasConditionalSections());
  const auto writesBeforeVisibility = writes;
  for (bool enabled : {false, true, false, true}) {
    masterOn = enabled; // A live change from either UI or shortcut uses the same reader.
    assert(page.IsVisible("master"));
    assert(page.IsVisible("dependent") == enabled);
    assert(page.IsVisible("targets") == enabled);
    assert(page.IsVisible(setting.id()) == enabled);
    assert(page.IsVisible("later") && page.IsVisible(playerSlider.state().id()));
    assert(value && writes == writesBeforeVisibility); // Hiding/revealing never clears a target preference.
  }
  masterAvailable = false;
  assert(!page.IsVisible("dependent"));
  assert(!page.IsVisible("targets") && !page.IsVisible(setting.id()));
  assert(page.IsVisible("master") && page.IsVisible("unknown"));
  bool        rejected = false;
  std::thread wrong_thread([&] {
    try {
      (void)catalog.Build();
    } catch (const std::logic_error&) {
      rejected = true;
    }
  });
  wrong_thread.join();
  assert(rejected);
  std::cout << "Settings catalog rebuild/identity/lifetime fixtures passed\n";
}

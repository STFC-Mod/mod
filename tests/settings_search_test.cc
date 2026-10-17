#include "settings/settings_search.h"
#include <cassert>
#include <iostream>
using namespace mod_settings;
int main()
{
  int        reads = 0, writes = 0;
  const auto definition = [&](std::string id, std::string label, SettingMetadata metadata) {
    Definition result{std::move(id), std::move(label),
                      [&] {
                        ++reads;
                        return ReadResult::Known(true, 1);
                      },
                      [&](bool, std::uint64_t) {
                        ++writes;
                        return ApplyResult::Applied;
                      }};
    result.metadata = std::move(metadata);
    return result;
  };
  BooleanSetting cargo(
      definition("legacy.presentation.counter", "Instant ship cargo counter",
                 {"ui", "instant_cargo_counter", "Remove the animation delay from the capacity counter"}));
  BooleanSetting hidden(definition("anything", "Hostile cargo", {"ui", "show_hostile_cargo"}));
  BooleanSetting labels(
      definition("legacy.presentation.player", "Player label visibility", {"graphics", "zoom_label_player_detail"}));
  BooleanSetting native(definition("native.preference", "Native preference", {}));
  ActionSetting  exact{"exact", "Precise counter", [](std::size_t) { return ActionSetting::Presentation{}; },
                       [](std::size_t) {}};
  exact.metadata = &cargo.metadata();
  ActionSetting         change{"shortcut.change", "Change", [](std::size_t) { return ActionSetting::Presentation{}; },
                               [](std::size_t) {}};
  ActionSetting         remove{"shortcut.remove", "Remove", [](std::size_t) { return ActionSetting::Presentation{}; },
                               [](std::size_t) {}};
  const SettingMetadata shortcutMetadata{"shortcuts", "move_left", "Move the camera to the left"};
  PageCatalog           pages("community_mod.settings", "Mod Settings");
  pages.AddPage("community_mod.previews", "Previews & Cargo", "community_mod.settings");
  pages.AddHeading("community_mod.previews", "cargo.targets", "Target types", false, [] { return false; });
  pages.AddBoolean("community_mod.previews", hidden);
  pages.AddBoolean("community_mod.previews", cargo);
  pages.AddAction("community_mod.previews", exact);
  pages.AddBoolean("community_mod.previews", native);
  pages.AddPage("community_mod.labels", "Fleet Labels", "community_mod.settings");
  pages.AddHeading("community_mod.labels", "player", "Player", true);
  pages.AddBoolean("community_mod.labels", labels);
  pages.AddPage("community_mod.shortcuts", "Shortcuts", "community_mod.settings");
  pages.AddPage("community_mod.shortcuts.camera", "Camera", "community_mod.shortcuts");
  pages.AddPage("community_mod.shortcuts.move_left", "Pan left", "community_mod.shortcuts.camera");
  pages.SetMetadata("community_mod.shortcuts.move_left", shortcutMetadata);
  pages.AddAction("community_mod.shortcuts.move_left", change);
  pages.AddAction("community_mod.shortcuts.move_left", remove);
  SettingsSearch search;
  const auto     plan = pages.Build();
  search.Build(plan);
  assert(search.Find("").empty());
  assert(search.Find("   ").empty());
  assert(search.Find("no such thing").empty());
  assert(search.Find("UI.INSTANT_CARGO_COUNTER").size() == 1);
  assert(search.Find("instant cargo").front()->item == cargo.id());
  assert(search.Find("capacity animation").front()->item == cargo.id());
  assert(search.Find("zoom_label_player_detail").front()->item == labels.id());
  assert(search.Find("fleet player visibility").front()->item == labels.id());
  assert(search.Find("hostile cargo").front()->item == hidden.id()); // Discoverable while its parent is off.
  assert(search.Find("shortcuts move_left").size() == 1);            // One hit per model, never per editor command.
  assert(search.Find("pan left").front()->key == "shortcuts.move_left");
  assert(search.Find("pan left").front()->location == "Shortcuts > Camera > Pan left");
  assert(std::ranges::none_of(search.Find("Remove"), [](const auto* entry) { return entry->label == "Remove"; }));
  assert(search.Find("Native preference").size() == 1);
  assert(search.Find("legacy.presentation.counter").empty()); // Keys come from definitions, never guessed from IDs.
  assert(reads == 0 && writes == 0);
  std::cout << "Settings search tests passed\n";
}

#include "settings/page_sections.h"
#include <cassert>
#include <iostream>

using namespace mod_settings;

// Run the same visit contract for the fleet and galaxy page shapes.
void CheckPage(bool galaxy)
{
  bool overlays    = true;
  int  writes      = 0;
  auto rejectWrite = [&](auto, std::uint64_t) {
    ++writes;
    return ApplyResult::Rejected;
  };
  BooleanSetting master({"master", "Multiple overlays", [&] { return ReadResult::Known(overlays, 1); },
                         [&](bool, std::uint64_t) {
                           ++writes;
                           return ApplyResult::Rejected;
                         }});
  BooleanSetting overlay({"overlay", "Overlay", [] { return ReadResult::Known(true, 1); }, rejectWrite});
  ChoiceSetting  first({"first", "First", [] { return ValueReadResult<int>::Known(1, 1); }, rejectWrite},
                       {"Native", "Always", "Threshold"});
  ChoiceSetting  second({"second", "Second", [] { return ValueReadResult<int>::Known(2, 1); }, rejectWrite},
                        {"Native", "Always", "Threshold"});
  PageCatalog    catalog("labels", galaxy ? "Galaxy Labels" : "Fleet Labels");
  if (galaxy) {
    catalog.AddBoolean("labels", master);
    catalog.AddHeading("labels", "overlays", "Overlays", false, [&] { return overlays; });
    catalog.AddBoolean("labels", overlay);
  }
  catalog.AddHeading("labels", "first.heading", galaxy ? "Major systems" : "Player", true);
  catalog.AddChoice("labels", first);
  catalog.AddHeading("labels", "second.heading", galaxy ? "Minor systems" : "Non-player", true);
  catalog.AddChoice("labels", second);
  const auto   pages         = catalog.Build();
  const auto&  page          = pages.front();
  const auto&  firstHeading  = *page.SectionFor("first");
  const auto&  secondHeading = *page.SectionFor("second");
  PageSections sections;
  for (int visit = 0; visit < 2; ++visit) {
    sections.Begin(page);
    assert(sections.Visible(page, "first.heading") && sections.Visible(page, "second.heading"));
    assert(!sections.Visible(page, "first") && !sections.Visible(page, "second"));
    if (galaxy) {
      assert(sections.Visible(page, "master") && sections.Visible(page, "overlays"));
      assert(page.SectionFor("overlay") == nullptr);
      assert(sections.Visible(page, "overlay"));
      overlays = false;
      assert(!sections.Visible(page, "overlays") && !sections.Visible(page, "overlay"));
      assert(sections.Visible(page, "master") && sections.Visible(page, "first.heading"));
      overlays = true;
      assert(sections.Visible(page, "overlay")); // Direct controls return without an expansion click.
    }
    sections.Toggle(firstHeading);
    assert(sections.Visible(page, "first") && !sections.Visible(page, "second"));
    const auto beforeFailedBind = sections.Snapshot();
    sections.Toggle(secondHeading);
    sections.Restore(beforeFailedBind);
    assert(sections.Visible(page, "first") && !sections.Visible(page, "second"));
    sections.Toggle(secondHeading);
    assert(sections.Visible(page, "first") && sections.Visible(page, "second"));
    sections.Toggle(firstHeading);
    assert(!sections.Visible(page, "first") && sections.Visible(page, "second"));
    // Leave one section open; the next Begin must reset the whole visit.
  }
  sections.ExpandAll(); // Existing accessible fallback after a failed folded bind.
  assert(sections.Visible(page, "first") && sections.Visible(page, "second"));
  assert(writes == 0);
}

int main()
{
  CheckPage(false);
  CheckPage(true);
  PageSections sections;
  assert(!sections.Refreshing());
  {
    PageSections::RefreshScope initialPopulation(sections);
    assert(sections.Refreshing());
    try {
      PageSections::RefreshScope nested(sections);
      assert(sections.Refreshing());
      throw 1;
    } catch (int) {
    }
    assert(sections.Refreshing()); // Nested exit must not expose the unfinished outer bind.
  }
  assert(!sections.Refreshing());
  std::cout << "Shared fleet/galaxy section visit and refresh fixtures passed\n";
}

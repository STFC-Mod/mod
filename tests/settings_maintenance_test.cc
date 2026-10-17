#include "settings/maintenance.h"
#include <cassert>
#include <iostream>

namespace
{
std::optional<mod_settings::TextEntryRequest> editor;
}
namespace mod_settings
{
bool OpenTextEntry(TextEntryRequest request)
{
  editor = std::move(request);
  return true;
}
} // namespace mod_settings

int main()
{
  using namespace mod_settings;
  unsigned              writes = 0, saves = 0;
  bool                  enabled = true, available = true;
  bool                  numeric_available = true;
  float                 number            = 0.75f;
  SaveStatus            boolean_save, numeric_save;
  ValueDefinition<bool> boolean{"feature", "Feature",
                                [&] { return available ? ReadResult::Known(enabled, 1) : ReadResult{}; },
                                [&](bool value, std::uint64_t) {
                                  ++writes;
                                  enabled = value;
                                  return ApplyResult::Applied;
                                }};
  boolean.metadata      = {"ui", "enabled", "Feature help"};
  boolean.default_value = false;
  boolean.persist       = [&](const bool&) {
    ++saves;
    boolean_save.state = SaveState::Pending;
  };
  boolean.save_status = [&](bool) { return boolean_save; };
  BooleanSetting         toggle(std::move(boolean));
  ValueDefinition<float> numeric{
      "number", "Number",
      [&] { return numeric_available ? ValueReadResult<float>::Known(number, 1) : ValueReadResult<float>{}; },
      [&](float value, std::uint64_t) {
        ++writes;
        number = value;
        return ApplyResult::Applied;
      }};
  numeric.metadata      = {"graphics", "threshold", "Number help", "ratio"};
  numeric.default_value = 0.5f;
  numeric.persist       = [&](const float&) {
    ++saves;
    numeric_save.state = SaveState::Pending;
  };
  numeric.save_status = [&](float) { return numeric_save; };
  ValueSetting<float> canonical(numeric);
  SliderSetting       slider(std::move(numeric), 0, 1, 0.01f, [] { return true; });

  ActionSetting exact{"number.exact", "Exact number",
                      [](std::size_t) { return ActionSetting::Presentation{"Exact number", "Edit", ""}; },
                      [](std::size_t) {}};
  exact.metadata      = &canonical.metadata();
  exact.reset         = [&] { canonical.Reset(canonical.Observe()); };
  exact.prepare_reset = [&] { return PrepareReset(canonical); };
  exact.retry         = [&] { canonical.RetrySave(canonical.Observe()); };
  exact.default_label = [] { return "0.5"; };
  exact.status        = [&] { return canonical.status_text(); };
  exact.save_status   = [&] { return canonical.save_status(); };
  PageCatalog catalog("community_mod.settings", "Mod Settings");
  assert(catalog.AddPage("page", "Page", "community_mod.settings") == Registration::Added);
  catalog.AddBoolean("page", toggle);
  catalog.AddSlider("page", slider);
  catalog.AddAction("page", exact); // Both representations describe one saved setting.
  RegisterMaintenancePages(catalog);
  auto       pages = catalog.Build();
  const auto page  = std::ranges::find(pages, "page.defaults", &PageCatalog::Page::id);
  assert(page != pages.end());
  assert(page->items.size() == 5); // Page reset + two (details, retry) pairs.
  assert(writes == 0 && saves == 0);
  for (const auto& item : page->items)
    std::get<ActionSetting*>(item)->Read(0);
  assert(writes == 0 && saves == 0); // Browsing details/status never normalizes config.

  auto* page_reset = std::get<ActionSetting*>(page->items[0]);
  available        = false;
  page_reset->invoke(0);
  assert(editor->help.find("1 available setting(s)") != std::string::npos);
  assert(editor->help.find("1 unavailable setting(s)") != std::string::npos);
  assert(editor->commit("") && enabled && number == 0.5f && writes == 1 && saves == 1);
  numeric_available = false;
  page_reset->invoke(0);
  assert(!editor->commit("") && writes == 1 && saves == 1); // Nothing available is not a successful reset.
  numeric_available = true;
  available         = true;
  number            = 0.75f;
  writes = saves = 0;
  numeric_save   = {};
  page_reset->invoke(0);
  assert(editor && writes == 0 && saves == 0);
  // A value changing while confirmation is open invalidates the whole page;
  // the other setting must not be reset first.
  number = 0.9f;
  assert(!editor->commit("") && enabled && number == 0.9f && writes == 0 && saves == 0);
  page_reset->invoke(0);
  available = false;
  assert(!editor->commit("") && writes == 0 && saves == 0);
  available = true;
  page_reset->invoke(0);
  toggle.InvalidateSession();
  assert(!editor->commit("") && writes == 0 && saves == 0);
  page_reset->invoke(0);
  assert(editor->commit("") && !enabled && number == 0.5f && writes == 2 && saves == 2);

  auto* details = std::get<ActionSetting*>(page->items[1]);
  enabled       = true;
  details->invoke(0);
  assert(editor->help.find("ui.enabled") != std::string::npos);
  assert(editor->help.find("Default: Off") != std::string::npos);
  toggle.InvalidateSession();
  assert(!editor->commit("") && enabled && writes == 2);

  auto* boolean_retry = std::get<ActionSetting*>(page->items[2]);
  auto* numeric_retry = std::get<ActionSetting*>(page->items[4]);
  boolean_save.state  = SaveState::Failed;
  numeric_save.state  = SaveState::Saved;
  assert(boolean_retry->Read(0).visible && !numeric_retry->Read(0).visible);
  boolean_retry->invoke(0);
  assert(saves == 3 && writes == 2); // A retry persists this key without replaying application.
  assert(boolean_save.state == SaveState::Pending && numeric_save.state == SaveState::Saved);
  assert(!boolean_retry->Read(0).visible);
  number = 2.0f; // Valid canonical value outside the convenience slider's range.
  assert(!slider.state().Observe().state.known() && canonical.Observe().state.known());
  auto* numeric_details = std::get<ActionSetting*>(page->items[3]);
  numeric_details->invoke(0);
  assert(editor->commit("") && number == 0.5f && writes == 3 && saves == 4);
  boolean_save.state = SaveState::SessionOnly;
  assert(boolean_retry->Read(0).visible && boolean_retry->Read(0).button == "Save current value");
  boolean_retry->invoke(0);
  assert(saves == 5 && writes == 3 && boolean_save.state == SaveState::Pending);
  std::cout << "Maintenance browse/dedup/reset/stale-session/per-key retry fixtures passed\n";
}

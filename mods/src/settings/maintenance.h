#pragma once

#include "page_catalog.h"
#include "text_entry.h"
#include <memory>
#include <set>

namespace mod_settings
{
// A companion page keeps help/default commands separate from the normal native
// controls. Every command refers back to its model, including exact-entry rows.
inline void RegisterMaintenancePages(PageCatalog& catalog)
{
  struct Control {
    const SettingMetadata*                 metadata;
    std::string                            label;
    std::function<PreparedSettingAction()> prepare_reset;
    std::function<void()>                  retry;
    std::function<std::string()>           status, default_label;
    std::function<SaveStatus()>            save_status;
  };
  static std::vector<std::shared_ptr<ActionSetting>> commands;
  const auto                                         pages = catalog.RegisteredPages();
  for (const auto& page : pages) {
    if (page.metadata)
      continue; // Shortcut pages own their draft/default workflow.
    auto                  controls = std::make_shared<std::vector<Control>>();
    std::set<std::string> seen;
    for (const auto& item : page.items)
      std::visit(
          [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (!std::is_same_v<T, PageCatalog::Heading>) {
              Control control;
              if constexpr (std::is_same_v<T, ActionSetting*>) {
                if (!value->metadata || !value->prepare_reset)
                  return;
                control = {value->metadata, value->label,         value->prepare_reset, value->retry,
                           value->status,   value->default_label, value->save_status};
              } else {
                auto* setting = [&] {
                  if constexpr (std::is_same_v<T, BooleanSetting*>)
                    return value;
                  else
                    return &value->state();
                }();
                if (!setting->default_value())
                  return;
                control = {&setting->metadata(),
                           setting->label(),
                           [setting] { return PrepareReset(*setting); },
                           [setting] { setting->RetrySave(setting->Observe()); },
                           [setting] { return setting->status_text(); },
                           [setting, value] {
                             const auto& defaultValue = *setting->default_value();
                             if constexpr (std::is_same_v<T, BooleanSetting*>)
                               return std::string(defaultValue ? "On" : "Off");
                             else if constexpr (std::is_same_v<T, ChoiceSetting*>)
                               return value->labels().at(defaultValue);
                             else
                               return std::to_string(defaultValue);
                           },
                           [setting] { return setting->save_status(); }};
              }
              const auto key = control.metadata->TomlKey();
              if (!key.empty()) {
                if (seen.insert(key).second)
                  controls->push_back(std::move(control));
                else if constexpr (std::is_same_v<T, ActionSetting*>) {
                  // An exact editor owns the full configured range. Prefer its
                  // canonical model over a convenience slider, in either order.
                  const auto existing = std::ranges::find_if(
                      *controls, [&](const Control& entry) { return entry.metadata->TomlKey() == key; });
                  if (existing != controls->end())
                    *existing = std::move(control);
                }
              }
            }
          },
          item);
    if (controls->empty())
      continue;
    const auto id = page.id + ".defaults";
    catalog.AddPage(id, "Defaults & Help", page.id);
    const auto add = [&](std::shared_ptr<ActionSetting> action) {
      catalog.AddAction(id, *action);
      commands.push_back(std::move(action));
    };
    auto all      = std::make_shared<ActionSetting>();
    all->identity = id + ".reset";
    all->label    = "Restore page defaults";
    all->read     = [label = page.label](std::size_t) {
      return ActionSetting::Presentation{"Restore page defaults", "Review", label};
    };
    all->invoke = [controls, label = page.label](std::size_t) {
      std::vector<PreparedSettingAction> plans;
      std::size_t                        skipped = 0;
      for (const auto& control : *controls) {
        auto plan = control.prepare_reset();
        if (plan.current && plan.current())
          plans.push_back(std::move(plan));
        else
          ++skipped;
      }
      auto help = "Reset " + std::to_string(plans.size()) + " available setting(s) on this page.";
      if (skipped)
        help += " " + std::to_string(skipped) + " unavailable setting(s) will be skipped.";
      help += " Each change is saved to TOML. Included settings must remain available and unchanged while this "
              "confirmation is open. Restart settings take effect after restarting the client.";
      OpenTextEntry({.title = "Restore " + label + " defaults",
                     .value = "",
                     .help  = std::move(help),
                     .commit =
                         [plans = std::move(plans)](std::string_view) {
                           if (plans.empty())
                             return false;
                           for (const auto& plan : plans)
                             if (!plan.current || !plan.current())
                               return false;
                           for (const auto& plan : plans)
                             if (!plan.apply || !plan.apply())
                               return false;
                           return true;
                         },
                     .read_only   = true,
                     .apply_label = "Reset page"});
    };
    add(std::move(all));
    for (const auto& control : *controls) {
      auto details      = std::make_shared<ActionSetting>();
      details->identity = id + "." + control.metadata->section + "." + control.metadata->key;
      details->label    = control.label;
      details->read     = [control](std::size_t) {
        return ActionSetting::Presentation{control.label, "Details", control.status ? control.status() : ""};
      };
      details->invoke = [control](std::size_t) {
        auto        plan         = control.prepare_reset();
        std::string help         = control.metadata->TomlKey() + "\n" + control.metadata->description;
        const auto  defaultValue = control.default_label ? control.default_label() : std::string{};
        help += "\nDefault: " + (defaultValue.empty() ? std::string("(empty)") : defaultValue);
        if (control.metadata->timing == ApplyTiming::Restart)
          help += "\nTakes effect after restarting the client.";
        else if (control.metadata->timing == ApplyTiming::ReopenView)
          help += "\nReopen the affected screen after changing this setting.";
        else
          help += "\nTakes effect immediately.";
        OpenTextEntry(
            {.title  = control.label,
             .value  = "",
             .help   = std::move(help),
             .commit = [plan = std::move(plan)](
                           std::string_view) { return plan.current && plan.current() && plan.apply && plan.apply(); },
             .read_only   = true,
             .apply_label = "Reset default"});
      };
      add(std::move(details));
      auto retry      = std::make_shared<ActionSetting>();
      retry->identity = id + "." + control.metadata->section + "." + control.metadata->key + ".retry";
      retry->label    = control.label + " save";
      retry->read     = [control](std::size_t) {
        const auto status = control.status ? control.status() : std::string{};
        const auto state  = control.save_status ? control.save_status().state : SaveState::None;
        const bool failed = state == SaveState::Conflict || state == SaveState::Failed
                            || state == SaveState::Unavailable || state == SaveState::SessionOnly;
        return ActionSetting::Presentation{control.label,
                                           state == SaveState::SessionOnly ? "Save current value" : "Retry save",
                                           status, !!control.retry, failed};
      };
      retry->invoke = [control](std::size_t) {
        if (control.retry)
          control.retry();
      };
      add(std::move(retry));
    }
  }
}
} // namespace mod_settings

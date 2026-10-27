#pragma once

#include "action_setting.h"
#include "boolean_settings.h"
#include "choice_setting.h"
#include "slider_setting.h"
#include <algorithm>
#include <functional>
#include <map>
#include <ranges>
#include <string_view>
#include <variant>
#include <vector>

namespace mod_settings
{
// Presentation only. Settings keep their identity, live state and persistence.
class PageCatalog
{
public:
  // Shared with the native adapter's child-list sanity check. Presentation
  // producers must fit this budget without limiting the underlying config.
  static constexpr int NativeChildLimit = 128;
  struct Heading {
    std::string id, label;
    bool        collapsible = false;
    // Optional presentation dependency, including the heading and all following
    // controls up to the next heading. It never changes their saved values.
    std::function<bool()>        visible;
    std::function<std::string()> summary;
  };
  using Item = std::variant<Heading, BooleanSetting*, ChoiceSetting*, SliderSetting*, ActionSetting*>;
  struct Page {
    std::string       id, label, parent;
    std::vector<Item> items; // Registration order is visual order, including headings.
    std::map<std::string, std::function<bool()>, std::less<>> itemVisibility;
    // Page departure owns draft/capture cancellation, never a pooled row release.
    std::function<void()>        leave;
    std::function<std::string()> summary;
    const SettingMetadata*       metadata = nullptr;
    bool                         HasConditionalSections() const
    {
      if (!itemVisibility.empty())
        return true;
      return std::any_of(items.begin(), items.end(), [](const Item& item) {
        const auto* heading = std::get_if<Heading>(&item);
        return heading && static_cast<bool>(heading->visible);
      });
    }
    bool IsVisible(std::string_view id) const
    {
      if (const auto found = itemVisibility.find(id); found != itemVisibility.end() && !found->second())
        return false;
      const Heading* section = nullptr;
      for (const auto& item : items) {
        if (const auto* heading = std::get_if<Heading>(&item))
          section = heading;
        if (Id(item) == id)
          return !section || !section->visible || section->visible();
      }
      return true;
    }
    std::size_t PositionFor(std::string_view id) const
    {
      for (std::size_t i = 0; i < items.size(); ++i)
        if (Id(items[i]) == id)
          return i;
      return items.size();
    }
    // A heading owns following controls up to the next heading. Collapse is
    // presentation state only; this lookup never reads or writes a setting.
    const Heading* SectionFor(std::string_view setting_id) const
    {
      const Heading* section = nullptr;
      for (const auto& item : items) {
        if (const auto* heading = std::get_if<Heading>(&item))
          section = heading->collapsible ? heading : nullptr;
        else if (Id(item) == setting_id)
          return section;
      }
      return nullptr;
    }
    template <typename T> auto Controls() const
    {
      return items | std::views::filter([](const Item& item) { return std::holds_alternative<T*>(item); })
             | std::views::transform([](const Item& item) { return std::get<T*>(item); });
    }
    std::size_t ControlRows() const
    {
      std::size_t count = 0;
      for (const auto& item : items)
        std::visit(
            [&](const auto& value) {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<T, ChoiceSetting*>)
                count += value->labels().size();
              else if constexpr (!std::is_same_v<T, Heading>)
                ++count;
            },
            item);
      return count;
    }
  };

  explicit PageCatalog(std::string root_id, std::string root_label)
  {
    if (root_id.empty() || root_label.empty())
      throw std::invalid_argument("settings root identity");
    pages_.push_back({std::move(root_id), std::move(root_label), {}, {}});
  }
  PageCatalog(const PageCatalog&)                        = delete;
  PageCatalog&             operator=(const PageCatalog&) = delete;
  const std::vector<Page>& RegisteredPages() const
  {
    CheckThread();
    return pages_;
  }

  Registration AddPage(std::string id, std::string label, std::string_view parent)
  {
    CheckThread();
    if (frozen_)
      return Registration::Frozen;
    if (id.empty() || label.empty() || !FindPage(parent))
      return Registration::Invalid;
    if (FindPage(id))
      return Registration::Duplicate;
    for (const auto& page : pages_)
      for (const auto& item : page.items)
        if (const auto* heading = std::get_if<Heading>(&item); heading && heading->collapsible && heading->id == id)
          return Registration::Invalid;
    // Parents already exist: no orphan or cyclic registrations.
    pages_.push_back({std::move(id), std::move(label), std::string(parent), {}});
    return Registration::Added;
  }
  Registration AddBoolean(std::string_view page, BooleanSetting& setting, std::function<bool()> visible = {})
  { return AddControl(page, setting, std::move(visible)); }
  Registration OnLeave(std::string_view id, std::function<void()> callback)
  {
    CheckThread();
    if (frozen_)
      return Registration::Frozen;
    auto* page = FindPage(id);
    if (!page)
      return Registration::Invalid;
    page->leave = std::move(callback);
    return Registration::Added;
  }
  Registration SetSummary(std::string_view id, std::function<std::string()> callback)
  {
    CheckThread();
    if (frozen_)
      return Registration::Frozen;
    auto* page = FindPage(id);
    if (!page)
      return Registration::Invalid;
    page->summary = std::move(callback);
    return Registration::Added;
  }
  Registration SetMetadata(std::string_view id, const SettingMetadata& metadata)
  {
    CheckThread();
    if (frozen_)
      return Registration::Frozen;
    auto* page = FindPage(id);
    if (!page)
      return Registration::Invalid;
    page->metadata = &metadata;
    return Registration::Added;
  }
  Registration AddChoice(std::string_view page, ChoiceSetting& setting)
  { return AddControl(page, setting); }
  Registration AddSlider(std::string_view page, SliderSetting& setting)
  { return AddControl(page, setting); }
  Registration AddAction(std::string_view page, ActionSetting& action)
  { return AddControl(page, action); }
  Registration AddHeading(std::string_view page_id, std::string id, std::string label, bool collapsible = false,
                          std::function<bool()> visible = {}, std::function<std::string()> summary = {})
  {
    CheckThread();
    if (frozen_)
      return Registration::Frozen;
    auto* page = FindPage(page_id);
    if (!page || id.empty() || label.empty())
      return Registration::Invalid;
    if (collapsible && FindPage(id))
      return Registration::Invalid; // Both use native category contexts; identities must be distinct.
    for (const auto& existing : pages_)
      for (const auto& item : existing.items)
        if (Id(item) == id)
          return Registration::Duplicate;
    page->items.emplace_back(
        Heading{std::move(id), std::move(label), collapsible, std::move(visible), std::move(summary)});
    return Registration::Added;
  }

  // Freeze one immutable plan. Each native settings context gets fresh objects.
  // Heading-only pages are empty; building never reads or writes a setting.
  std::vector<Page> Build()
  {
    CheckThread();
    frozen_     = true;
    auto result = pages_;
    for (std::size_t i = result.size(); i-- > 0;) {
      if (result[i].ControlRows())
        continue;
      const bool has_child = std::any_of(result.begin() + i + 1, result.end(),
                                         [&](const Page& page) { return page.parent == result[i].id; });
      if (!has_child)
        result.erase(result.begin() + i);
    }
    return result;
  }

private:
  static const std::string& Id(const Item& item)
  {
    return std::visit(
        [](const auto& value) -> const std::string& {
          using T = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<T, Heading>)
            return value.id;
          else if constexpr (std::is_same_v<T, BooleanSetting*> || std::is_same_v<T, ActionSetting*>)
            return value->id();
          else
            return value->state().id();
        },
        item);
  }
  template <typename T>
  Registration AddControl(std::string_view page_id, T& setting, std::function<bool()> visible = {})
  {
    CheckThread();
    if (frozen_)
      return Registration::Frozen;
    auto* page = FindPage(page_id);
    if (!page)
      return Registration::Invalid;
    const auto& state = [&]() -> const auto& {
      if constexpr (std::is_same_v<T, BooleanSetting> || std::is_same_v<T, ActionSetting>)
        return setting;
      else
        return setting.state();
    }();
    const auto& label = [&]() -> const std::string& {
      if constexpr (std::is_same_v<T, ActionSetting>)
        return state.label;
      else
        return state.label();
    }();
    if (state.id().empty() || label.empty())
      return Registration::Invalid;
    const Item candidate = &setting;
    for (const auto& existing : pages_)
      for (const auto& item : existing.items) {
        if (Id(item) != Id(candidate))
          continue;
        auto* owner = std::get_if<T*>(&item);
        if (!owner || *owner != &setting)
          return Registration::Invalid;
        if (existing.id == page_id)
          return Registration::Duplicate;
      }
    page->items.push_back(candidate);
    if (visible)
      page->itemVisibility.emplace(state.id(), std::move(visible));
    return Registration::Added;
  }
  Page* FindPage(std::string_view id)
  {
    auto found = std::find_if(pages_.begin(), pages_.end(), [&](const Page& page) { return page.id == id; });
    return found == pages_.end() ? nullptr : &*found;
  }
  void CheckThread() const
  {
    if (std::this_thread::get_id() != thread_)
      throw std::logic_error("settings catalog thread mismatch");
  }
  const std::thread::id thread_ = std::this_thread::get_id();
  bool                  frozen_ = false;
  std::vector<Page>     pages_;
};
} // namespace mod_settings

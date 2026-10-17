#pragma once

#include "page_catalog.h"
#include <utility>

namespace mod_settings
{
// Visit-local presentation shared by every settings page. Native widget and
// context lifetimes remain with the native adapter.
class PageSections
{
public:
  class RefreshScope
  {
  public:
    explicit RefreshScope(PageSections& owner)
        : owner_(owner)
        , previous_(std::exchange(owner.refreshing_, true))
    {
    }
    ~RefreshScope()
    { owner_.refreshing_ = previous_; }
    RefreshScope(const RefreshScope&)            = delete;
    RefreshScope& operator=(const RefreshScope&) = delete;

  private:
    PageSections& owner_;
    bool          previous_;
  };

  void Begin(const PageCatalog::Page& page)
  {
    ExpandAll();
    for (const auto& item : page.items)
      if (const auto* heading = std::get_if<PageCatalog::Heading>(&item); heading && heading->collapsible)
        collapsed_.push_back(heading->id);
  }
  void ExpandAll()
  { collapsed_.clear(); }
  auto Snapshot() const
  { return collapsed_; }
  void Restore(std::vector<std::string> collapsed)
  { collapsed_ = std::move(collapsed); }
  bool Refreshing() const
  { return refreshing_; }
  bool Collapsed(const PageCatalog::Heading& heading) const
  { return std::find(collapsed_.begin(), collapsed_.end(), heading.id) != collapsed_.end(); }
  void Toggle(const PageCatalog::Heading& heading)
  {
    if (!heading.collapsible)
      return;
    if (Collapsed(heading))
      std::erase(collapsed_, heading.id);
    else
      collapsed_.push_back(heading.id);
  }
  bool Visible(const PageCatalog::Page& page, std::string_view id) const
  {
    const auto* section = page.SectionFor(id);
    return page.IsVisible(id) && (!section || !Collapsed(*section));
  }

private:
  std::vector<std::string> collapsed_;
  bool                     refreshing_ = false;
};
} // namespace mod_settings

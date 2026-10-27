#pragma once
#include "page_catalog.h"
#include <cctype>
#include <sstream>

namespace mod_settings
{
inline std::string SearchWords(std::string_view text)
{
  std::string result;
  for (unsigned char c : text)
    // TOML underscores, dots and UI spaces should be interchangeable. Preserve
    // UTF-8 bytes; current catalog labels/keys use ASCII for searchable words.
    result += c == '_' || c == '.' || c == '-' ? ' ' : static_cast<char>(std::tolower(c));
  return result;
}
struct SettingsSearchEntry {
  std::string page, item, label, location, key, words;
};
class SettingsSearch
{
public:
  static constexpr std::size_t ResultLimit = 32;
  static constexpr int         QueryLimit  = 128;
  void                         Build(const std::vector<PageCatalog::Page>& pages)
  {
    entries_.clear();
    const auto path = [&](const PageCatalog::Page& page) {
      std::string text   = page.label;
      auto        parent = page.parent;
      for (std::size_t depth = 0; depth < pages.size() && parent != "community_mod.settings"; ++depth) {
        auto it = std::ranges::find(pages, parent, &PageCatalog::Page::id);
        if (it == pages.end())
          break;
        text   = it->label + " > " + text;
        parent = it->parent;
      }
      return text;
    };
    for (const auto& page : pages) {
      const auto location = path(page);
      // Action-only editors expose their one model explicitly.
      if (page.metadata) {
        Add(page.id, {}, page.label, location, page.metadata->TomlKey(), page.metadata->description);
        continue;
      }
      std::vector<std::string> seen;
      std::string              section;
      for (const auto& item : page.items)
        std::visit(
            [&](const auto& value) {
              using T = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<T, PageCatalog::Heading>) {
                section = value.label;
              } else {
                const auto& setting = [&]() -> const auto& {
                  if constexpr (std::is_same_v<T, BooleanSetting*> || std::is_same_v<T, ActionSetting*>)
                    return *value;
                  else
                    return value->state();
                }();
                const SettingMetadata* metadata = nullptr;
                std::string            label;
                if constexpr (std::is_same_v<T, ActionSetting*>) {
                  metadata = setting.metadata;
                  label    = setting.label;
                  if (!metadata)
                    return;
                } else {
                  metadata = &setting.metadata();
                  label    = setting.label();
                }
                const auto key = metadata->TomlKey();
                if (!key.empty()) {
                  if (std::ranges::find(seen, key) != seen.end())
                    return;
                  seen.push_back(key);
                }
                Add(page.id, setting.id(), label, location + (section.empty() ? "" : " > " + section), key,
                    metadata->description);
              }
            },
            item);
    }
  }
  std::vector<const SettingsSearchEntry*> Find(std::string_view query) const
  {
    std::vector<std::string> tokens;
    std::istringstream       input(SearchWords(query.substr(0, QueryLimit)));
    for (std::string token; input >> token;)
      tokens.push_back(std::move(token));
    std::vector<const SettingsSearchEntry*> result;
    if (tokens.empty())
      return result;
    for (const auto& entry : entries_)
      if (std::ranges::all_of(tokens, [&](const auto& token) { return entry.words.find(token) != std::string::npos; }))
        result.push_back(&entry);
    // Exact key/name matches precede broad matches while registration order
    // remains stable within each group.
    const auto exact = SearchWords(query);
    std::stable_partition(result.begin(), result.end(), [&](auto* entry) {
      const auto key = entry->key.substr(entry->key.find('.') + 1);
      return SearchWords(entry->key) == exact || SearchWords(key) == exact || SearchWords(entry->label) == exact;
    });
    return result;
  }

private:
  void Add(std::string page, std::string item, std::string label, std::string location, std::string key,
           std::string_view description = {})
  {
    auto words = SearchWords(label + " " + key + " " + location + " " + std::string(description));
    entries_.push_back(
        {std::move(page), std::move(item), std::move(label), std::move(location), std::move(key), std::move(words)});
  }
  std::vector<SettingsSearchEntry> entries_;
};
} // namespace mod_settings

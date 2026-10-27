#pragma once

#include "page_catalog.h"

namespace mod_settings
{
// Register only features present in the upstream mod. The catalog owns its
// adapters for the process lifetime; browsing it never changes configuration.
void RegisterUpstreamSettings(PageCatalog& catalog);
// Save changes made by an existing shortcut or native pin action through the
// same field serializer. Returns false for unknown or unavailable controls.
bool PersistCurrentSetting(std::string_view section, std::string_view key);
} // namespace mod_settings

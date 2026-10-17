#pragma once
#include "page_catalog.h"

namespace mod_settings
{
// Register intentional feature adapters before native settings installation.
// The empty catalog installs no extra navigation hooks and shows no empty menu.
PageCatalog& ModPages();
void         RegisterModPages();
} // namespace mod_settings

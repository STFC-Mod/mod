#pragma once
namespace mod_settings
{
class PageCatalog;
void RegisterShortcutPages(PageCatalog& catalog);
void SetShortcutPresentationObserver(void (*observer)());
bool ShortcutHintControlAvailable();
} // namespace mod_settings

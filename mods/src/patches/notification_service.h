#pragma once

struct Toast;

// Initialize the notification platform (WinRT on Windows).
// Call once during InstallToastBannerHooks().
void notification_init();

// Called from toast hooks — checks config, formats, and delivers notification.
void notification_handle_toast(Toast* toast);

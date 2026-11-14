#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

enum class NotificationSound : uint8_t {
  kNone = 0,
  kDefault,
  kInfo,
  kSuccess,
  kWarning,
  kAlarm,
  kArrival,
  kSoft,
  kPing,
  kRepair,
  kCount,
};

[[nodiscard]] std::string_view                 NotificationSoundName(NotificationSound sound);
[[nodiscard]] std::optional<NotificationSound> ParseNotificationSound(std::string_view name);
// Generated mono PCM WAV bytes, cached for the process lifetime. None and invalid sounds have no data.
std::span<const uint8_t> NotificationSoundBytes(NotificationSound sound);
void                     PlayNotificationSound(NotificationSound sound);

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

enum class NotificationSound : uint8_t {
  None = 0,
  Default,
  Info,
  Success,
  Warning,
  Alarm,
  Arrival,
  Soft,
  Ping,
  Repair,
  Count,
};

[[nodiscard]] std::string_view                 notification_sound_name(NotificationSound sound);
[[nodiscard]] std::optional<NotificationSound> notification_sound_from_name(std::string_view name);
void                                           notification_audio_play(NotificationSound sound);

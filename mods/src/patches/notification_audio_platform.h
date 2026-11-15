#pragma once

#include <cstddef>
#include <cstdint>

[[nodiscard]] bool notification_audio_platform_play(const uint8_t* data, size_t size);

#pragma once

// The notification cues toasts play for [audio] alert_*: tones generated as WAV data and played without the game.

#include "config.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Mono 16-bit PCM WAV data for `sound`, generated on first use and kept for the process: playback is asynchronous
// and reads the buffer after the call returns. None has no data.
std::span<const uint8_t> NotificationSoundBytes(NotificationSound sound);
void                     PlayNotificationSound(NotificationSound sound);

// Starts asynchronous playback of WAV data that outlives it. Windows plays it in notification_audio.cc, macOS in
// notification_audio_mac.mm; elsewhere nothing plays.
bool PlayNotificationBuffer(const uint8_t* data, size_t size);

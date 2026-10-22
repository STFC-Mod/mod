// Notification cues: their tone patterns, the WAV data generated from them, and Windows playback.

#include "features/notification_audio.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <numbers>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#if _WIN32
#include <Windows.h>
#include <mmsystem.h>
#endif

namespace
{
struct ToneSegment {
  double frequency_hz;
  int    duration_ms;
};
// Entries follow NotificationSound; unused segments have zero duration. kNone has no tones.
constexpr std::array<ToneSegment, 5> kPatterns[] = {
    {},
    {{{740, 70}, {0, 22}, {880, 85}, {0, 18}}},
    {{{659, 80}, {0, 24}, {880, 110}}},
    {{{587, 70}, {0, 18}, {740, 70}, {0, 18}, {988, 120}}},
    {{{622, 90}, {0, 36}, {466, 110}, {0, 28}, {466, 90}}},
    {{{880, 90}, {0, 42}, {880, 90}, {0, 42}, {698, 160}}},
    {{{523, 65}, {0, 18}, {659, 70}, {0, 18}, {1046, 125}}},
    {{{523, 90}, {0, 26}, {659, 110}}},
    {{{1046, 95}}},
    {{{440, 70}, {0, 18}, {554, 70}, {0, 18}, {740, 140}}},
};
static_assert(std::size(kPatterns) == std::to_underlying(NotificationSound::kCount));

constexpr int    kSampleRate = 44100;
constexpr double kTwoPi      = 2 * std::numbers::pi;
// Peak level, about -12 dBFS.
constexpr double kAmplitude = 0.26;

int SegmentSamples(const ToneSegment& segment)
{ return static_cast<int>((static_cast<int64_t>(segment.duration_ms) * kSampleRate) / 1000); }

// RIFF stores integers little-endian.
void AppendU16(std::vector<uint8_t>& buffer, uint16_t value)
{ buffer.insert(buffer.end(), {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)}); }

void AppendU32(std::vector<uint8_t>& buffer, uint32_t value)
{
  AppendU16(buffer, static_cast<uint16_t>(value));
  AppendU16(buffer, static_cast<uint16_t>(value >> 16));
}

void AppendAscii(std::vector<uint8_t>& buffer, std::string_view value)
{ buffer.insert(buffer.end(), value.begin(), value.end()); }

// A canonical 44-byte RIFF header (one PCM fmt chunk, then data) followed by the samples.
std::vector<uint8_t> BuildWav(std::span<const ToneSegment> pattern)
{
  uint32_t sample_count = 0;
  for (const auto& segment : pattern)
    sample_count += static_cast<uint32_t>(SegmentSamples(segment));

  constexpr uint16_t channels        = 1;
  constexpr uint16_t bits_per_sample = 16;
  const uint32_t     data_bytes      = sample_count * channels * (bits_per_sample / 8);

  std::vector<uint8_t> buffer;
  buffer.reserve(44 + data_bytes);
  AppendAscii(buffer, "RIFF");
  AppendU32(buffer, 36 + data_bytes); // everything after this field
  AppendAscii(buffer, "WAVE");
  AppendAscii(buffer, "fmt ");
  AppendU32(buffer, 16); // fmt chunk size
  AppendU16(buffer, 1);  // PCM
  AppendU16(buffer, channels);
  AppendU32(buffer, kSampleRate);
  AppendU32(buffer, kSampleRate * channels * (bits_per_sample / 8)); // bytes per second
  AppendU16(buffer, channels * (bits_per_sample / 8));               // bytes per frame
  AppendU16(buffer, bits_per_sample);
  AppendAscii(buffer, "data");
  AppendU32(buffer, data_bytes);

  // A frequency of 0 is a rest. Each tone fades in over 80 samples and out over 120 (2-3 ms), which removes the
  // click a hard edge would make.
  double phase = 0.0;
  for (const auto& segment : pattern) {
    const auto segment_samples = SegmentSamples(segment);
    const bool rest            = segment.frequency_hz <= 0.0;
    const auto step            = rest ? 0.0 : kTwoPi * segment.frequency_hz / kSampleRate;
    for (int index = 0; index < segment_samples; ++index) {
      const auto fade_in  = std::min(1.0, index / 80.0);
      const auto fade_out = std::min(1.0, (segment_samples - index) / 120.0);
      const auto sample   = rest ? 0.0 : std::sin(phase) * kAmplitude * fade_in * fade_out;
      AppendU16(buffer, static_cast<uint16_t>(static_cast<int16_t>(sample * 32767.0)));
      phase += step;
      if (phase > kTwoPi)
        phase -= kTwoPi;
    }
  }
  return buffer;
}
} // namespace

std::span<const uint8_t> NotificationSoundBytes(NotificationSound sound)
{
  const size_t index = std::to_underlying(sound);
  if (index == 0 || index >= std::size(kPatterns))
    return {};
  static std::array<std::once_flag, std::size(kPatterns)>       ready;
  static std::array<std::vector<uint8_t>, std::size(kPatterns)> buffers;
  // Only the cues in use are generated.
  std::call_once(ready[index], [index] { buffers[index] = BuildWav(kPatterns[index]); });
  return buffers[index];
}

void PlayNotificationSound(NotificationSound sound)
{
  const auto buffer = NotificationSoundBytes(sound);
  if (!buffer.empty() && !PlayNotificationBuffer(buffer.data(), buffer.size()))
    spdlog::warn("[Toasts] could not play the {} cue", NotificationSoundName(sound));
}

#if _WIN32
bool PlayNotificationBuffer(const uint8_t* data, size_t)
{ return PlaySoundW(reinterpret_cast<LPCWSTR>(data), nullptr, SND_ASYNC | SND_MEMORY | SND_NODEFAULT) != FALSE; }
#elif !defined(__APPLE__)
bool PlayNotificationBuffer(const uint8_t*, size_t)
{ return false; }
#endif

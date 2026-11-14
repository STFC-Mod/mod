// Named notification cues and their generated mono PCM WAV data.
#include "features/notification_audio.h"
#include "text.h"

#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
#include <mutex>
#include <vector>

#if _WIN32
#include <Windows.h>
#include <mmsystem.h>
#endif

bool PlayNotificationBuffer(const uint8_t* data, size_t size);

namespace
{
struct ToneSegment {
  double frequency_hz;
  int    duration_ms;
};
struct SoundDefinition {
  std::string_view           name;
  std::array<ToneSegment, 5> pattern;
};

// Entries follow NotificationSound; unused segments have zero duration.
constexpr SoundDefinition kSounds[] = {
    {"none", {}},
    {"default", {{{740, 70}, {0, 22}, {880, 85}, {0, 18}}}},
    {"info", {{{659, 80}, {0, 24}, {880, 110}}}},
    {"success", {{{587, 70}, {0, 18}, {740, 70}, {0, 18}, {988, 120}}}},
    {"warning", {{{622, 90}, {0, 36}, {466, 110}, {0, 28}, {466, 90}}}},
    {"alarm", {{{880, 90}, {0, 42}, {880, 90}, {0, 42}, {698, 160}}}},
    {"arrival", {{{523, 65}, {0, 18}, {659, 70}, {0, 18}, {1046, 125}}}},
    {"soft", {{{523, 90}, {0, 26}, {659, 110}}}},
    {"ping", {{{1046, 95}}}},
    {"repair", {{{440, 70}, {0, 18}, {554, 70}, {0, 18}, {740, 140}}}},
};
static_assert(std::size(kSounds) == static_cast<size_t>(NotificationSound::kCount));

constexpr std::pair<std::string_view, NotificationSound> kAliases[] = {
    {"off", NotificationSound::kNone},        {"silent", NotificationSound::kNone},
    {"victory", NotificationSound::kSuccess}, {"warn", NotificationSound::kWarning},
    {"attack", NotificationSound::kAlarm},    {"arrive", NotificationSound::kArrival},
    {"quiet", NotificationSound::kSoft},      {"repaired", NotificationSound::kRepair},
};

constexpr int    kSampleRate = 44100;
constexpr double kTwoPi      = 6.28318530717958647692;
constexpr double kAmplitude  = 0.26;

void append_u16(std::vector<uint8_t>& buffer, uint16_t value)
{
  buffer.push_back(static_cast<uint8_t>(value & 0xff));
  buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<uint8_t>& buffer, uint32_t value)
{
  buffer.push_back(static_cast<uint8_t>(value & 0xff));
  buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
  buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
  buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void append_ascii(std::vector<uint8_t>& buffer, std::string_view value)
{ buffer.insert(buffer.end(), value.begin(), value.end()); }

std::vector<uint8_t> build_wav(std::span<const ToneSegment> pattern)
{
  uint32_t sample_count = 0;
  for (const auto& segment : pattern) {
    sample_count += static_cast<uint32_t>((static_cast<int64_t>(segment.duration_ms) * kSampleRate) / 1000);
  }

  constexpr uint16_t channels        = 1;
  constexpr uint16_t bits_per_sample = 16;
  const uint32_t     data_bytes      = sample_count * channels * (bits_per_sample / 8);

  std::vector<uint8_t> buffer;
  buffer.reserve(44 + data_bytes);
  append_ascii(buffer, "RIFF");
  append_u32(buffer, 36 + data_bytes);
  append_ascii(buffer, "WAVE");
  append_ascii(buffer, "fmt ");
  append_u32(buffer, 16);
  append_u16(buffer, 1);
  append_u16(buffer, channels);
  append_u32(buffer, kSampleRate);
  append_u32(buffer, kSampleRate * channels * (bits_per_sample / 8));
  append_u16(buffer, channels * (bits_per_sample / 8));
  append_u16(buffer, bits_per_sample);
  append_ascii(buffer, "data");
  append_u32(buffer, data_bytes);

  double phase = 0.0;
  for (const auto& segment : pattern) {
    const auto segment_samples = static_cast<int>((static_cast<int64_t>(segment.duration_ms) * kSampleRate) / 1000);
    const auto step            = segment.frequency_hz > 0.0 ? kTwoPi * segment.frequency_hz / kSampleRate : 0.0;
    for (int index = 0; index < segment_samples; ++index) {
      const auto fade_in  = std::min(1.0, static_cast<double>(index) / 80.0);
      const auto fade_out = std::min(1.0, static_cast<double>(segment_samples - index) / 120.0);
      const auto sample   = segment.frequency_hz > 0.0 ? std::sin(phase) * kAmplitude * fade_in * fade_out : 0.0;
      append_u16(buffer, static_cast<uint16_t>(static_cast<int16_t>(sample * 32767.0)));
      phase += step;
      if (phase > kTwoPi) {
        phase -= kTwoPi;
      }
    }
  }
  return buffer;
}

} // namespace

std::string_view NotificationSoundName(NotificationSound sound)
{
  const auto index = static_cast<size_t>(sound);
  return index < std::size(kSounds) ? kSounds[index].name : "none";
}

std::optional<NotificationSound> ParseNotificationSound(std::string_view name)
{
  const auto normalized = AsciiStrToLower(name);
  for (size_t i = 0; i < std::size(kSounds); ++i)
    if (kSounds[i].name == normalized)
      return static_cast<NotificationSound>(i);
  for (const auto& [alias, sound] : kAliases)
    if (alias == normalized)
      return sound;
  return std::nullopt;
}

std::span<const uint8_t> NotificationSoundBytes(NotificationSound sound)
{
  const auto index = static_cast<size_t>(sound);
  if (index == 0 || index >= std::size(kSounds))
    return {};
  static std::array<std::once_flag, std::size(kSounds)>       ready;
  static std::array<std::vector<uint8_t>, std::size(kSounds)> buffers;
  // Generate only the requested cue, once. Buffers stay alive while asynchronous playback uses them.
  std::call_once(ready[index], [index] { buffers[index] = build_wav(kSounds[index].pattern); });
  return buffers[index];
}

void PlayNotificationSound(NotificationSound sound)
{
  const auto buffer = NotificationSoundBytes(sound);
  if (!buffer.empty() && !PlayNotificationBuffer(buffer.data(), buffer.size()))
    spdlog::warn("[NotifyAudio] Failed to play '{}' cue", NotificationSoundName(sound));
}

#if _WIN32
bool PlayNotificationBuffer(const uint8_t* data, size_t)
{ return PlaySoundW(reinterpret_cast<LPCWSTR>(data), nullptr, SND_ASYNC | SND_MEMORY | SND_NODEFAULT) != FALSE; }
#elif !defined(__APPLE__)
bool PlayNotificationBuffer(const uint8_t*, size_t)
{ return false; }
#endif

#include "patches/notification_audio.h"

#include "patches/notification_audio_platform.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
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

constexpr int    kSampleRate = 44100;
constexpr double kTwoPi      = 6.28318530717958647692;
constexpr double kAmplitude  = 0.26;

constexpr std::array<ToneSegment, 4> kDefaultPattern{{{740.0, 70}, {0.0, 22}, {880.0, 85}, {0.0, 18}}};
constexpr std::array<ToneSegment, 3> kInfoPattern{{{659.0, 80}, {0.0, 24}, {880.0, 110}}};
constexpr std::array<ToneSegment, 5> kSuccessPattern{{{587.0, 70}, {0.0, 18}, {740.0, 70}, {0.0, 18}, {988.0, 120}}};
constexpr std::array<ToneSegment, 5> kWarningPattern{{{622.0, 90}, {0.0, 36}, {466.0, 110}, {0.0, 28}, {466.0, 90}}};
constexpr std::array<ToneSegment, 5> kAlarmPattern{{{880.0, 90}, {0.0, 42}, {880.0, 90}, {0.0, 42}, {698.0, 160}}};
constexpr std::array<ToneSegment, 5> kArrivalPattern{{{523.0, 65}, {0.0, 18}, {659.0, 70}, {0.0, 18}, {1046.0, 125}}};
constexpr std::array<ToneSegment, 3> kSoftPattern{{{523.0, 90}, {0.0, 26}, {659.0, 110}}};
constexpr std::array<ToneSegment, 1> kPingPattern{{{1046.0, 95}}};
constexpr std::array<ToneSegment, 5> kRepairPattern{{{440.0, 70}, {0.0, 18}, {554.0, 70}, {0.0, 18}, {740.0, 140}}};

struct SoundDefinition {
  NotificationSound            sound;
  std::string_view             name;
  std::span<const ToneSegment> pattern;
};

constexpr auto kSoundDefinitions = std::to_array<SoundDefinition>({
    {NotificationSound::None, "none", {}},
    {NotificationSound::Default, "default", kDefaultPattern},
    {NotificationSound::Info, "info", kInfoPattern},
    {NotificationSound::Success, "success", kSuccessPattern},
    {NotificationSound::Warning, "warning", kWarningPattern},
    {NotificationSound::Alarm, "alarm", kAlarmPattern},
    {NotificationSound::Arrival, "arrival", kArrivalPattern},
    {NotificationSound::Soft, "soft", kSoftPattern},
    {NotificationSound::Ping, "ping", kPingPattern},
    {NotificationSound::Repair, "repair", kRepairPattern},
});

struct SoundAlias {
  std::string_view  name;
  NotificationSound sound;
};

constexpr auto kSoundAliases = std::to_array<SoundAlias>({
    {"off", NotificationSound::None},
    {"silent", NotificationSound::None},
    {"victory", NotificationSound::Success},
    {"warn", NotificationSound::Warning},
    {"attack", NotificationSound::Alarm},
    {"arrive", NotificationSound::Arrival},
    {"quiet", NotificationSound::Soft},
    {"repaired", NotificationSound::Repair},
});

consteval bool SoundCatalogIsValid()
{
  if (kSoundDefinitions.size() != static_cast<size_t>(NotificationSound::Count)) {
    return false;
  }

  for (size_t index = 0; index < kSoundDefinitions.size(); ++index) {
    const auto& definition = kSoundDefinitions[index];
    if (static_cast<size_t>(definition.sound) != index || definition.name.empty()
        || (definition.sound == NotificationSound::None) != definition.pattern.empty()) {
      return false;
    }
    for (size_t other = index + 1; other < kSoundDefinitions.size(); ++other) {
      if (definition.name == kSoundDefinitions[other].name) {
        return false;
      }
    }
  }

  for (size_t index = 0; index < kSoundAliases.size(); ++index) {
    const auto& alias = kSoundAliases[index];
    if (alias.name.empty() || static_cast<size_t>(alias.sound) >= kSoundDefinitions.size()) {
      return false;
    }
    for (const auto& definition : kSoundDefinitions) {
      if (alias.name == definition.name) {
        return false;
      }
    }
    for (size_t other = index + 1; other < kSoundAliases.size(); ++other) {
      if (alias.name == kSoundAliases[other].name) {
        return false;
      }
    }
  }
  return true;
}

static_assert(SoundCatalogIsValid());

constexpr const SoundDefinition* sound_definition(NotificationSound sound)
{
  const auto index = static_cast<size_t>(sound);
  if (index >= kSoundDefinitions.size() || kSoundDefinitions[index].sound != sound) {
    return nullptr;
  }
  return &kSoundDefinitions[index];
}

static_assert(sound_definition(NotificationSound::Success)->name == "success");
static_assert(sound_definition(NotificationSound::Count) == nullptr);

std::string normalize_sound_name(std::string_view value)
{
  std::string normalized;
  normalized.reserve(value.size());
  for (const auto ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      normalized.push_back(static_cast<char>(ch - 'A' + 'a'));
    } else if (ch == '-' || ch == ' ') {
      normalized.push_back('_');
    } else {
      normalized.push_back(ch);
    }
  }
  return normalized;
}

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

using SoundBuffers = std::array<std::vector<uint8_t>, static_cast<size_t>(NotificationSound::Count)>;

const SoundBuffers& sound_buffers()
{
  static const auto buffers = [] {
    SoundBuffers result;
    for (const auto& definition : kSoundDefinitions) {
      if (!definition.pattern.empty()) {
        result[static_cast<size_t>(definition.sound)] = build_wav(definition.pattern);
      }
    }
    return result;
  }();
  return buffers;
}
} // namespace

std::string_view notification_sound_name(NotificationSound sound)
{
  const auto* definition = sound_definition(sound);
  return definition ? definition->name : kSoundDefinitions.front().name;
}

std::optional<NotificationSound> notification_sound_from_name(std::string_view name)
{
  const auto normalized = normalize_sound_name(name);
  if (const auto definition = std::ranges::find(kSoundDefinitions, normalized, &SoundDefinition::name);
      definition != kSoundDefinitions.end()) {
    return definition->sound;
  }
  if (const auto alias = std::ranges::find(kSoundAliases, normalized, &SoundAlias::name);
      alias != kSoundAliases.end()) {
    return alias->sound;
  }
  return std::nullopt;
}

void notification_audio_play(NotificationSound sound)
{
  if (sound == NotificationSound::None)
    return;

  const auto index = static_cast<size_t>(sound);
  if (index >= kSoundDefinitions.size())
    return;

  const auto& buffer = sound_buffers()[index];
  if (buffer.empty())
    return;

  if (!notification_audio_platform_play(buffer.data(), buffer.size())) {
    spdlog::warn("[NotifyAudio] Failed to play '{}' cue", notification_sound_name(sound));
  }
}

#if _WIN32
bool notification_audio_platform_play(const uint8_t* data, size_t)
{ return PlaySoundW(reinterpret_cast<LPCWSTR>(data), nullptr, SND_ASYNC | SND_MEMORY | SND_NODEFAULT) != FALSE; }
#elif !defined(__APPLE__)
bool notification_audio_platform_play(const uint8_t*, size_t)
{ return false; }
#endif

#pragma once

#include "patches/runtime_config.h"
#include "value_settings.h"
#include <concepts>
#include <type_traits>
#include <utility>

namespace mod_settings
{
// Persistence is attached to the same definition used by rendering, search,
// defaults and application. No second list of permitted save keys is required.
template <typename T, typename Serializer>
  requires std::invocable<Serializer, const T&>
ValueDefinition<T> PersistedDefinition(ValueDefinition<T> definition, SettingMetadata metadata, T default_value,
                                       Serializer serialize, std::chrono::milliseconds delay = {})
{
  if (!metadata.persisted())
    throw std::invalid_argument("persisted setting requires a section and key");
  runtime_config::RegisterSetting(metadata.section.c_str(), metadata.key.c_str());
  definition.metadata      = std::move(metadata);
  definition.default_value = std::move(default_value);
  const auto section       = definition.metadata.section;
  const auto key           = definition.metadata.key;
  definition.persist       = [section, key, serialize, delay](const T& value) {
    runtime_config::SaveSetting(section.c_str(), key.c_str(), serialize(value), delay);
  };
  definition.save_status = [section, key, serialize = std::move(serialize)](const T& value) {
    return runtime_config::GetSaveStatus(section.c_str(), key.c_str(), serialize(value));
  };
  return definition;
}

template <typename T>
ValueDefinition<T> PersistedDefinition(ValueDefinition<T> definition, SettingMetadata metadata, T default_value,
                                       std::chrono::milliseconds delay = {})
{
  return PersistedDefinition(
      std::move(definition), std::move(metadata), std::move(default_value),
      [](const T& value) -> config_edit::Value {
        if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>)
          return value;
        else if constexpr (std::is_integral_v<T>)
          return static_cast<std::int64_t>(value);
        else if constexpr (std::is_floating_point_v<T>)
          return static_cast<double>(value);
        else
          static_assert(std::is_same_v<T, void>, "setting requires an explicit serializer");
      },
      delay);
}
} // namespace mod_settings

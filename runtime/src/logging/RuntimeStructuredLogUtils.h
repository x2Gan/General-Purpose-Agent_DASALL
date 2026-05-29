#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "logging/ILogger.h"

namespace dasall::runtime::detail {

[[nodiscard]] inline std::string compact_log_value(
    const std::string_view value,
    const std::size_t max_length = 160) {
  if (value.size() <= max_length) {
    return std::string(value);
  }

  if (max_length <= 3U) {
    return std::string(value.substr(0, max_length));
  }

  return std::string(value.substr(0, max_length - 3U)) + "...";
}

inline void add_string_attr(
    infra::LogEvent::AttributeMap& attrs,
    std::string key,
    const std::string_view value) {
  if (value.empty()) {
    return;
  }

  attrs.emplace(std::move(key), compact_log_value(value));
}

inline void add_optional_string_attr(
    infra::LogEvent::AttributeMap& attrs,
    std::string key,
    const std::optional<std::string>& value) {
  if (!value.has_value() || value->empty()) {
    return;
  }

  add_string_attr(attrs, std::move(key), *value);
}

inline void add_bool_attr(
    infra::LogEvent::AttributeMap& attrs,
    std::string key,
    const bool value) {
  attrs.emplace(std::move(key), value ? "true" : "false");
}

template <typename Integer>
inline void add_integer_attr(
    infra::LogEvent::AttributeMap& attrs,
    std::string key,
    const Integer value) {
  attrs.emplace(std::move(key), std::to_string(static_cast<long long>(value)));
}

inline void emit_runtime_log(
    const std::shared_ptr<infra::logging::ILogger>& logger,
    const infra::LogLevel level,
    const std::string_view event_name,
    const std::string_view component,
    const std::optional<std::string>& runtime_instance_id,
    infra::LogEvent::AttributeMap attrs) {
  if (!logger) {
    return;
  }

  attrs.emplace("event_name", std::string(event_name));
  attrs.emplace("component", std::string(component));
  add_optional_string_attr(attrs, "runtime_instance_id", runtime_instance_id);

  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  (void)logger->log(infra::LogEvent{
      .level = level,
      .module = std::string(component),
      .message = std::string(event_name),
      .attrs = std::move(attrs),
      .ts = now_ms,
  });
}

}  // namespace dasall::runtime::detail
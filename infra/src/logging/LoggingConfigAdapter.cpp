#include "LoggingConfigAdapter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <string>
#include <string_view>

#include "logging/LoggingErrors.h"

namespace dasall::infra::logging {

namespace {

constexpr std::string_view kLoggingConfigAdapterSourceRef = "LoggingConfigAdapter";
constexpr std::string_view kFallbackSourceId = "config://fallback";

struct LoggingConfigQueryDescriptor {
  std::string_view key_path;
  config::ConfigValueType value_type;
  std::string_view fallback_serialized_value;
};

constexpr std::array<LoggingConfigQueryDescriptor, 12> kLoggingConfigQueries{{
    {"infra.logging.level", config::ConfigValueType::String, "info"},
    {"infra.logging.format", config::ConfigValueType::String, "json_line"},
    {"infra.logging.async.enabled", config::ConfigValueType::Boolean, "true"},
    {"infra.logging.async.queue_size", config::ConfigValueType::UnsignedInteger, "8192"},
    {"infra.logging.async.overflow_policy", config::ConfigValueType::String, "block"},
    {"infra.logging.file.path", config::ConfigValueType::String, "logs/runtime.log"},
    {"infra.logging.file.rotate.max_size_mb", config::ConfigValueType::UnsignedInteger, "50"},
    {"infra.logging.file.rotate.max_files", config::ConfigValueType::UnsignedInteger, "10"},
    {"infra.logging.redaction.enabled", config::ConfigValueType::Boolean, "true"},
    {"infra.logging.redaction.ruleset", config::ConfigValueType::String, "default_v1"},
    {"infra.logging.export.enable_diag_pull", config::ConfigValueType::Boolean, "true"},
    {"infra.audit.required", config::ConfigValueType::Boolean, "true"},
}};

[[nodiscard]] std::string to_ascii_lower(std::string_view value) {
  std::string normalized(value);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return normalized;
}

[[nodiscard]] LoggingConfigApplyResult make_config_invalid_failure(
    std::string message,
    std::string stage,
    std::vector<std::string> rejected_keys = {}) {
  const auto mapping = map_logging_error_code(LoggingErrorCode::ConfigInvalid);
  return LoggingConfigApplyResult::failure(
      mapping.result_code,
      std::move(message),
      std::move(stage),
      std::string(kLoggingConfigAdapterSourceRef),
      std::move(rejected_keys));
}

void append_unique_key(std::vector<std::string>& rejected_keys, std::string key_path) {
  if (std::find(rejected_keys.begin(), rejected_keys.end(), key_path) == rejected_keys.end()) {
    rejected_keys.push_back(std::move(key_path));
  }
}

[[nodiscard]] config::ConfigQuery make_query(const LoggingConfigQueryDescriptor& descriptor) {
  return config::ConfigQuery{
      .key_path = std::string(descriptor.key_path),
      .expected_type = descriptor.value_type,
      .default_policy = config::ConfigDefaultPolicy::ReturnFallback,
      .fallback_serialized_value = std::string(descriptor.fallback_serialized_value),
  };
}

[[nodiscard]] config::TypedConfig make_fallback_config(const LoggingConfigQueryDescriptor& descriptor) {
  return config::TypedConfig{
      .key_path = std::string(descriptor.key_path),
      .value_type = descriptor.value_type,
      .serialized_value = std::string(descriptor.fallback_serialized_value),
      .schema_version = std::string(config::kConfigSchemaVersionV1),
      .source_kind = config::ConfigSourceKind::Defaults,
      .source_id = std::string(kFallbackSourceId),
      .secret_backed = false,
  };
}

[[nodiscard]] bool parse_bool_value(const std::string_view& serialized_value,
                                    bool& parsed_value) {
  const std::string normalized = to_ascii_lower(serialized_value);
  if (normalized == "true" || normalized == "1") {
    parsed_value = true;
    return true;
  }

  if (normalized == "false" || normalized == "0") {
    parsed_value = false;
    return true;
  }

  return false;
}

[[nodiscard]] bool parse_uint32_value(const std::string_view& serialized_value,
                                      std::uint32_t& parsed_value) {
  try {
    const std::size_t consumed = 0;
    const auto value = std::stoull(std::string(serialized_value));
    (void)consumed;
    if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }

    parsed_value = static_cast<std::uint32_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool parse_log_level_value(const std::string_view& serialized_value,
                                         LogLevel& parsed_value) {
  const std::string normalized = to_ascii_lower(serialized_value);
  if (normalized == "trace") {
    parsed_value = LogLevel::Trace;
    return true;
  }

  if (normalized == "debug") {
    parsed_value = LogLevel::Debug;
    return true;
  }

  if (normalized == "info") {
    parsed_value = LogLevel::Info;
    return true;
  }

  if (normalized == "warn" || normalized == "warning") {
    parsed_value = LogLevel::Warn;
    return true;
  }

  if (normalized == "error") {
    parsed_value = LogLevel::Error;
    return true;
  }

  if (normalized == "fatal") {
    parsed_value = LogLevel::Fatal;
    return true;
  }

  return false;
}

[[nodiscard]] bool parse_logging_format_value(const std::string_view& serialized_value,
                                              LoggingFormat& parsed_value) {
  const std::string normalized = to_ascii_lower(serialized_value);
  if (normalized == "json_line") {
    parsed_value = LoggingFormat::JsonLine;
    return true;
  }

  if (normalized == "key_value") {
    parsed_value = LoggingFormat::KeyValue;
    return true;
  }

  return false;
}

[[nodiscard]] bool parse_logging_overflow_policy_value(
  const std::string_view& serialized_value,
    LoggingOverflowPolicy& parsed_value) {
  const std::string normalized = to_ascii_lower(serialized_value);
  if (normalized == "block") {
    parsed_value = LoggingOverflowPolicy::Block;
    return true;
  }

  if (normalized == "overrun_oldest") {
    parsed_value = LoggingOverflowPolicy::OverrunOldest;
    return true;
  }

  return false;
}

[[nodiscard]] bool assign_entry_to_config(const config::TypedConfig& entry,
                                          LoggingConfig& config,
                                          std::vector<std::string>& parse_errors) {
  if (entry.key_path == "infra.logging.level") {
    if (!parse_log_level_value(entry.serialized_value, config.level)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.format") {
    if (!parse_logging_format_value(entry.serialized_value, config.format)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.async.enabled") {
    if (!parse_bool_value(entry.serialized_value, config.async_enabled)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.async.queue_size") {
    if (!parse_uint32_value(entry.serialized_value, config.queue_size)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.async.overflow_policy") {
    if (!parse_logging_overflow_policy_value(entry.serialized_value,
                                             config.overflow_policy)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.file.path") {
    config.file_path = entry.serialized_value;
    if (config.file_path.empty()) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.file.rotate.max_size_mb") {
    if (!parse_uint32_value(entry.serialized_value, config.rotate_max_size_mb)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.file.rotate.max_files") {
    if (!parse_uint32_value(entry.serialized_value, config.rotate_max_files)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.redaction.enabled") {
    if (!parse_bool_value(entry.serialized_value, config.redaction_enabled)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.redaction.ruleset") {
    config.redaction_ruleset = entry.serialized_value;
    if (config.redaction_ruleset.empty()) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.logging.export.enable_diag_pull") {
    if (!parse_bool_value(entry.serialized_value, config.enable_diag_pull)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  if (entry.key_path == "infra.audit.required") {
    if (!parse_bool_value(entry.serialized_value, config.audit_required)) {
      append_unique_key(parse_errors, entry.key_path);
      return false;
    }

    return true;
  }

  append_unique_key(parse_errors, entry.key_path);
  return false;
}

}  // namespace

LoggingConfigAdapter::LoggingConfigAdapter(const config::IConfigCenter& config_center)
    : config_center_(&config_center) {}

LoggingConfigApplyResult LoggingConfigAdapter::load_and_apply() {
  if (config_center_ == nullptr) {
    return make_config_invalid_failure(
        "logging config adapter requires a config center binding before load_and_apply()",
        "logging.config.load");
  }

  LoggingConfig next_config;
  next_config.source_entries.clear();
  next_config.source_entries.reserve(kLoggingConfigQueries.size());

  std::vector<std::string> parse_errors;
  for (const auto& descriptor : kLoggingConfigQueries) {
    auto typed_config = config_center_->get_typed(make_query(descriptor));
    if (!typed_config.has_value()) {
      typed_config = make_fallback_config(descriptor);
    }

    next_config.source_entries.push_back(*typed_config);
    (void)assign_entry_to_config(*typed_config, next_config, parse_errors);
  }

  if (!parse_errors.empty()) {
    return make_config_invalid_failure(
        "logging config adapter failed to parse one or more frozen key values",
        "logging.config.parse",
        std::move(parse_errors));
  }

  return apply(next_config);
}

LoggingConfigApplyResult LoggingConfigAdapter::apply(const LoggingConfig& config) {
  std::vector<std::string> rejected_keys;

  if (!config.contains_only_frozen_keys()) {
    for (const auto& entry : config.source_entries) {
      if (!is_logging_config_key(entry.key_path)) {
        append_unique_key(rejected_keys, entry.key_path);
      }
    }
  }

  if (!config.has_unique_source_entries()) {
    for (std::size_t index = 0; index < config.source_entries.size(); ++index) {
      const auto duplicate = std::find_if(
          config.source_entries.begin() + index + 1,
          config.source_entries.end(),
          [&](const config::TypedConfig& entry) {
            return entry.key_path == config.source_entries[index].key_path;
          });
      if (duplicate != config.source_entries.end()) {
        append_unique_key(rejected_keys, config.source_entries[index].key_path);
      }
    }
  }

  for (const auto key_path : kLoggingConfigFrozenKeys) {
    if (config.find_source_entry(key_path) == nullptr) {
      append_unique_key(rejected_keys, std::string(key_path));
    }
  }

  for (const auto& entry : config.source_entries) {
    if (!entry.is_valid()) {
      append_unique_key(rejected_keys, entry.key_path);
      continue;
    }

    if (!logging_key_allows_source(entry.key_path, entry.source_kind)) {
      append_unique_key(rejected_keys, entry.key_path);
    }
  }

  if (config.level == LogLevel::Unspecified) {
    append_unique_key(rejected_keys, "infra.logging.level");
  }

  if (config.format == LoggingFormat::Unspecified) {
    append_unique_key(rejected_keys, "infra.logging.format");
  }

  if (config.queue_size == 0U) {
    append_unique_key(rejected_keys, "infra.logging.async.queue_size");
  }

  if (config.overflow_policy == LoggingOverflowPolicy::Unspecified) {
    append_unique_key(rejected_keys, "infra.logging.async.overflow_policy");
  }

  if (config.file_path.empty()) {
    append_unique_key(rejected_keys, "infra.logging.file.path");
  }

  if (config.rotate_max_size_mb == 0U) {
    append_unique_key(rejected_keys, "infra.logging.file.rotate.max_size_mb");
  }

  if (config.rotate_max_files == 0U) {
    append_unique_key(rejected_keys, "infra.logging.file.rotate.max_files");
  }

  if (config.redaction_enabled && config.redaction_ruleset.empty()) {
    append_unique_key(rejected_keys, "infra.logging.redaction.ruleset");
  }

  const auto* audit_required_entry = config.find_source_entry("infra.audit.required");
  if (!config.audit_required || audit_required_entry == nullptr ||
      to_ascii_lower(audit_required_entry->serialized_value) != "true") {
    append_unique_key(rejected_keys, "infra.audit.required");
  }

  if (!rejected_keys.empty()) {
    return make_config_invalid_failure(
        "logging config adapter rejected active config entries that violate the frozen key/source/audit rules",
        "logging.config.validate",
        std::move(rejected_keys));
  }

  if (!config.has_consistent_values()) {
    return make_config_invalid_failure(
        "logging config adapter requires a complete frozen config object before apply()",
        "logging.config.apply");
  }

  active_config_ = config;
  return LoggingConfigApplyResult::success(config.uses_runtime_override());
}

}  // namespace dasall::infra::logging
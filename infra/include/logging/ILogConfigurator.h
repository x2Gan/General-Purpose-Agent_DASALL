#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "LogEvent.h"
#include "config/ConfigTypes.h"
#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::logging {

using LogLevel = ::dasall::infra::LogLevel;

enum class LoggingFormat {
  Unspecified = 0,
  JsonLine = 1,
  KeyValue = 2,
};

enum class LoggingOverflowPolicy {
  Unspecified = 0,
  Block = 1,
  OverrunOldest = 2,
};

inline constexpr std::array<std::string_view, 12> kLoggingConfigFrozenKeys{
    "infra.logging.level",
    "infra.logging.format",
    "infra.logging.async.enabled",
    "infra.logging.async.queue_size",
    "infra.logging.async.overflow_policy",
    "infra.logging.file.path",
    "infra.logging.file.rotate.max_size_mb",
    "infra.logging.file.rotate.max_files",
    "infra.logging.redaction.enabled",
    "infra.logging.redaction.ruleset",
    "infra.logging.export.enable_diag_pull",
    "infra.audit.required",
};

[[nodiscard]] inline constexpr std::string_view logging_format_name(LoggingFormat format) {
  switch (format) {
    case LoggingFormat::Unspecified:
      return "unspecified";
    case LoggingFormat::JsonLine:
      return "json_line";
    case LoggingFormat::KeyValue:
      return "key_value";
  }

  return "unknown";
}

[[nodiscard]] inline constexpr std::string_view logging_overflow_policy_name(
    LoggingOverflowPolicy policy) {
  switch (policy) {
    case LoggingOverflowPolicy::Unspecified:
      return "unspecified";
    case LoggingOverflowPolicy::Block:
      return "block";
    case LoggingOverflowPolicy::OverrunOldest:
      return "overrun_oldest";
  }

  return "unknown";
}

[[nodiscard]] inline bool is_logging_config_key(std::string_view key_path) {
  return std::find(kLoggingConfigFrozenKeys.begin(),
                   kLoggingConfigFrozenKeys.end(),
                   key_path) != kLoggingConfigFrozenKeys.end();
}

[[nodiscard]] inline bool is_logging_runtime_tunable_key(std::string_view key_path) {
  return key_path == "infra.logging.level" || key_path == "infra.logging.file.path" ||
         key_path == "infra.logging.redaction.ruleset";
}

[[nodiscard]] inline bool logging_key_allows_source(std::string_view key_path,
                                                    config::ConfigSourceKind source_kind) {
  using config::ConfigSourceKind;

  if (!is_logging_config_key(key_path)) {
    return false;
  }

  switch (source_kind) {
    case ConfigSourceKind::Defaults:
      return true;
    case ConfigSourceKind::Profile:
      return key_path != "infra.logging.file.path" &&
             key_path != "infra.logging.redaction.ruleset";
    case ConfigSourceKind::DeploymentOverride:
      return key_path == "infra.logging.level" || key_path == "infra.logging.format" ||
             key_path == "infra.logging.async.queue_size" ||
             key_path == "infra.logging.async.overflow_policy" ||
             key_path == "infra.logging.file.path" ||
             key_path == "infra.logging.file.rotate.max_size_mb" ||
             key_path == "infra.logging.file.rotate.max_files" ||
             key_path == "infra.logging.redaction.ruleset" ||
             key_path == "infra.logging.export.enable_diag_pull";
    case ConfigSourceKind::RuntimeOverride:
      return is_logging_runtime_tunable_key(key_path);
    case ConfigSourceKind::Unspecified:
      break;
  }

  return false;
}

struct LoggingConfig {
  LogLevel level = LogLevel::Info;
  LoggingFormat format = LoggingFormat::JsonLine;
  bool async_enabled = true;
  std::uint32_t queue_size = 8192;
  LoggingOverflowPolicy overflow_policy = LoggingOverflowPolicy::Block;
  std::string file_path = "logs/runtime.log";
  std::uint32_t rotate_max_size_mb = 50;
  std::uint32_t rotate_max_files = 10;
  bool redaction_enabled = true;
  std::string redaction_ruleset = "default_v1";
  bool enable_diag_pull = true;
  bool audit_required = true;
  std::vector<config::TypedConfig> source_entries;

  [[nodiscard]] const config::TypedConfig* find_source_entry(std::string_view key_path) const {
    const auto it = std::find_if(source_entries.begin(),
                                 source_entries.end(),
                                 [key_path](const config::TypedConfig& entry) {
                                   return entry.key_path == key_path;
                                 });
    return it == source_entries.end() ? nullptr : &(*it);
  }

  [[nodiscard]] bool has_unique_source_entries() const {
    for (std::size_t index = 0; index < source_entries.size(); ++index) {
      const auto duplicate = std::find_if(source_entries.begin() + index + 1,
                                          source_entries.end(),
                                          [&](const config::TypedConfig& entry) {
                                            return entry.key_path == source_entries[index].key_path;
                                          });
      if (duplicate != source_entries.end()) {
        return false;
      }
    }

    return true;
  }

  [[nodiscard]] bool contains_only_frozen_keys() const {
    return std::all_of(source_entries.begin(),
                       source_entries.end(),
                       [](const config::TypedConfig& entry) {
                         return is_logging_config_key(entry.key_path);
                       });
  }

  [[nodiscard]] bool covers_frozen_keys() const {
    return std::all_of(kLoggingConfigFrozenKeys.begin(),
                       kLoggingConfigFrozenKeys.end(),
                       [&](std::string_view key_path) {
                         return find_source_entry(key_path) != nullptr;
                       });
  }

  [[nodiscard]] bool uses_runtime_override() const {
    return std::any_of(source_entries.begin(),
                       source_entries.end(),
                       [](const config::TypedConfig& entry) {
                         return entry.source_kind == config::ConfigSourceKind::RuntimeOverride;
                       });
  }

  [[nodiscard]] bool has_consistent_values() const {
    return level != LogLevel::Unspecified && format != LoggingFormat::Unspecified &&
           queue_size > 0U && overflow_policy != LoggingOverflowPolicy::Unspecified &&
           !file_path.empty() && rotate_max_size_mb > 0U && rotate_max_files > 0U &&
           (!redaction_enabled || !redaction_ruleset.empty()) && audit_required &&
           !source_entries.empty() && has_unique_source_entries() &&
           contains_only_frozen_keys() && covers_frozen_keys() &&
           std::all_of(source_entries.begin(),
                       source_entries.end(),
                       [](const config::TypedConfig& entry) { return entry.is_valid(); });
  }
};

struct LoggingConfigApplyResult {
  bool applied = false;
  bool runtime_override_active = false;
  std::vector<std::string> rejected_keys;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error_info;

  [[nodiscard]] static LoggingConfigApplyResult success(bool runtime_override_active) {
    return LoggingConfigApplyResult{
        .applied = true,
        .runtime_override_active = runtime_override_active,
        .rejected_keys = {},
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error_info = std::nullopt,
    };
  }

  [[nodiscard]] static LoggingConfigApplyResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref,
      std::vector<std::string> rejected_keys = {}) {
    return LoggingConfigApplyResult{
        .applied = false,
        .runtime_override_active = false,
        .rejected_keys = std::move(rejected_keys),
        .result_code = result_code,
        .error_info = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.logging",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error_info.has_value()) {
      return applied;
    }

    return error_info->failure_type.has_value() &&
           *error_info->failure_type == contracts::classify_result_code(result_code);
  }
};

class ILogConfigurator {
 public:
  virtual ~ILogConfigurator() = default;

  [[nodiscard]] virtual LoggingConfigApplyResult apply(const LoggingConfig& config) = 0;
};

}  // namespace dasall::infra::logging
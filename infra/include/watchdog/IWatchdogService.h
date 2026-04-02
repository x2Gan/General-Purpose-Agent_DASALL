#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"

namespace dasall::infra::watchdog {

struct WatchedEntityDescriptor;
struct HeartbeatSample;
struct WatchdogSnapshot;

enum class WatchdogTimeoutLevelPolicy {
  Unspecified = 0,
  WarnThenCritical = 1,
  CriticalOnly = 2,
};

enum class WatchdogEventOverflowPolicy {
  Unspecified = 0,
  Block = 1,
  OverrunOldest = 2,
};

struct WatchdogServiceConfig {
  bool enabled = true;
  std::uint32_t scan_interval_ms = 500;
  std::uint32_t timeout_ms = 15000;
  std::uint32_t grace_ms = 2000;
  std::uint32_t consecutive_miss_threshold = 3;
  WatchdogTimeoutLevelPolicy timeout_level_policy =
      WatchdogTimeoutLevelPolicy::WarnThenCritical;
  std::uint32_t event_queue_size = 2048;
  WatchdogEventOverflowPolicy event_overflow_policy =
      WatchdogEventOverflowPolicy::Block;
  bool recovery_hint_enabled = true;
  bool audit_required = true;
  std::uint32_t max_entities = 1024;
  std::uint32_t safe_mode_scan_interval_ms = 2000;

  [[nodiscard]] bool is_valid() const {
    return scan_interval_ms > 0 && timeout_ms > 0 && grace_ms < timeout_ms &&
           consecutive_miss_threshold > 0 &&
           timeout_level_policy != WatchdogTimeoutLevelPolicy::Unspecified &&
           event_queue_size > 0 &&
           event_overflow_policy != WatchdogEventOverflowPolicy::Unspecified &&
           max_entities > 0 && safe_mode_scan_interval_ms >= scan_interval_ms;
  }
};

struct WatchdogOperationResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static WatchdogOperationResult success() {
    return WatchdogOperationResult{
        .ok = true,
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static WatchdogOperationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return WatchdogOperationResult{
        .ok = false,
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }
};

struct WatchdogSnapshotQueryResult {
  bool ok = false;
  std::shared_ptr<const WatchdogSnapshot> snapshot;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static WatchdogSnapshotQueryResult success(
      std::shared_ptr<const WatchdogSnapshot> snapshot) {
    return WatchdogSnapshotQueryResult{
        .ok = true,
        .snapshot = std::move(snapshot),
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static WatchdogSnapshotQueryResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return WatchdogSnapshotQueryResult{
        .ok = false,
        .snapshot = nullptr,
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.watchdog",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return ok && !result_code.has_value();
    }

    return result_code.has_value() && error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool has_snapshot() const {
    return ok && snapshot != nullptr && !result_code.has_value() &&
           !error.has_value();
  }
};

class IWatchdogService {
 public:
  virtual ~IWatchdogService() = default;

  virtual WatchdogOperationResult init(const WatchdogServiceConfig& config) = 0;
  virtual WatchdogOperationResult start() = 0;
  virtual WatchdogOperationResult stop(std::uint32_t timeout_ms) = 0;
  virtual WatchdogOperationResult register_entity(
      const WatchedEntityDescriptor& descriptor) = 0;
  virtual WatchdogOperationResult unregister_entity(
      std::string_view entity_id) = 0;
  virtual WatchdogOperationResult heartbeat(const HeartbeatSample& sample) = 0;
  [[nodiscard]] virtual WatchdogSnapshotQueryResult snapshot() const = 0;
};

}  // namespace dasall::infra::watchdog
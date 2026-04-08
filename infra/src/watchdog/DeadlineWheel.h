#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "error/ErrorInfo.h"
#include "error/ResultCode.h"
#include "watchdog/HeartbeatIngestor.h"
#include "watchdog/HeartbeatRegistry.h"
#include "watchdog/IWatchdogService.h"
#include "watchdog/WatchdogErrors.h"

namespace dasall::platform {
class ITimer;
}

namespace dasall::infra::watchdog {

struct DeadlineCandidate {
  WatchedEntityDescriptor descriptor{};
  HeartbeatSample latest_sample{};
  std::int64_t overdue_by_ms = 0;

  [[nodiscard]] bool has_consistent_values() const {
    return descriptor.has_required_fields() && latest_sample.has_required_fields() &&
           descriptor.entity_id == latest_sample.entity_id && overdue_by_ms >= 0;
  }
};

struct DeadlineScanResult {
  bool ok = false;
  bool scheduler_started = false;
  bool safe_observe_mode = false;
  std::vector<DeadlineCandidate> due_candidates;
  std::int64_t scan_ts = 0;
  std::int64_t scan_lag_ms = 0;
  std::optional<WatchdogErrorCode> watchdog_code;
  std::optional<contracts::ResultCode> result_code;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static DeadlineScanResult success(
      std::vector<DeadlineCandidate> due_candidates,
      std::int64_t scan_ts,
      std::int64_t scan_lag_ms,
      bool scheduler_started,
      bool safe_observe_mode) {
    return DeadlineScanResult{
        .ok = true,
        .scheduler_started = scheduler_started,
        .safe_observe_mode = safe_observe_mode,
        .due_candidates = std::move(due_candidates),
        .scan_ts = scan_ts,
        .scan_lag_ms = scan_lag_ms,
        .watchdog_code = std::nullopt,
        .result_code = std::nullopt,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static DeadlineScanResult failure(
      std::optional<WatchdogErrorCode> watchdog_code,
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref,
      std::int64_t scan_ts,
      std::int64_t scan_lag_ms,
      bool scheduler_started,
      bool safe_observe_mode) {
    return DeadlineScanResult{
        .ok = false,
        .scheduler_started = scheduler_started,
        .safe_observe_mode = safe_observe_mode,
        .due_candidates = {},
        .scan_ts = scan_ts,
        .scan_lag_ms = scan_lag_ms,
        .watchdog_code = watchdog_code,
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
    if (!result_code.has_value() && !error.has_value()) {
      return ok;
    }

    return result_code.has_value() && error.has_value() &&
           error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(*result_code);
  }

  [[nodiscard]] bool has_due_candidates() const {
    return ok && !due_candidates.empty();
  }
};

class DeadlineWheel {
 public:
  explicit DeadlineWheel(
      WatchdogServiceConfig config = {},
      const HeartbeatRegistry* registry = nullptr,
      const HeartbeatIngestor* ingestor = nullptr,
      std::shared_ptr<platform::ITimer> scan_scheduler = nullptr);

  void bind_registry(const HeartbeatRegistry* registry);
  void bind_ingestor(const HeartbeatIngestor* ingestor);
  void set_scan_scheduler(std::shared_ptr<platform::ITimer> scan_scheduler);

  [[nodiscard]] DeadlineScanResult tick_collect_due(std::int64_t now_ts);
  [[nodiscard]] DeadlineScanResult scan_once();

  [[nodiscard]] bool scheduler_armed() const {
    return scheduled_scan_handle_id_ != 0U;
  }

  [[nodiscard]] bool safe_observe_mode() const {
    return safe_observe_mode_;
  }

  [[nodiscard]] std::int64_t last_scan_ts() const {
    return last_scan_ts_;
  }

 private:
  [[nodiscard]] DeadlineScanResult invalid_request(std::string message,
                                                   std::string stage,
                                                   std::int64_t scan_ts,
                                                   std::int64_t scan_lag_ms,
                                                   bool scheduler_started,
                                                   bool safe_observe_mode) const;
  [[nodiscard]] DeadlineScanResult platform_failure(std::string message,
                                                    std::string stage,
                                                    std::int64_t scan_ts,
                                                    std::int64_t scan_lag_ms,
                                                    bool scheduler_started,
                                                    bool safe_observe_mode,
                                                    contracts::ResultCode result_code) const;
  [[nodiscard]] DeadlineScanResult scan_overdue(std::int64_t now_ts,
                                                std::int64_t scan_lag_ms,
                                                bool scheduler_started,
                                                std::string detail_suffix);
  [[nodiscard]] std::int64_t expected_next_scan_ts() const;

  WatchdogServiceConfig config_;
  const HeartbeatRegistry* registry_ = nullptr;
  const HeartbeatIngestor* ingestor_ = nullptr;
  std::shared_ptr<platform::ITimer> scan_scheduler_;
  std::uint64_t scheduled_scan_handle_id_ = 0;
  std::int64_t last_scan_ts_ = 0;
  bool safe_observe_mode_ = false;
};

}  // namespace dasall::infra::watchdog
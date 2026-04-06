#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "logging/ILogger.h"
#include "metrics/MetricsBridgeEvent.h"
#include "metrics/MetricsRecovery.h"

namespace dasall::infra::metrics {

struct MetricsLoggingBridgeOptions {
  std::string detail_ref_prefix = "metrics://logging/";
};

struct MetricsLoggingWriteResult {
  bool emitted = false;
  logging::LogEvent log_event{};
  logging::LogWriteResult write_result{};

  [[nodiscard]] bool has_consistent_state() const {
    if (emitted) {
      return write_result.ok && log_event.attrs_are_serializable() &&
             log_event.has_timestamp();
    }

    return !write_result.ok && write_result.references_only_contract_error_types();
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    return write_result.references_only_contract_error_types();
  }
};

struct MetricsLoggingBridgeStatus {
  std::uint64_t emitted_total = 0;
  std::uint64_t emit_failures = 0;
  bool degraded = false;
  std::optional<contracts::ResultCode> last_error_code;
  std::string detail_ref;

  [[nodiscard]] bool is_valid() const {
    if (detail_ref.empty()) {
      return false;
    }

    if (last_error_code.has_value() &&
        contracts::classify_result_code(*last_error_code) ==
            contracts::ResultCodeCategory::Unknown) {
      return false;
    }

    return true;
  }
};

class MetricsLoggingBridge final : public IMetricsRecoveryLogHook {
 public:
  explicit MetricsLoggingBridge(
      std::shared_ptr<logging::ILogger> logger = nullptr,
      MetricsLoggingBridgeOptions options = {});

  void set_logger(std::shared_ptr<logging::ILogger> logger);

  [[nodiscard]] MetricsLoggingWriteResult write_log_event(
      const MetricsBridgeEvent& event);
  MetricsOperationStatus write_recovery_event(
      const MetricsRecoveryEvent& event) override;

  [[nodiscard]] MetricsLoggingBridgeStatus get_status() const;

 private:
  [[nodiscard]] logging::LogEvent make_log_event(const MetricsBridgeEvent& event) const;
  void record_success(const std::string& detail_ref);
  void record_failure(std::optional<contracts::ResultCode> result_code,
                      const std::string& detail_ref);

  std::shared_ptr<logging::ILogger> logger_;
  MetricsLoggingBridgeOptions options_{};
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::metrics
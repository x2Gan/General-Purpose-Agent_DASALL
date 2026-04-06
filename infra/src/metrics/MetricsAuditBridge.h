#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "audit/IAuditLogger.h"
#include "metrics/MetricsBridgeEvent.h"
#include "metrics/MetricsRecovery.h"

namespace dasall::infra::metrics {

struct MetricsAuditBridgeOptions {
  std::string detail_ref_prefix = "metrics://audit/";
  std::string event_id_prefix = "metrics-audit-event-";
};

struct MetricsAuditWriteResult {
  bool emitted = false;
  AuditEvent audit_event{};
  AuditContext audit_context{};
  AuditWriteOutcome write_outcome{};

  [[nodiscard]] bool has_consistent_state() const {
    if (!emitted) {
      return write_outcome.has_consistent_state();
    }

    return audit_event.has_required_fields() &&
           audit_event.side_effects_are_serializable() &&
           audit_context.has_non_empty_fields() &&
           (write_outcome.is_success() || write_outcome.is_degraded_success());
  }
};

struct MetricsAuditBridgeStatus {
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

class MetricsAuditBridge {
 public:
  explicit MetricsAuditBridge(
      std::shared_ptr<audit::IAuditLogger> audit_logger = nullptr,
      MetricsAuditBridgeOptions options = {});

  void set_audit_logger(std::shared_ptr<audit::IAuditLogger> audit_logger);

  [[nodiscard]] MetricsAuditWriteResult write_audit_event(
      const MetricsBridgeEvent& event);
  [[nodiscard]] MetricsAuditWriteResult write_recovery_event(
      const MetricsRecoveryEvent& event,
      const InfraContext& context = {});

  [[nodiscard]] MetricsAuditBridgeStatus get_status() const;

 private:
  [[nodiscard]] AuditEvent make_audit_event(const MetricsBridgeEvent& event);
  [[nodiscard]] AuditContext make_audit_context(const MetricsBridgeEvent& event) const;
  void record_success(const std::string& detail_ref);
  void record_failure(std::optional<contracts::ResultCode> result_code,
                      const std::string& detail_ref);

  std::shared_ptr<audit::IAuditLogger> audit_logger_;
  MetricsAuditBridgeOptions options_{};
  std::uint64_t next_event_sequence_ = 1;
  std::uint64_t emitted_total_ = 0;
  std::uint64_t emit_failures_ = 0;
  std::optional<contracts::ResultCode> last_error_code_;
  std::string last_detail_ref_;
};

}  // namespace dasall::infra::metrics
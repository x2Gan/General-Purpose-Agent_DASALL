#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "audit/IAuditLogger.h"
#include "metrics/MetricsAuditBridge.h"
#include "support/TestAssertions.h"

namespace {

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    last_event = event;
    last_context = context;
    return dasall::infra::AuditWriteOutcome{
        .accepted = true,
        .persisted = true,
        .fallback_used = false,
        .error_code = std::nullopt,
    };
  }

  dasall::infra::ExportResult export_audit(
      const dasall::infra::ExportQuery&) override {
    return dasall::infra::ExportResult{};
  }

  dasall::infra::AuditEvent last_event{};
  dasall::infra::AuditContext last_context{};
};

[[nodiscard]] bool has_prefixed_side_effect(const dasall::infra::AuditEvent& event,
                                            const std::string& prefix) {
  return std::any_of(event.side_effects.begin(),
                     event.side_effects.end(),
                     [&](const std::string& entry) {
                       return entry.rfind(prefix, 0) == 0;
                     });
}

void test_metrics_audit_bridge_keeps_governance_events_inside_frozen_audit_boundary() {
  using dasall::infra::metrics::MetricsAuditBridge;
  using dasall::infra::metrics::MetricsAuditWriteResult;
  using dasall::infra::metrics::MetricsBridgeEvent;
  using dasall::infra::metrics::MetricsBridgeEventKind;
  using dasall::infra::metrics::MetricsBridgeEventOutcome;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&MetricsAuditBridge::write_audit_event),
                               MetricsAuditWriteResult (MetricsAuditBridge::*)(
                                   const MetricsBridgeEvent&)>);

  auto logger = std::make_shared<RecordingAuditLogger>();
  MetricsAuditBridge bridge(logger);

  const auto result = bridge.write_audit_event(MetricsBridgeEvent{
      .kind = MetricsBridgeEventKind::ConfigChange,
      .action = std::string("config_rollback"),
      .stage = std::string("metrics.config.rollback"),
      .outcome = MetricsBridgeEventOutcome::Failure,
      .reason = std::string("metrics configuration rolled back after validation failure"),
      .error_code = dasall::infra::metrics::MetricsErrorCode::ConfigInvalid,
      .module_snapshot = {.queue_depth = 0,
                          .guard_reject_total = 0,
                          .exporter_state = std::string("noop"),
                          .degraded = true},
      .context = {.infra_context = {.request_id = std::string("req-contract-metrics-019"),
                                    .session_id = std::string("session-contract-metrics-019"),
                                    .trace_id = std::string("trace-contract-metrics-019"),
                                    .task_id = std::string("task-contract-metrics-019"),
                                    .parent_task_id = std::string("parent-contract-metrics-019"),
                                    .lease_id = std::string("lease-contract-metrics-019")},
                  .worker_type = std::string("infra.metrics")},
      .detail_ref = std::string("metrics://config/rollback/desktop_full"),
      .config_version = std::string("desktop_full:v4"),
      .previous_config_version = std::string("desktop_full:v3"),
      .consecutive_failure_total = 0,
      .degrade_enter_total = 1,
      .recovery_success_total = 0,
      .timestamp_ms = 1712400000200,
  });

  assert_true(result.emitted && result.has_consistent_state(),
              "metrics audit bridge should emit a valid audit payload for configuration rollback governance events");
  assert_true(logger->last_event.has_required_fields() &&
                  logger->last_event.references_contract_boundary() &&
                  logger->last_event.side_effects_are_serializable(),
              "metrics audit bridge should stay inside the frozen AuditEvent boundary and only emit serializable governance facts");
  assert_true(logger->last_event.evidence_ref.kind == dasall::infra::AuditEvidenceKind::ToolResult &&
                  logger->last_event.evidence_ref.ref == "metrics://config/rollback/desktop_full",
              "metrics audit bridge should keep bridge evidence inside the existing ToolResult reference class");
  assert_true(logger->last_context.has_non_empty_fields(),
              "metrics audit bridge should populate the frozen AuditContext correlation fields without adding new public payload members");
  assert_true(!has_prefixed_side_effect(logger->last_event, "request_id:") &&
                  !has_prefixed_side_effect(logger->last_event, "trace_id:"),
              "metrics audit bridge should keep request and trace correlation inside AuditContext instead of leaking them into audit side_effects");
}

}  // namespace

int main() {
  try {
    test_metrics_audit_bridge_keeps_governance_events_inside_frozen_audit_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
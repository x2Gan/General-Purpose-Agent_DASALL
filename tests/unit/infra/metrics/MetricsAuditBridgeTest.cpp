#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/IAuditLogger.h"
#include "metrics/MetricsAuditBridge.h"
#include "support/TestAssertions.h"

namespace {

class ScriptedAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
  dasall::infra::AuditWriteOutcome write_audit(
      const dasall::infra::AuditEvent& event,
      const dasall::infra::AuditContext& context) override {
    events.push_back(event);
    contexts.push_back(context);
    if (!scripted_outcomes.empty()) {
      const auto outcome = scripted_outcomes.front();
      scripted_outcomes.pop_front();
      return outcome;
    }

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

  std::deque<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

void test_metrics_audit_bridge_emits_complete_recovery_audit_payload() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::infra::InfraContext;
  using dasall::infra::metrics::MetricsAuditBridge;
  using dasall::infra::metrics::MetricsModuleSnapshot;
  using dasall::infra::metrics::MetricsRecoveryEvent;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  MetricsAuditBridge bridge(logger);

  const auto result = bridge.write_recovery_event(
      MetricsRecoveryEvent{
          .action = std::string("enter_degraded"),
          .stage = std::string("metrics.recovery.enter_degraded"),
          .reason = std::string("metrics exporter timed out twice consecutively"),
          .error_code = dasall::infra::metrics::MetricsErrorCode::ExportTimeout,
          .module_snapshot = MetricsModuleSnapshot{
              .queue_depth = 7,
              .guard_reject_total = 3,
              .exporter_state = std::string("noop"),
              .degraded = true,
          },
          .consecutive_failure_total = 2,
          .degrade_enter_total = 1,
          .recovery_success_total = 0,
      },
      InfraContext{
          .request_id = std::string("req-metrics-019"),
          .session_id = std::string("session-metrics-019"),
          .trace_id = std::string("trace-metrics-019"),
          .task_id = std::string("task-metrics-019"),
          .parent_task_id = std::string("parent-metrics-019"),
          .lease_id = std::string("lease-metrics-019"),
      });
  const auto status = bridge.get_status();

  assert_true(result.emitted && result.has_consistent_state(),
              "metrics audit bridge should emit a complete AuditEvent/AuditContext payload for degraded recovery transitions");
  assert_true(status.is_valid() && status.emitted_total == 1 && !status.degraded,
              "metrics audit bridge should remain healthy after the audit sink persists the degraded recovery event");

  const auto& event = logger->events.back();
  const auto& context = logger->contexts.back();
  assert_equal(std::string("metrics.enter_degraded"),
               event.action,
               "metrics audit bridge should map degraded recovery transitions to the frozen metrics.enter_degraded action");
  assert_equal(std::string("metrics:recovery"),
               event.target,
               "metrics audit bridge should keep recovery governance events inside the metrics:recovery audit namespace");
  assert_true(event.outcome == AuditOutcome::Escalated,
              "metrics audit bridge should map degraded recovery transitions to AuditOutcome::Escalated");
  assert_true(event.evidence_ref.kind == AuditEvidenceKind::ToolResult &&
                  event.evidence_ref.ref == "metrics://recovery/enter_degraded",
              "metrics audit bridge should keep evidence inside the existing ToolResult reference boundary");
  assert_true(has_side_effect(event, "stage:metrics.recovery.enter_degraded") &&
                  has_side_effect(event, "error_code:MET_E_EXPORT_TIMEOUT") &&
                  has_side_effect(event, "queue_depth:7") &&
                  has_side_effect(event, "degraded:true"),
              "metrics audit bridge should serialize the frozen recovery facts into unique audit side_effect entries");
  assert_equal(std::string("req-metrics-019"),
               context.request_id,
               "metrics audit bridge should propagate request correlation into AuditContext.request_id");
  assert_equal(std::string("infra.metrics"),
               context.worker_type,
               "metrics audit bridge should pin worker_type=infra.metrics on emitted audit contexts");
}

void test_metrics_audit_bridge_surfaces_sink_failures_for_followup_diagnostics() {
  using dasall::contracts::ResultCode;
  using dasall::infra::metrics::MetricsAuditBridge;
  using dasall::infra::metrics::MetricsBridgeEvent;
  using dasall::infra::metrics::MetricsBridgeEventKind;
  using dasall::infra::metrics::MetricsBridgeEventOutcome;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = ResultCode::ProviderTimeout,
  });

  MetricsAuditBridge bridge(logger);
  const auto result = bridge.write_audit_event(MetricsBridgeEvent{
      .kind = MetricsBridgeEventKind::ConfigChange,
      .action = std::string("config_rollback"),
      .stage = std::string("metrics.config.rollback"),
      .outcome = MetricsBridgeEventOutcome::Failure,
      .reason = std::string("metrics histogram buckets rolled back after validation failure"),
      .error_code = dasall::infra::metrics::MetricsErrorCode::ConfigInvalid,
      .module_snapshot = {.queue_depth = 0,
                          .guard_reject_total = 0,
                          .exporter_state = std::string("noop"),
                          .degraded = true},
      .context = {},
      .detail_ref = std::string("metrics://config/rollback/desktop_full"),
      .config_version = std::string("desktop_full:v3"),
      .previous_config_version = std::string("desktop_full:v2"),
      .consecutive_failure_total = 0,
      .degrade_enter_total = 1,
      .recovery_success_total = 0,
      .timestamp_ms = 1712400000100,
  });
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.has_consistent_state(),
              "metrics audit bridge should surface sink persistence failures without fabricating a successful governance write");
  assert_true(status.is_valid() && status.degraded && status.emit_failures == 1 &&
                  status.last_error_code == ResultCode::ProviderTimeout,
              "metrics audit bridge should retain the last sink failure result code for later health and retry decisions");
}

}  // namespace

int main() {
  try {
    test_metrics_audit_bridge_emits_complete_recovery_audit_payload();
    test_metrics_audit_bridge_surfaces_sink_failures_for_followup_diagnostics();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
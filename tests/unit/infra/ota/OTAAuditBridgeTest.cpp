#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/IAuditLogger.h"
#include "ota/OTAAuditBridge.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::ota::OTAAuditRecord make_ota_audit_record(
    bool succeeded = true) {
  using dasall::infra::ota::OTAAuditRecord;

  return OTAAuditRecord{
      .actor = std::string("runtime"),
      .plan_id = std::string("ota-plan-001"),
      .package_id = std::string("package-001"),
      .target_scope = std::string("slot-group/system"),
      .succeeded = succeeded,
      .evidence_ref = std::string("evidence-001"),
      .rollback_id = std::string("rollback-001"),
      .request_id = std::string("req-001"),
      .trace_id = std::string("trace-001"),
      .task_id = std::string("task-001"),
  };
}

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

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
      const dasall::infra::ExportQuery& query) override {
    static_cast<void>(query);
    return dasall::infra::ExportResult{};
  }

  std::deque<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

void test_ota_audit_bridge_emits_complete_precheck_apply_and_rollback_events() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::infra::kAuditContextUnknown;
  using dasall::infra::ota::OTAAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  OTAAuditBridge bridge(logger);

  const auto precheck_result = bridge.write_precheck_audit(make_ota_audit_record(true));
  const auto apply_result = bridge.write_apply_audit(make_ota_audit_record(true));
  const auto rollback_result = bridge.write_rollback_audit(make_ota_audit_record(true));

  assert_true(precheck_result.emitted && precheck_result.is_valid(),
              "OTAAuditBridge should emit a valid ota.precheck audit record with complete payloads");
  assert_true(apply_result.emitted && apply_result.is_valid(),
              "OTAAuditBridge should emit a valid ota.apply audit record for high-risk OTA execution");
  assert_true(rollback_result.emitted && rollback_result.is_valid(),
              "OTAAuditBridge should emit a valid ota.rollback audit record for rollback completion");
  assert_equal(3, static_cast<int>(logger->events.size()),
               "OTAAuditBridge should dispatch one AuditEvent per precheck/apply/rollback wrapper call");

  const auto& precheck_event = logger->events.front();
  const auto& precheck_context = logger->contexts.front();
  assert_equal(std::string("ota.precheck"),
               precheck_event.action,
               "OTAAuditBridge should map precheck emissions to the frozen ota.precheck action name");
  assert_equal(std::string("ota:slot-group/system"),
               precheck_event.target,
               "OTAAuditBridge should encode target_scope inside the frozen ota: audit namespace");
  assert_true(precheck_event.outcome == AuditOutcome::Succeeded,
              "OTAAuditBridge should report successful precheck runs as AuditOutcome::Succeeded");
  assert_true(precheck_event.evidence_ref.kind == AuditEvidenceKind::ToolResult &&
                  precheck_event.evidence_ref.ref == "evidence-001",
              "OTAAuditBridge should keep precheck evidence inside the ToolResult audit evidence boundary");
  assert_true(has_side_effect(precheck_event, "plan_id:ota-plan-001") &&
                  has_side_effect(precheck_event, "package_id:package-001") &&
                  has_side_effect(precheck_event, "target_scope:slot-group/system") &&
                  has_side_effect(precheck_event, "rollback_id:rollback-001"),
              "OTAAuditBridge should serialize the frozen actor/plan/package/target/rollback fields into auditable side_effect entries");
  assert_true(precheck_event.event_id.rfind("ota-audit-event-", 0) == 0 &&
                  precheck_event.timestamp > 0,
              "OTAAuditBridge should stamp every audit payload with an internal event id and timestamp");
  assert_equal(std::string("req-001"), precheck_context.request_id,
               "OTAAuditBridge should propagate request_id into AuditContext.request_id");
  assert_equal(std::string("trace-001"), precheck_context.trace_id,
               "OTAAuditBridge should propagate trace_id into AuditContext.trace_id");
  assert_equal(std::string("task-001"), precheck_context.task_id,
               "OTAAuditBridge should propagate task_id into AuditContext.task_id");
  assert_equal(std::string("ota"), precheck_context.worker_type,
               "OTAAuditBridge should identify the emitting worker_type as ota");
  assert_equal(std::string(kAuditContextUnknown), precheck_context.session_id,
               "OTAAuditBridge should keep unfrozen session_id on the unknown default");

  assert_equal(std::string("ota.apply"), logger->events[1].action,
               "OTAAuditBridge should map apply emissions to the frozen ota.apply action name");
  assert_true(logger->events[1].outcome == AuditOutcome::Succeeded,
              "OTAAuditBridge should report successful apply runs as AuditOutcome::Succeeded");
  assert_equal(std::string("ota.rollback"), logger->events[2].action,
               "OTAAuditBridge should map rollback emissions to the frozen ota.rollback action name");
  assert_true(logger->events[2].evidence_ref.kind == AuditEvidenceKind::RecoveryOutcome,
              "OTAAuditBridge should keep rollback evidence inside the RecoveryOutcome audit evidence boundary");
}

void test_ota_audit_bridge_maps_rejected_failed_and_escalated_outcomes() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::ota::OTAAuditBridge;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = true,
      .persisted = true,
      .fallback_used = false,
      .error_code = std::nullopt,
  });
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = true,
      .persisted = true,
      .fallback_used = false,
      .error_code = std::nullopt,
  });
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = true,
      .persisted = true,
      .fallback_used = true,
      .error_code = std::nullopt,
  });

  OTAAuditBridge bridge(logger);

  const auto precheck_result = bridge.write_precheck_audit(make_ota_audit_record(false));
  const auto apply_result = bridge.write_apply_audit(make_ota_audit_record(false));
  const auto rollback_result = bridge.write_rollback_audit(make_ota_audit_record(false));

  assert_true(precheck_result.emitted && precheck_result.is_valid(),
              "OTAAuditBridge should emit a valid rejected audit record for failed precheck gates");
  assert_true(apply_result.emitted && apply_result.is_valid(),
              "OTAAuditBridge should emit a valid failed audit record for ota.apply failures");
  assert_true(rollback_result.emitted && rollback_result.is_valid() &&
                  rollback_result.write_outcome.is_degraded_success(),
              "OTAAuditBridge should accept degraded-success audit sink outcomes for ota.rollback failure escalation records");
  assert_true(logger->events[0].outcome == AuditOutcome::Rejected,
              "OTAAuditBridge should map failed precheck events to AuditOutcome::Rejected");
  assert_true(logger->events[1].outcome == AuditOutcome::Failed,
              "OTAAuditBridge should map failed apply events to AuditOutcome::Failed");
  assert_true(logger->events[2].outcome == AuditOutcome::Escalated,
              "OTAAuditBridge should map failed rollback events to AuditOutcome::Escalated");
}

void test_ota_audit_bridge_requires_audit_logger_for_high_risk_actions() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ota::OTAAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  OTAAuditBridge bridge;

  const auto result = bridge.write_apply_audit(make_ota_audit_record(true));
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.is_valid() &&
                  result.result_code == ResultCode::RuntimeRetryExhausted,
              "OTAAuditBridge should reject ota.apply when no mandatory audit logger sink is configured");
  assert_true(result.error_info.has_value() &&
                  result.error_info->details.message.find("audit::IAuditLogger") != std::string::npos,
              "OTAAuditBridge should surface the missing audit sink in the returned error payload");
  assert_equal(1, static_cast<int>(status.emit_failures),
               "OTAAuditBridge should count missing audit sink as an explicit emit failure");
  assert_true(status.degraded && status.is_valid() &&
                  status.last_error_code == ResultCode::RuntimeRetryExhausted,
              "OTAAuditBridge should expose degraded status after a mandatory audit sink is missing");
}

void test_ota_audit_bridge_surfaces_audit_write_failures_with_infra_error_mapping() {
  using dasall::contracts::ResultCode;
  using dasall::infra::ota::OTAAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  logger->scripted_outcomes.push_back(dasall::infra::AuditWriteOutcome{
      .accepted = false,
      .persisted = false,
      .fallback_used = false,
      .error_code = ResultCode::ProviderTimeout,
  });

  OTAAuditBridge bridge(logger);

  const auto result = bridge.write_rollback_audit(make_ota_audit_record(false));
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.is_valid() &&
                  result.result_code == ResultCode::RuntimeRetryExhausted,
              "OTAAuditBridge should map any non-success audit write outcome to INF_E_AUDIT_WRITE_FAIL and keep the failure inside the frozen infra error domain");
  assert_true(result.error_info.has_value() &&
                  result.error_info->details.message.find("INF_E_AUDIT_WRITE_FAIL") != std::string::npos,
              "OTAAuditBridge should surface the frozen infra audit failure code in the returned error payload");
  assert_equal(1, static_cast<int>(status.emit_failures),
               "OTAAuditBridge should track explicit audit sink write failures for later health evaluation");
  assert_true(status.degraded && status.is_valid() &&
                  status.last_error_code == ResultCode::RuntimeRetryExhausted,
              "OTAAuditBridge should expose degraded status with the mapped audit failure result code after a sink write failure");
}

}  // namespace

int main() {
  try {
    test_ota_audit_bridge_emits_complete_precheck_apply_and_rollback_events();
    test_ota_audit_bridge_maps_rejected_failed_and_escalated_outcomes();
    test_ota_audit_bridge_requires_audit_logger_for_high_risk_actions();
    test_ota_audit_bridge_surfaces_audit_write_failures_with_infra_error_mapping();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
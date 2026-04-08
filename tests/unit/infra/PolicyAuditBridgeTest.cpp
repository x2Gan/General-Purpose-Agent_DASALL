#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/IAuditLogger.h"
#include "policy/PolicyAuditBridge.h"
#include "policy/PolicyErrors.h"
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

dasall::infra::policy::PolicyPatch make_patch() {
  return dasall::infra::policy::PolicyPatch{
      .patch_id = std::string("policy-patch-019"),
      .base_generation = 7,
      .operations = {dasall::infra::policy::PolicyPatchOperation{
          .operation = dasall::infra::policy::PolicyPatchOperationType::RemoveRule,
          .rule_id = std::string("plugin-policy-rule"),
          .rule = std::nullopt,
          .mode = dasall::infra::policy::PolicyMode::Unspecified,
      }},
      .actor = std::string("ops-user"),
      .reason = std::string("ticket-019"),
  };
}

dasall::infra::policy::PolicyOpResult make_patch_failure_result() {
  return dasall::infra::policy::PolicyOpResult::failure(
      dasall::infra::policy::map_policy_error_code(
          dasall::infra::policy::PolicyErrorCode::StoreCommitFailed)
          .result_code,
      std::string(dasall::infra::policy::policy_error_code_name(
          dasall::infra::policy::PolicyErrorCode::StoreCommitFailed)) +
          ": policy_store_commit_failed",
      "policy.apply_patch",
      "PolicySnapshotStore");
}

dasall::infra::policy::PolicyQueryContext make_query() {
  return dasall::infra::policy::PolicyQueryContext{
      .module = std::string("plugin"),
      .operation = std::string("load"),
      .target_type = std::string("manifest"),
      .target_ref = std::string("plugin.echo"),
      .actor_ref = std::string("ops-user"),
      .request_id = std::string("req-019"),
      .session_id = std::string("sess-019"),
      .trace_id = std::string("trace-019"),
      .task_id = std::string("task-019"),
      .profile_id = std::string("desktop_full"),
  };
}

dasall::infra::policy::PolicyDecisionRef make_deny_decision() {
  return dasall::infra::policy::PolicyDecisionRef{
      .decision = dasall::infra::policy::PolicyDecision::Deny,
      .reason_code = std::string("plugin_denied_high_risk"),
      .matched_rule_ids = {std::string("plugin-policy-rule")},
      .snapshot_id = std::string("policy-snapshot-9"),
      .generation = 9,
      .evidence_ref = std::string("audit://policy/decision/plugin-denied"),
      .warnings = {},
  };
}

void test_policy_audit_bridge_emits_high_risk_deny_with_frozen_audit_payload() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::AuditOutcome;
  using dasall::infra::policy::PolicyAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  PolicyAuditBridge bridge(logger);

  const auto result = bridge.emit_high_risk_deny(make_query(), make_deny_decision());
  const auto status = bridge.get_status();

  assert_true(result.emitted && result.is_valid(),
              "PolicyAuditBridge should emit a valid audit payload for high-risk deny decisions");
  assert_equal(1, static_cast<int>(logger->events.size()),
               "PolicyAuditBridge should dispatch one AuditEvent for a high-risk deny emission");
  assert_true(status.is_valid() && status.emitted_total == 1 && !status.degraded,
              "PolicyAuditBridge should keep a healthy status after a successful high-risk deny emission");

  const auto& event = logger->events.front();
  const auto& context = logger->contexts.front();
  assert_equal(std::string("policy.query_deny"),
               event.action,
               "PolicyAuditBridge should map deny emissions to the frozen policy.query_deny action");
  assert_equal(std::string("policy_target:plugin:manifest:plugin.echo"),
               event.target,
               "PolicyAuditBridge should encode the denied policy target without leaking internal rule structures");
  assert_true(event.outcome == AuditOutcome::Rejected,
              "PolicyAuditBridge should map a deny projection to AuditOutcome::Rejected");
  assert_true(event.evidence_ref.kind == AuditEvidenceKind::ToolResult &&
                  event.evidence_ref.ref.find("policy:decision/") == 0,
              "PolicyAuditBridge should keep deny evidence inside the frozen audit ToolResult reference boundary");
  assert_true(has_side_effect(event, "reason_code:plugin_denied_high_risk") &&
                  has_side_effect(event, "snapshot_id:policy-snapshot-9") &&
                  has_side_effect(event, "generation:9") &&
                  has_side_effect(event, "detail_ref:audit://policy/decision/plugin-denied"),
              "PolicyAuditBridge should serialize deny reason, snapshot and detail references into stable side_effect facts");
  assert_equal(std::string("req-019"), context.request_id,
               "PolicyAuditBridge should propagate request correlation into AuditContext.request_id");
  assert_equal(std::string("task-019"), context.task_id,
               "PolicyAuditBridge should propagate task correlation into AuditContext.task_id");
  assert_equal(std::string("plugin"), context.worker_type,
               "PolicyAuditBridge should map query.module into AuditContext.worker_type");
}

void test_policy_audit_bridge_emits_patch_failure_event_with_stable_failure_facts() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::policy::PolicyAuditBridge;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  PolicyAuditBridge bridge(logger);

  const auto result = bridge.emit_patch_result(make_patch(), make_patch_failure_result());
  const auto status = bridge.get_status();

  assert_true(result.emitted && result.is_valid(),
              "PolicyAuditBridge should still emit a valid audit payload when patch application fails");
  assert_true(status.is_valid() && status.emitted_total == 1 && !status.degraded,
              "PolicyAuditBridge should keep success status when the audit sink itself persists the patch failure event");

  const auto& event = logger->events.front();
  assert_equal(std::string("policy.apply_patch"),
               event.action,
               "PolicyAuditBridge should use the frozen policy.apply_patch action for patch audit events");
  assert_equal(std::string("policy_patch:policy-patch-019"),
               event.target,
               "PolicyAuditBridge should encode the patch id into the audit target namespace");
  assert_true(event.outcome == AuditOutcome::Failed,
              "PolicyAuditBridge should map store commit failures to AuditOutcome::Failed");
  assert_true(has_side_effect(event, "patch_id:policy-patch-019") &&
                  has_side_effect(event, "base_generation:7") &&
                  has_side_effect(event, "reason_code:INF_E_POLICY_STORE_COMMIT_FAILED"),
              "PolicyAuditBridge should serialize patch failure facts without dropping the frozen policy error token");
}

}  // namespace

int main() {
  try {
    test_policy_audit_bridge_emits_high_risk_deny_with_frozen_audit_payload();
    test_policy_audit_bridge_emits_patch_failure_event_with_stable_failure_facts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
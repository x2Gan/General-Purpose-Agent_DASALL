#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "audit/IAuditLogger.h"
#include "policy/PolicyAuditBridge.h"
#include "dasall/tests/support/TestAssertions.h"

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

void test_policy_audit_bridge_keeps_audit_event_inside_frozen_boundary_fields() {
  using dasall::infra::AuditEvidenceKind;
  using dasall::infra::policy::PolicyAuditBridge;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<RecordingAuditLogger>();
  PolicyAuditBridge bridge(logger);

  const auto result = bridge.emit_high_risk_deny(
      dasall::infra::policy::PolicyQueryContext{
          .module = std::string("plugin"),
          .operation = std::string("load"),
          .target_type = std::string("manifest"),
          .target_ref = std::string("plugin.echo"),
          .actor_ref = std::string("ops-user"),
          .request_id = std::string("req-contract-019"),
          .session_id = std::string("session-contract-019"),
          .trace_id = std::string("trace-contract-019"),
          .task_id = std::string("task-contract-019"),
          .profile_id = std::string("desktop_full"),
      },
      dasall::infra::policy::PolicyDecisionRef{
          .decision = dasall::infra::policy::PolicyDecision::Deny,
          .reason_code = std::string("plugin_denied_boundary"),
          .matched_rule_ids = {std::string("plugin-policy-rule"), std::string("policy-loader-default-effect")},
          .snapshot_id = std::string("policy-snapshot-12"),
          .generation = 12,
          .evidence_ref = std::string("audit://policy/decision/plugin-boundary"),
          .warnings = {},
      });

  assert_true(result.emitted && result.is_valid(),
              "PolicyAuditBridge should emit a valid audit payload for denied policy decisions");
  assert_true(logger->last_event.has_required_fields() &&
                  logger->last_event.references_contract_boundary() &&
                  logger->last_event.side_effects_are_serializable(),
              "PolicyAuditBridge should stay within the frozen AuditEvent boundary and only emit serializable audit facts");
  assert_true(logger->last_event.evidence_ref.kind == AuditEvidenceKind::ToolResult &&
                  logger->last_event.evidence_ref.ref.find("policy:decision/") == 0,
              "PolicyAuditBridge should keep policy deny evidence inside the existing audit ToolResult reference class");
  assert_true(!has_prefixed_side_effect(logger->last_event, "matched_rule_ids:") &&
                  !has_prefixed_side_effect(logger->last_event, "effective_rules:"),
              "PolicyAuditBridge should not leak policy implementation internals such as matched rule vectors or effective rule bodies into AuditEvent side_effects");
  assert_true(logger->last_context.has_non_empty_fields(),
              "PolicyAuditBridge should populate the frozen AuditContext correlation fields without adding new public payload members");
}

}  // namespace

int main() {
  try {
    test_policy_audit_bridge_keeps_audit_event_inside_frozen_boundary_fields();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
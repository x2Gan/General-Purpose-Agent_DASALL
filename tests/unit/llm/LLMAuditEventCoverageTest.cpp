#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/observability/LLMAuditBridge.h"

namespace {

using dasall::llm::observability::LLMAuditBridge;
using dasall::llm::observability::LLMAuditContext;
using dasall::llm::observability::LLMAuditEvent;
using dasall::llm::observability::LLMAuditEventKind;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

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

[[nodiscard]] LLMAuditContext make_context() {
  return LLMAuditContext{
      .infra_context = dasall::infra::InfraContext{
          .request_id = "req-028-audit",
          .session_id = "sess-028-audit",
          .trace_id = "trace-028-audit",
          .task_id = "task-028-audit",
          .parent_task_id = "parent-028-audit",
          .lease_id = "lease-028-audit",
      },
      .worker_type = "llm.observability",
  };
}

void test_llm_audit_bridge_emits_trusted_source_failure_rejection() {
  auto logger = std::make_shared<ScriptedAuditLogger>();
  LLMAuditBridge bridge(logger);

  const auto result = bridge.write_audit_event(LLMAuditEvent{
      .kind = LLMAuditEventKind::TrustedSourceFailure,
      .stage = "llm.prompt.policy",
      .reason = "trusted source rejected by active profile allowlist",
      .context = make_context(),
      .detail_ref = "llm://audit/trusted-source-reject",
      .llm_call_id = "call-028-audit-1",
      .prompt_id = "prompt.planner.default",
      .prompt_version = "2026-04-13.1",
      .resolved_route = "deepseek-prod/deepseek-chat",
      .model_name = "deepseek-chat",
      .profile_id = "desktop_full",
      .trusted_source = "snapshot://untrusted/2026-04-13",
      .metadata_field = std::string(),
      .expected_value = std::string(),
      .observed_value = std::string(),
      .reasoning_mode_requested = std::string(),
      .reasoning_mode_effective = std::string(),
      .timestamp_ms = 1712577600600,
  });
  const auto status = bridge.get_status();

  assert_true(result.emitted && result.has_consistent_state(),
              "LLMAuditBridge should emit a valid audit payload for trusted source failures");
  assert_true(status.is_valid() && status.emitted_total == 1U && !status.degraded,
              "LLMAuditBridge should keep a healthy status after a successful trusted source rejection audit emission");
  const auto& event = logger->events.front();
  const auto& context = logger->contexts.front();
  assert_equal(std::string("llm.trusted_source_rejected"),
               event.action,
               "LLMAuditBridge should map trusted source failures to the frozen llm.trusted_source_rejected action");
  assert_equal(std::string("llm:prompt"),
               event.target,
               "LLMAuditBridge should keep trusted source failures in the llm:prompt audit target namespace");
  assert_true(event.outcome == dasall::infra::AuditOutcome::Rejected,
              "LLMAuditBridge should map trusted source failures to AuditOutcome::Rejected");
  assert_true(has_side_effect(event, "trusted_source:snapshot://untrusted/2026-04-13") &&
                  has_side_effect(event, "prompt_id:prompt.planner.default") &&
                  has_side_effect(event, "stage:llm.prompt.policy"),
              "LLMAuditBridge should preserve trusted source, prompt and stage facts as side effects for trusted source failures");
  assert_equal(std::string("trace-028-audit"),
               context.trace_id,
               "LLMAuditBridge should propagate trace_id into AuditContext correlation fields");
}

void test_llm_audit_bridge_emits_reasoning_content_strip_escalation() {
  auto logger = std::make_shared<ScriptedAuditLogger>();
  LLMAuditBridge bridge(logger);

  const auto result = bridge.write_audit_event(LLMAuditEvent{
      .kind = LLMAuditEventKind::ReasoningContentStripped,
      .stage = "llm.response.normalize",
      .reason = "reasoning_content removed before semantic response handoff",
      .context = make_context(),
      .detail_ref = "llm://audit/reasoning-content-strip",
      .llm_call_id = "call-028-audit-2",
      .prompt_id = "prompt.planner.default",
      .prompt_version = "2026-04-13.1",
      .resolved_route = "deepseek-prod/deepseek-reasoner",
      .model_name = "deepseek-reasoner",
      .profile_id = "desktop_full",
      .trusted_source = std::string(),
      .metadata_field = std::string(),
      .expected_value = std::string(),
      .observed_value = std::string(),
      .reasoning_mode_requested = "thinking",
      .reasoning_mode_effective = "non_thinking",
      .timestamp_ms = 1712577600601,
  });

  assert_true(result.emitted && result.has_consistent_state(),
              "LLMAuditBridge should emit a valid audit payload for reasoning_content stripping");
  const auto& event = logger->events.front();
  assert_equal(std::string("llm.reasoning_content_stripped"),
               event.action,
               "LLMAuditBridge should map reasoning content stripping to the frozen llm.reasoning_content_stripped action");
  assert_equal(std::string("llm:response"),
               event.target,
               "LLMAuditBridge should keep reasoning content stripping inside the llm:response target namespace");
  assert_true(event.outcome == dasall::infra::AuditOutcome::Escalated,
              "LLMAuditBridge should map reasoning content stripping to AuditOutcome::Escalated");
  assert_true(has_side_effect(event, "reasoning_mode_requested:thinking") &&
                  has_side_effect(event, "reasoning_mode_effective:non_thinking") &&
                  has_side_effect(event, "llm_call_id:call-028-audit-2"),
              "LLMAuditBridge should preserve reasoning mode and llm_call_id facts as side effects for reasoning content stripping");
}

void test_llm_audit_bridge_emits_metadata_drift_escalation() {
  auto logger = std::make_shared<ScriptedAuditLogger>();
  LLMAuditBridge bridge(logger);

  const auto result = bridge.write_audit_event(LLMAuditEvent{
      .kind = LLMAuditEventKind::MetadataDrift,
      .stage = "llm.provider.catalog",
      .reason = "provider metadata drift detected against verified baseline",
      .context = make_context(),
      .detail_ref = "llm://audit/metadata-drift",
      .llm_call_id = "call-028-audit-3",
      .prompt_id = "prompt.planner.default",
      .prompt_version = "2026-04-13.1",
      .resolved_route = "deepseek-prod/deepseek-reasoner",
      .model_name = "deepseek-reasoner",
      .profile_id = "cloud_full",
      .trusted_source = std::string(),
      .metadata_field = "verification_state",
      .expected_value = "verified",
      .observed_value = "limited",
      .reasoning_mode_requested = std::string(),
      .reasoning_mode_effective = std::string(),
      .timestamp_ms = 1712577600602,
  });

  assert_true(result.emitted && result.has_consistent_state(),
              "LLMAuditBridge should emit a valid audit payload for metadata drift detection");
  const auto& event = logger->events.front();
  assert_equal(std::string("llm.metadata_drift_detected"),
               event.action,
               "LLMAuditBridge should map metadata drift to the frozen llm.metadata_drift_detected action");
  assert_equal(std::string("llm:provider_metadata"),
               event.target,
               "LLMAuditBridge should keep metadata drift inside the llm:provider_metadata target namespace");
  assert_true(event.outcome == dasall::infra::AuditOutcome::Escalated,
              "LLMAuditBridge should map metadata drift to AuditOutcome::Escalated");
  assert_true(has_side_effect(event, "metadata_field:verification_state") &&
                  has_side_effect(event, "expected_value:verified") &&
                  has_side_effect(event, "observed_value:limited"),
              "LLMAuditBridge should preserve metadata field, expected value and observed value as side effects for metadata drift detection");
}

}  // namespace

int main() {
  try {
    test_llm_audit_bridge_emits_trusted_source_failure_rejection();
    test_llm_audit_bridge_emits_reasoning_content_strip_escalation();
    test_llm_audit_bridge_emits_metadata_drift_escalation();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
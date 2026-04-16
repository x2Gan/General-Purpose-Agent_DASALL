#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "RuntimePolicySnapshot.h"
#include "audit/IAuditLogger.h"
#include "ops/ToolAuditBridge.h"
#include "support/TestAssertions.h"

namespace {

using dasall::infra::AuditOutcome;
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
      scripted_outcomes.erase(scripted_outcomes.begin());
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

  std::vector<dasall::infra::AuditWriteOutcome> scripted_outcomes;
  std::vector<dasall::infra::AuditEvent> events;
  std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] bool has_side_effect(const dasall::infra::AuditEvent& event,
                                   const std::string& expected) {
  return std::find(event.side_effects.begin(),
                   event.side_effects.end(),
                   expected) != event.side_effects.end();
}

[[nodiscard]] dasall::contracts::ToolRequest make_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-audit"),
      .tool_call_id = std::string("call-audit"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload = std::string("{\"command\":\"echo secret-payload\"}"),
      .created_at = 1000,
      .goal_id = std::string("goal-audit"),
      .worker_task_id = std::string("worker-audit"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-audit"),
      .tags = std::vector<std::string>{"audit", "tools"},
  };
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_context() {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.main"),
      .session_id = std::string("session-audit"),
      .profile_snapshot = nullptr,
      .trace = {
          .trace_id = std::string("trace-audit"),
          .span_id = std::string("span-audit"),
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::vector<dasall::tools::ToolConfirmationFact>{
          dasall::tools::ToolConfirmationFact{
              .confirmation_id = std::string("confirm-audit"),
              .subject_ref = std::string("goal://audit"),
              .proof_type = std::string("user.approved"),
              .confirmed_at_ms = 900,
          }},
  };
}

[[nodiscard]] dasall::tools::ToolInvocationEnvelope make_success_envelope() {
  return dasall::tools::ToolInvocationEnvelope{
      .tool_result = dasall::contracts::ToolResult{
          .request_id = std::string("req-audit"),
          .tool_call_id = std::string("call-audit"),
          .tool_name = std::string("agent.terminal"),
          .success = true,
          .payload = std::string("{\"stdout\":\"secret-payload\"}"),
          .error = std::nullopt,
          .side_effects = std::vector<std::string>{"safe_mode.enabled"},
          .completed_at = 2000,
          .duration_ms = 15,
          .goal_id = std::string("goal-audit"),
          .worker_task_id = std::string("worker-audit"),
          .tags = std::vector<std::string>{"audit", "tools"},
      },
      .observation = std::nullopt,
      .observation_digest = dasall::contracts::ObservationDigest{
          .observation_id = std::string("obs-audit"),
          .summary = std::string("command completed"),
          .key_facts = std::vector<std::string>{"stdout:ok"},
          .citations = std::vector<std::string>{"tool://call-audit"},
          .confidence = 0.8F,
          .omitted_details = std::vector<std::string>{"stdout truncated"},
          .source = std::nullopt,
          .created_at = 2000,
          .tags = std::vector<std::string>{"audit"},
      },
      .route_facts = dasall::tools::ToolRouteFacts{
          .route_kind = std::string("builtin"),
          .route_ref = std::string("builtin"),
          .decision_reason = std::string("route.builtin.selected"),
          .plugin_id = std::nullopt,
          .server_id = std::nullopt,
      },
      .evidence_refs = std::vector<std::string>{"tool://call-audit"},
      .compensation_hints = std::nullopt,
      .failure_reason_code = std::nullopt,
  };
}

[[nodiscard]] dasall::tools::ToolInvocationEnvelope make_failed_envelope() {
  return dasall::tools::ToolInvocationEnvelope{
      .tool_result = dasall::contracts::ToolResult{
          .request_id = std::string("req-audit"),
          .tool_call_id = std::string("call-audit"),
          .tool_name = std::string("agent.terminal"),
          .success = false,
          .payload = std::string("{\"stderr\":\"secret-payload\"}"),
          .error = dasall::contracts::ErrorInfo{
              .failure_type = dasall::contracts::classify_result_code(
                  dasall::contracts::ResultCode::ProviderTimeout),
              .retryable = true,
              .safe_to_replan = true,
              .details = dasall::contracts::ErrorDetails{
                  .code = static_cast<int>(dasall::contracts::ResultCode::ProviderTimeout),
                  .message = std::string("provider timeout"),
                  .stage = std::string("tools.manager.execute"),
              },
              .source_ref = dasall::contracts::ErrorSourceRefMinimal{
                  .ref_type = std::string("tool_manager"),
                  .ref_id = std::string("agent.terminal"),
              },
          },
          .side_effects = std::vector<std::string>{"safe_mode.partial"},
          .completed_at = 2100,
          .duration_ms = 25,
          .goal_id = std::string("goal-audit"),
          .worker_task_id = std::string("worker-audit"),
          .tags = std::vector<std::string>{"audit", "tools"},
      },
      .observation = std::nullopt,
      .observation_digest = std::nullopt,
      .route_facts = dasall::tools::ToolRouteFacts{
          .route_kind = std::string("builtin"),
          .route_ref = std::string("builtin"),
          .decision_reason = std::string("route.builtin.selected"),
          .plugin_id = std::nullopt,
          .server_id = std::nullopt,
      },
      .evidence_refs = std::vector<std::string>{"tool://call-audit"},
      .compensation_hints = std::nullopt,
      .failure_reason_code = std::string("tool.timeout"),
  };
}

void test_tool_audit_bridge_emits_requested_completed_failed_without_raw_payload() {
  ScriptedAuditLogger logger;
  dasall::tools::ops::ToolAuditBridge bridge(&logger);

  const auto requested = bridge.emit_requested(make_request(), make_context());
  const auto completed = bridge.emit_completed(make_success_envelope());
  const auto failed = bridge.emit_failed(make_failed_envelope());

  assert_true(requested.emitted && completed.emitted && failed.emitted,
              "tool audit bridge should emit requested/completed/failed events when a logger sink is present");
  assert_equal(3,
               static_cast<int>(logger.events.size()),
               "tool audit bridge should persist one audit record for each requested/completed/failed emission");

  const auto& requested_event = logger.events[0];
  assert_equal(std::string("tool.execution.requested"),
               requested_event.action,
               "requested audit should use the frozen tool.execution.requested name");
  assert_true(requested_event.outcome == AuditOutcome::Escalated,
              "requested audit should mark the execution as escalated into the tool runtime");
  assert_true(has_side_effect(requested_event, "tool_name:agent.terminal") &&
                  has_side_effect(requested_event, "caller_domain:runtime.main") &&
                  has_side_effect(requested_event, "confirmation_present:true"),
              "requested audit should preserve tool identity, caller domain and confirmation facts");

  const auto& completed_event = logger.events[1];
  assert_equal(std::string("tool.execution.completed"),
               completed_event.action,
               "completed audit should use the frozen tool.execution.completed name");
  assert_true(completed_event.outcome == AuditOutcome::Succeeded,
              "successful completion audit should map to AuditOutcome::Succeeded");
  assert_true(has_side_effect(completed_event, "side_effect:safe_mode.enabled") &&
                  has_side_effect(completed_event, "digest_confidence:0.800000"),
              "completed audit should keep side effects and digest confidence visible without embedding raw payload");

  const auto& failed_event = logger.events[2];
  assert_equal(std::string("tool.execution.failed"),
               failed_event.action,
               "failed audit should use the frozen tool.execution.failed name");
  assert_true(failed_event.outcome == AuditOutcome::Failed,
              "provider timeout audit should map to AuditOutcome::Failed");
  assert_true(has_side_effect(failed_event, "failure_reason:tool.timeout") &&
                  has_side_effect(failed_event, "error_stage:tools.manager.execute") &&
                  has_side_effect(failed_event, "error_source:agent.terminal"),
              "failed audit should preserve failure reason, stage and error source");

  for (const auto& event : logger.events) {
    assert_true(std::find(event.side_effects.begin(),
                          event.side_effects.end(),
                          std::string("{\"stdout\":\"secret-payload\"}")) ==
                    event.side_effects.end() &&
                    std::find(event.side_effects.begin(),
                              event.side_effects.end(),
                              std::string("{\"stderr\":\"secret-payload\"}")) ==
                    event.side_effects.end(),
                "tool audit bridge should never embed raw result payload strings into audit side effects");
  }

  assert_equal(std::string("session-audit"),
               logger.contexts.front().session_id,
               "tool audit bridge should recover session correlation from the requested invocation context");
  assert_equal(std::string("trace-audit"),
               logger.contexts.front().trace_id,
               "tool audit bridge should recover trace correlation from the requested invocation context");
}

void test_tool_audit_bridge_emits_compensation_and_tracks_missing_sink() {
  ScriptedAuditLogger logger;
  dasall::tools::ops::ToolAuditBridge bridge(&logger);

  static_cast<void>(bridge.emit_requested(make_request(), make_context()));
  const auto compensation = bridge.emit_compensation(
      dasall::tools::CompensationRequest{
          .tool_call_id = std::string("call-audit"),
          .compensation_action = std::string("safe_mode.exit"),
          .target_ref = std::string("goal://audit"),
          .reason_code = std::string("manual_recovery"),
          .evidence_refs = std::vector<std::string>{"recovery://call-audit"},
      },
      dasall::tools::ToolInvocationEnvelope{
          .tool_result = dasall::contracts::ToolResult{
              .request_id = std::nullopt,
              .tool_call_id = std::string("call-audit"),
              .tool_name = std::nullopt,
              .success = false,
              .payload = std::nullopt,
              .error = std::nullopt,
              .side_effects = std::vector<std::string>{"safe_mode.rollback_pending"},
              .completed_at = 2200,
              .duration_ms = 3,
              .goal_id = std::nullopt,
              .worker_task_id = std::nullopt,
              .tags = std::nullopt,
          },
          .observation = std::nullopt,
          .observation_digest = std::nullopt,
          .route_facts = dasall::tools::ToolRouteFacts{
              .route_kind = std::string("builtin"),
              .route_ref = std::string("builtin"),
              .decision_reason = std::string("route.builtin.selected"),
              .plugin_id = std::nullopt,
              .server_id = std::nullopt,
          },
          .evidence_refs = std::vector<std::string>{"recovery://call-audit"},
          .compensation_hints = std::nullopt,
          .failure_reason_code = std::string("tool.manager.compensation_unconfigured"),
      });

  assert_true(compensation.emitted,
              "tool audit bridge should emit compensation audit events when a logger sink is present");
  assert_equal(std::string("tool.compensation.executed"),
               logger.events.back().action,
               "compensation audit should use the frozen tool.compensation.executed name for the current ToolManager hook shape");
  assert_true(has_side_effect(logger.events.back(), "compensation_action:safe_mode.exit") &&
                  has_side_effect(logger.events.back(), "reason_code:manual_recovery") &&
                  has_side_effect(logger.events.back(), "side_effect:safe_mode.rollback_pending"),
              "compensation audit should preserve action, reason code and compensation side effects");
  assert_equal(std::string("session-audit"),
               logger.contexts.back().session_id,
               "compensation audit should reuse cached request correlation for later recovery events");

  dasall::tools::ops::ToolAuditBridge missing_sink_bridge;
  const auto missing_sink_result =
      missing_sink_bridge.emit_requested(make_request(), make_context());
  const auto missing_sink_status = missing_sink_bridge.get_status();
  assert_true(!missing_sink_result.emitted && missing_sink_result.is_valid(),
              "tool audit bridge should surface a missing sink as a structured local failure");
  assert_true(missing_sink_status.is_valid() && missing_sink_status.degraded &&
                  missing_sink_status.emit_failures == 1,
              "tool audit bridge should retain degraded status after a missing sink failure");
}

}  // namespace

int main() {
  try {
    test_tool_audit_bridge_emits_requested_completed_failed_without_raw_payload();
    test_tool_audit_bridge_emits_compensation_and_tracks_missing_sink();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
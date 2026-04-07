#include <algorithm>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "audit/IAuditLogger.h"
#include "tracing/TraceAuditBridge.h"
#include "dasall/tests/support/TestAssertions.h"

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

[[nodiscard]] bool has_prefixed_side_effect(const dasall::infra::AuditEvent& event,
                                            const std::string& prefix) {
  return std::any_of(event.side_effects.begin(),
                     event.side_effects.end(),
                     [&](const std::string& entry) {
                       return entry.rfind(prefix, 0) == 0;
                     });
}

dasall::infra::tracing::TraceAuditContext make_context() {
  return dasall::infra::tracing::TraceAuditContext{
      .infra_context = dasall::infra::InfraContext{
          .request_id = std::string("req-trace-audit-015"),
          .session_id = std::string("sess-trace-audit-015"),
          .trace_id = std::string("trace-trace-audit-015"),
          .task_id = std::string("task-trace-audit-015"),
          .parent_task_id = std::string("parent-trace-audit-015"),
          .lease_id = std::string("lease-trace-audit-015"),
      },
      .worker_type = std::string("infra.tracing"),
  };
}

dasall::infra::tracing::TraceModuleSnapshot make_module_snapshot(bool degraded) {
  return dasall::infra::tracing::TraceModuleSnapshot{
      .queue_depth = 3U,
      .dropped_total = 2U,
      .exporter_state = degraded ? std::string("degraded_noop") : std::string("file"),
      .degraded = degraded,
  };
}

void test_trace_audit_bridge_emits_sampler_change_with_frozen_payload() {
  using dasall::infra::AuditOutcome;
  using dasall::infra::tracing::TraceAuditBridge;
  using dasall::infra::tracing::TraceAuditEvent;
  using dasall::infra::tracing::TraceAuditEventKind;
  using dasall::infra::tracing::TraceAuditEventOutcome;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  auto logger = std::make_shared<ScriptedAuditLogger>();
  TraceAuditBridge bridge(logger);

  const auto result = bridge.write_audit_event(TraceAuditEvent{
      .kind = TraceAuditEventKind::SamplerConfigChange,
      .action = std::string("sampler_changed"),
      .stage = std::string("tracing.config.sampler"),
      .outcome = TraceAuditEventOutcome::Success,
      .reason = std::string("sampler switched by profile override"),
      .error_code = std::nullopt,
      .module_snapshot = make_module_snapshot(false),
      .context = make_context(),
      .detail_ref = std::string("tracing://config/sampler/desktop_full"),
      .current_sampler_type = std::string("ratio"),
      .previous_sampler_type = std::string("parent_based_always_on"),
      .timestamp_ms = 1712486403000,
  });
  const auto status = bridge.get_status();

  assert_true(result.emitted && result.has_consistent_state(),
              "TraceAuditBridge should emit a valid audit payload for sampler change governance events");
  assert_true(status.is_valid() && status.emitted_total == 1 && !status.degraded,
              "TraceAuditBridge should keep a healthy status after a successful sampler change emission");
  const auto& event = logger->events.front();
  const auto& context = logger->contexts.front();
  assert_equal(std::string("tracing.sampler_changed"),
               event.action,
               "TraceAuditBridge should map sampler change emissions to the frozen tracing.sampler_changed action");
  assert_equal(std::string("tracing:sampler"),
               event.target,
               "TraceAuditBridge should keep sampler governance events inside the tracing:sampler target namespace");
  assert_true(event.outcome == AuditOutcome::Succeeded,
              "TraceAuditBridge should map successful sampler changes to AuditOutcome::Succeeded");
  assert_true(has_side_effect(event, "current_sampler_type:ratio") &&
                  has_side_effect(event, "previous_sampler_type:parent_based_always_on") &&
                  has_side_effect(event, "stage:tracing.config.sampler"),
              "TraceAuditBridge should serialize sampler change facts into stable audit side_effects");
  assert_true(!has_prefixed_side_effect(event, "request_id:") &&
                  !has_prefixed_side_effect(event, "trace_id:"),
              "TraceAuditBridge should keep request and trace correlation inside AuditContext instead of leaking them into side_effects");
  assert_equal(std::string("trace-trace-audit-015"),
               context.trace_id,
               "TraceAuditBridge should propagate trace_id into the frozen AuditContext correlation fields");
}

void test_trace_audit_bridge_reports_missing_logger_for_shutdown_fallback() {
  using dasall::contracts::ResultCode;
  using dasall::infra::tracing::TraceAuditBridge;
  using dasall::infra::tracing::TraceAuditEvent;
  using dasall::infra::tracing::TraceAuditEventKind;
  using dasall::infra::tracing::TraceAuditEventOutcome;
  using dasall::infra::tracing::TraceErrorCode;
  using dasall::tests::support::assert_true;

  TraceAuditBridge bridge(nullptr);

  const auto result = bridge.write_audit_event(TraceAuditEvent{
      .kind = TraceAuditEventKind::ShutdownFallback,
      .action = std::string("shutdown_force_fallback"),
      .stage = std::string("tracing.shutdown"),
      .outcome = TraceAuditEventOutcome::Failure,
      .reason = std::string("force_flush timed out before exporter stop"),
      .error_code = TraceErrorCode::ShutdownTimeout,
      .module_snapshot = make_module_snapshot(true),
      .context = make_context(),
      .detail_ref = std::string("tracing://shutdown/fallback/desktop_full"),
        .current_sampler_type = std::string(),
        .previous_sampler_type = std::string(),
      .timestamp_ms = 1712486403200,
  });
  const auto status = bridge.get_status();

  assert_true(!result.emitted && result.has_consistent_state(),
              "TraceAuditBridge should return a failed write result when no audit logger is available for shutdown fallback governance events");
  assert_true(result.write_outcome.error_code == ResultCode::RuntimeRetryExhausted,
              "TraceAuditBridge should normalize missing audit sink failures to RuntimeRetryExhausted");
  assert_true(status.is_valid() && status.degraded &&
                  status.last_error_code == ResultCode::RuntimeRetryExhausted,
              "TraceAuditBridge should expose degraded bridge status after a missing audit logger failure");
}

}  // namespace

int main() {
  try {
    test_trace_audit_bridge_emits_sampler_change_with_frozen_payload();
    test_trace_audit_bridge_reports_missing_logger_for_shutdown_fallback();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
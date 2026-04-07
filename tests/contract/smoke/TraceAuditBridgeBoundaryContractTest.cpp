#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "audit/IAuditLogger.h"
#include "tracing/TraceAuditBridge.h"
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

void test_trace_audit_bridge_keeps_governance_events_inside_frozen_audit_boundary() {
  using dasall::infra::tracing::TraceAuditBridge;
  using dasall::infra::tracing::TraceAuditEvent;
  using dasall::infra::tracing::TraceAuditEventKind;
  using dasall::infra::tracing::TraceAuditEventOutcome;
  using dasall::infra::tracing::TraceAuditWriteResult;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(&TraceAuditBridge::write_audit_event),
                               TraceAuditWriteResult (TraceAuditBridge::*)(
                                   const TraceAuditEvent&)>);

  auto logger = std::make_shared<RecordingAuditLogger>();
  TraceAuditBridge bridge(logger);

  const auto result = bridge.write_audit_event(TraceAuditEvent{
      .kind = TraceAuditEventKind::ExportRecoveryTransition,
      .action = std::string("enter_degraded"),
      .stage = std::string("tracing.export"),
      .outcome = TraceAuditEventOutcome::Degraded,
      .reason = std::string("consecutive export failures triggered degraded mode"),
      .error_code = dasall::infra::tracing::TraceErrorCode::ExportFailure,
      .module_snapshot = {.queue_depth = 4,
                          .dropped_total = 3,
                          .exporter_state = std::string("degraded_noop"),
                          .degraded = true},
      .context = {.infra_context = {.request_id = std::string("req-contract-trace-015"),
                                    .session_id = std::string("session-contract-trace-015"),
                                    .trace_id = std::string("trace-contract-trace-015"),
                                    .task_id = std::string("task-contract-trace-015"),
                                    .parent_task_id = std::string("parent-contract-trace-015"),
                                    .lease_id = std::string("lease-contract-trace-015")},
                  .worker_type = std::string("infra.tracing")},
      .detail_ref = std::string("tracing://export/degraded/collector"),
        .current_sampler_type = std::string(),
        .previous_sampler_type = std::string(),
      .consecutive_failure_total = 2,
      .degrade_enter_total = 1,
      .recovery_success_total = 0,
      .timestamp_ms = 1712486405000,
  });

  assert_true(result.emitted && result.has_consistent_state(),
              "trace audit bridge should emit a valid audit payload for export degradation governance events");
  assert_true(logger->last_event.has_required_fields() &&
                  logger->last_event.references_contract_boundary() &&
                  logger->last_event.side_effects_are_serializable(),
              "trace audit bridge should stay inside the frozen AuditEvent boundary and only emit serializable governance facts");
  assert_true(logger->last_event.evidence_ref.kind == dasall::infra::AuditEvidenceKind::ToolResult &&
                  logger->last_event.evidence_ref.ref == "tracing://export/degraded/collector",
              "trace audit bridge should keep bridge evidence inside the existing ToolResult reference class");
  assert_true(logger->last_context.has_non_empty_fields(),
              "trace audit bridge should populate the frozen AuditContext correlation fields without adding new public payload members");
  assert_true(!has_prefixed_side_effect(logger->last_event, "request_id:") &&
                  !has_prefixed_side_effect(logger->last_event, "trace_id:"),
              "trace audit bridge should keep request and trace correlation inside AuditContext instead of leaking them into audit side_effects");
}

}  // namespace

int main() {
  try {
    test_trace_audit_bridge_keeps_governance_events_inside_frozen_audit_boundary();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
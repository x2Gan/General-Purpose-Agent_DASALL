#include <exception>
#include <iostream>
#include <memory>
#include <string_view>
#include <string>

#include "audit/IAuditLogger.h"
#include "logging/AuditLinkAdapter.h"
#include "logging/LoggingFacade.h"
#include "logging/SinkDispatcher.h"
#include "support/TestAssertions.h"

namespace {

class RecordingAuditLogger final : public dasall::infra::audit::IAuditLogger {
 public:
    dasall::infra::AuditWriteOutcome write_audit(
            const dasall::infra::AuditEvent& event,
            const dasall::infra::AuditContext& context) override {
        events.push_back(event);
        contexts.push_back(context);
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

    std::vector<dasall::infra::AuditEvent> events;
    std::vector<dasall::infra::AuditContext> contexts;
};

[[nodiscard]] bool contains_fragment(
        const std::vector<std::string>& values,
        std::string_view fragment) {
    for (const auto& value : values) {
        if (value.find(fragment) != std::string::npos) {
            return true;
        }
    }

    return false;
}

void test_logging_audit_link_routes_high_risk_events_to_audit_sink() {
  using dasall::infra::InfraContext;
    using dasall::infra::AuditOutcome;
  using dasall::infra::logging::AuditEvidenceKind;
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::infra::logging::SinkRoute;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
    auto audit_logger = std::make_shared<RecordingAuditLogger>();
  auto dispatcher = std::make_unique<SinkDispatcher>();
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));
    facade.attach_audit_logger(audit_logger);

  assert_true(facade.init(InfraContext{
                  .request_id = std::string("req-int-audit-001"),
                  .session_id = std::string("session-int-audit-001"),
                  .trace_id = std::string("trace-int-audit-001"),
                  .task_id = std::string("task-int-audit-001"),
                  .parent_task_id = std::string("parent-int-audit-001"),
                  .lease_id = std::string("lease-int-audit-001"),
              })
                  .ok,
              "logging audit integration should initialize the facade before routing a high-risk event");

  LogEvent event{
      .level = LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("audit integration smoke authorization=secret-int-audit-001"),
      .attrs = {
          {"event_kind", "high_risk"},
          {"authorization", "Bearer secret-int-audit-001"},
      },
      .ts = 1712217600101,
  };
  const AuditRef audit_ref{
      .evidence_ref = {
          .kind = AuditEvidenceKind::RecoveryOutcome,
          .ref = std::string("recovery-int-001"),
      },
      .trace_id = std::string("trace-int-audit-ref-001"),
      .task_id = std::string("task-int-audit-ref-001"),
  };

  const auto attach_result = adapter.attach_audit_ref(event, audit_ref);
  assert_true(attach_result.ok,
              "logging audit integration should attach a complete audit ref before dispatch");

  const auto log_result = facade.log(event);
  assert_true(log_result.ok,
              "logging audit integration should dispatch the linked high-risk event successfully");
  assert_true(dispatcher_ptr->last_route() == SinkRoute::Audit,
              "logging audit integration should route linked high-risk events to the audit sink");
  assert_equal(1,
               static_cast<int>(dispatcher_ptr->dispatched_record_count(SinkRoute::Audit)),
               "logging audit integration should count one audit-routed record");
  assert_equal(1,
               static_cast<int>(audit_logger->events.size()),
               "logging audit integration should persist one correlated audit event");
  assert_true(dispatcher_ptr->last_record().event.attrs.at("evidence_ref") ==
                  "recovery-int-001",
              "logging audit integration should preserve evidence_ref on the routed record");
  assert_true(facade.last_dispatched_event().attrs.at("request_id") ==
                  "req-int-audit-001",
              "logging audit integration should keep context enrichment alongside audit attrs");
  assert_true(dispatcher_ptr->last_record().event.message.find("secret-int-audit-001") ==
                  std::string::npos,
              "logging audit integration should keep secrets out of the persisted ordinary log record");

  const auto& audit_event = audit_logger->events.front();
  const auto& audit_context = audit_logger->contexts.front();
  assert_true(audit_event.evidence_ref.kind == AuditEvidenceKind::RecoveryOutcome,
              "logging audit integration should keep the frozen audit evidence kind during owner handoff");
  assert_true(audit_event.evidence_ref.ref == "recovery-int-001",
              "logging audit integration should correlate audit persistence with the routed evidence ref");
  assert_true(audit_event.outcome == AuditOutcome::Failed,
              "logging audit integration should map a high-risk error route to an audit failure outcome");
  assert_true(audit_context.trace_id == "trace-int-audit-ref-001" &&
                  audit_context.task_id == "task-int-audit-ref-001",
              "logging audit integration should persist the frozen audit trace/task anchors");
  assert_true(audit_event.target.find("secret-int-audit-001") == std::string::npos,
              "logging audit integration should keep raw message payload out of the audit target");
  assert_true(!contains_fragment(audit_event.side_effects, "secret-int-audit-001") &&
                  !contains_fragment(audit_event.side_effects, "authorization"),
              "logging audit integration should keep privacy-sensitive payload out of audit side effects");
}

void test_logging_audit_link_requires_audit_logger_before_dispatch() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::AuditEvidenceKind;
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::infra::logging::LoggingFacade;
  using dasall::infra::logging::SinkDispatcher;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  auto dispatcher = std::make_unique<SinkDispatcher>();
  auto* dispatcher_ptr = dispatcher.get();
  LoggingFacade facade(std::move(dispatcher));
  assert_true(facade.init(dasall::infra::InfraContext{}).ok,
              "logging audit integration should initialize the facade before checking missing audit logger enforcement");

  LogEvent event{
      .level = LogLevel::Error,
      .module = std::string("runtime"),
      .message = std::string("audit integration missing logger"),
      .attrs = {{"event_kind", "high_risk"}},
      .ts = 1712217600102,
  };
  const AuditRef audit_ref{
      .evidence_ref = {
          .kind = AuditEvidenceKind::WorkerTask,
          .ref = std::string("worker-task-int-001"),
      },
      .trace_id = std::string("trace-int-audit-ref-002"),
      .task_id = std::string("task-int-audit-ref-002"),
  };

  const auto attach_result = adapter.attach_audit_ref(event, audit_ref);
  assert_true(attach_result.ok,
              "logging audit integration should attach a complete audit ref before checking missing audit logger enforcement");

  const auto log_result = facade.log(event);
  assert_true(!log_result.ok,
              "logging audit integration should fail closed when a high-risk event lacks an attached audit logger");
  assert_true(log_result.result_code == ResultCode::RuntimeRetryExhausted,
              "logging audit integration should surface missing audit handoff as RuntimeRetryExhausted");
  assert_true(!dispatcher_ptr->has_last_record(),
              "logging audit integration should not dispatch a high-risk record before audit owner handoff is available");
}

void test_logging_audit_link_rejects_incomplete_refs_without_dispatching_side_effects() {
  using dasall::contracts::ResultCode;
  using dasall::infra::logging::AuditEvidenceKind;
  using dasall::infra::logging::AuditLinkAdapter;
  using dasall::infra::logging::AuditRef;
  using dasall::infra::logging::LogEvent;
  using dasall::infra::logging::LogLevel;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AuditLinkAdapter adapter;
  LogEvent event{
      .level = LogLevel::Fatal,
      .module = std::string("runtime"),
      .message = std::string("audit integration rejection"),
      .attrs = {{"event_kind", "high_risk_missing_ref"}},
      .ts = 1712217600103,
  };
  const AuditRef incomplete_ref{
      .evidence_ref = {
          .kind = AuditEvidenceKind::WorkerTask,
          .ref = std::string("worker-task-int-002"),
      },
      .trace_id = std::string("unknown"),
      .task_id = std::string("unknown"),
  };

  const auto attach_result = adapter.attach_audit_ref(event, incomplete_ref);
  assert_true(!attach_result.ok,
              "logging audit integration should reject incomplete audit refs for high-risk events");
  assert_true(attach_result.references_only_contract_error_types(),
              "logging audit integration rejection should stay within contracts error types");
  assert_true(attach_result.result_code == ResultCode::ValidationFieldMissing,
              "logging audit integration should map incomplete refs to ValidationFieldMissing");
  assert_equal(1,
               static_cast<int>(adapter.link_failure_count()),
               "logging audit integration should record one link failure when a ref is incomplete");
  assert_true(!event.attrs.contains("audit_ref_pending"),
              "logging audit integration should not emit partial audit routing attrs on rejection");
}

}  // namespace

int main() {
  try {
    test_logging_audit_link_routes_high_risk_events_to_audit_sink();
        test_logging_audit_link_requires_audit_logger_before_dispatch();
    test_logging_audit_link_rejects_incomplete_refs_without_dispatching_side_effects();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}